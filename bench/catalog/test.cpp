#include "yt/catalog_replay.hpp"
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace yt::catalog;
using yt::string;
namespace {
int checks = 0;
void check(bool ok, const char* what) {
  ++checks;
  if (!ok) { std::cerr << "FAIL " << what << '\n'; std::exit(1); }
}
string read(const string& root, const string& file) {
  std::ifstream in(root+"/"+file); check(bool(in),"fixture exists");
  std::stringstream s; s << in.rdbuf(); return s.str();
}
struct Http : yt::Transport {
  struct Step { string url, reply; Object args; bool auth = false; int status = 200; };
  std::deque<Step> steps;
  void add(string origin, string route, string reply, Object args = {}, bool auth = false, int status = 200) {
    steps.push_back({origin+route,std::move(reply),std::move(args),auth,status});
  }
  yt::Response request(const string& url, const string& body = {}, bool form = false) override {
    yt::Request q; q.url = url; q.body = body; q.form = form; return send(q);
  }
  yt::Response send(const yt::Request& q) override {
    check(!steps.empty(),"unexpected request");
    auto s = std::move(steps.front()); steps.pop_front();
    check(q.url == s.url,"request URL");
    check(q.agent.find("Mozilla/5.0") == 0,"browser identity required by Music");
    bool auth = std::find(q.headers.begin(),q.headers.end(),"Authorization: Bearer fixture-token") != q.headers.end();
    check(auth == s.auth,"token scope and guest state");
    if (!q.body.empty()) {
      J data; check(parse(q.body,data),"request JSON");
      for (const auto& kv : s.args) check(get(data,kv.first) == kv.second,"request fields");
      const auto& client = get(get(data,"context"),"client");
      check(str(client,"hl") == "pt" && str(client,"gl") == "BR","request locale");
    }
    return {s.status,s.reply,{}};
  }
};
const string music = "https://music.youtube.com/", web = "https://www.youtube.com/";
const string browse = "youtubei/v1/browse?prettyPrint=false", search = "youtubei/v1/search?prettyPrint=false", next = "youtubei/v1/next?prettyPrint=false";
const string cid = "UC_x5XG1OV2P6uZZ5FSM9Ttw";
std::map<string,string> request(string kind, string id = {}) { return {{"kind",kind},{"id",id},{"lang","pt"},{"tab","videos"}}; }
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  string dir = argv[1];
  for (const auto& name : {"public-radio","music-home","music-search","music-album","music-artist","music-charts","channel-videos","channel-live","channel-playlists"}) {
    J data; check(parse(read(dir,string(name)+".json"),data),"recording parses");
    Page p = decode(data,string(name).compare(0,7,"channel") != 0);
    check(p.error.empty() && !p.items.empty(),"recorded renderer yields content");
    if (string(name).compare(0,7,"channel") == 0) check(!p.title.empty() && !p.subtitle.empty() && !p.banner.empty() && !p.image.empty(),"modern channel header");
  }
  check(imageUrl("https://yt3.googleusercontent.com/image").size() > 0,"public artwork domain");
  check(!imageUrl("https://music.youtube.com/image/mixart?r=FIXTURE&k=FALLBACK_16X9_MIX").empty(),"recorded TV mix artwork endpoint");
  check(!imageUrl("https://music.youtube.com/image/radioart?r=FIXTURE").empty(),"radio artwork (top result of a Music search) is allowed like mix artwork");
  check(imageUrl("https://music.youtube.com/watch?v=fixture").empty() && imageUrl("https://music.youtube.com.evil/image/mixart?r=FIXTURE").empty(),"Music artwork allowlist is restricted to mixart on the exact host");
  for (auto u : {"https://googleusercontent.com.evil/image","http://i.ytimg.com/image","https://localhost/image","https://i.ytimg.com@evil/image","https://i.ytimg.com:443/image"}) check(imageUrl(u).empty(),"artwork rejects another host");
  J bad; check(!parse(string(81,'{')+string(81,'}'),bad),"depth bound");
  check(!parse("{} trailing",bad) && !parse("[]",bad),"invalid response rejected");
  Http h; Client c(h);
  h.add(music,"",read(dir,"music-context.html"));
  h.add(music,browse,read(dir,"music-home.json"),{{"browseId",J(string("FEmusic_home"))}});
  auto p = c.browse(request("music"));
  check(!p.items.empty() && !p.items.front().section.empty(),"home shelf headings");
  auto cached = c.browse(request("music")); check(p.items.size() == cached.items.size(),"repeat uses cache");
  h.add(music,browse,read(dir,"music-home.json"),{{"browseId",J(string("FEmusic_home"))}});
  auto mixes = request("music"); mixes["q"] = "mixes";
  p = c.browse(mixes); check(!p.items.empty() && std::all_of(p.items.begin(),p.items.end(),[](const Item& i) { return i.type == "playlist"; }),"mixes contain playlists");
  {  // F65: quais prateleiras são pessoais, com os títulos reais de cada idioma (nada renomeado para escapar)
    for (const char* title : {"Mixes de músicas","Mixes de canciones","Seus mixes","Ouvir de novo","Escolhas rápidas","Seleção rápida","Selección rápida","Quick picks","Listen again","Clipes recomendados","Reproduzidos recentemente"})
      check(Client::personalShelf(title),"personal Music shelf title is recognized");
    for (const char* title : {"Hits de hoje","Novos álbuns e singles","Demo música 11","Em alta","Today's biggest hits"})
      check(!Client::personalShelf(title),"generic Music shelf title is not personal");
  }
  {  // F65: chips de clima viram itens "chip" no topo e o pessoal (Ouvir de novo) passa na frente do genérico
    string home = read(dir,"music-home.json");
    auto rename = [&](const string& from, const string& to) { auto at = home.find(from); check(at != string::npos,"fixture shelf title"); home.replace(at,from.size(),to); };
    rename("Mixes de músicas","Generico A"); rename("Hits de hoje","Ouvir de novo");
    h.add(music,browse,home,{{"browseId",J(string("FEmusic_home"))}});
    auto fresh = request("music"); fresh["fresh"] = "1";
    auto shaped = c.browse(fresh);
    size_t chips = 0; while (chips < shaped.items.size() && shaped.items[chips].type == "chip") ++chips;
    check(chips >= 6 && shaped.items.front().section == "chips" && !shaped.items.front().id.empty(),"mood chips come first with their params");
    check(chips < shaped.items.size() && shaped.items[chips].section == "Ouvir de novo","personal shelf before generic shelf");
    check(std::any_of(shaped.items.begin(),shaped.items.end(),[](const Item& i) { return i.type == "video" && i.listStyle; }),"list rows (quick picks) are flagged as list style");
    check(std::none_of(shaped.items.begin()+chips,shaped.items.end(),[](const Item& i) { return i.section == "Ouvir de novo" && i.listStyle; }),"carousel items are not list style");
    check(wire(shaped).find("\trow\n") != string::npos,"list style travels in the wire as the optional field 9");
    check(std::any_of(shaped.items.begin()+chips,shaped.items.end(),[](const Item& i) { return i.section == "Generico A"; }),"generic shelf is kept");
    const string id = shaped.items.front().id; auto one = id.find('|'), two = id.find('|',one+1);
    check(one != string::npos && two != string::npos && id.substr(0,one) == "FEmusic_home" && id.substr(two+1) == shaped.items.front().title,"chip id carries browseId, params and label");
    Object chipArgs{{"browseId",J(string("FEmusic_home"))},{"params",J(id.substr(one+1,two-one-1))}};
    h.add(music,browse,read(dir,"music-home.json"),chipArgs);
    auto mood = request("chip"); mood["id"] = id;
    auto moodPage = c.browse(mood);
    check(!moodPage.items.empty() && moodPage.title == shaped.items.front().title,"a mood chip opens the Music home filtered by its params, titled by its label");
    auto bad = request("chip"); bad["id"] = "not-a-chip"; check(c.browse(bad).error == "invalid_navigation","a malformed chip id is refused before any request");
    bad["id"] = "evil|x|y"; check(c.browse(bad).error == "invalid_navigation","a chip must target a browse id starting with FE");
    h.add(music,browse,read(dir,"music-explore.json"),{{"browseId",J(string("FEmusic_explore"))}});
    auto explore = request("music"); explore["q"] = "explore";
    auto ex = c.browse(explore);
    size_t buttons = 0; while (buttons < ex.items.size() && ex.items[buttons].type == "chip" && ex.items[buttons].section == "chips") ++buttons;
    check(buttons >= 3,"Explore starts with its navigation buttons (releases, charts, moods and genres)");
    check(std::any_of(ex.items.begin(),ex.items.end(),[](const Item& i) { return i.type == "chip" && i.section != "chips"; }),"Explore lists the mood and genre buttons in their own shelf");
    check(std::any_of(ex.items.begin(),ex.items.end(),[](const Item& i) { return i.type == "album" || i.type == "video" || i.type == "playlist"; }),"Explore keeps its release shelves");
  }
  h.add(music,browse,read(dir,"music-explore.json"),{{"browseId",J(string("FEmusic_explore"))}});
  h.add(music,browse,read(dir,"music-charts.json"),{{"browseId",J(string("FEmusic_charts"))},{"params",J(string("sgYPRkVtdXNpY19leHBsb3Jl"))}});
  auto charts = request("music"); charts["q"] = "charts";
  check(!c.browse(charts).items.empty(),"charts follows the charts endpoint after releases");
  for (int filter = 0; filter < 6; ++filter) {
    Object args{{"query",J(string("jazz instrumental"))}};
    const char* params[] = {"","EgWKAQIIAQ%3D%3D","EgWKAQIYAQ%3D%3D","EgWKAQIgAQ%3D%3D","EgWKAQIQAQ%3D%3D","EgeKAQQoADgBagwQDhAKEAMQBBAJEAU%3D"};
    if (filter) args["params"] = J(string(params[filter]));
    h.add(music,search,read(dir,"music-search.json"),args);
    auto q = request("music_search"); q["q"] = "jazz instrumental"; q["filter"] = std::to_string(filter);
    auto found = c.browse(q);
    check(!found.items.empty(),"search filter contract");
    // F65: o cartão do topo vira o "melhor resultado" só na aba Tudo; as abas com filtro mantêm a ordem do YouTube
    check((filter == 0) == (found.items.front().section == "top"),"top result only on the unfiltered tab");
    if (filter == 0) check(found.items.front().type == "playlist" && found.items.front().id == "VLRDATnXgy" && found.items.front().title.find("jazz") != string::npos,"top result is the card at the head of the results");
  }
  h.add(music,browse,read(dir,"music-album.json"),{{"browseId",J(string("MPREb_GuNCgmKDLZT"))}});
  check(c.browse(request("album","MPREb_GuNCgmKDLZT")).title.find("Jazz") != string::npos,"album metadata");
  h.add(music,browse,read(dir,"music-artist.json"),{{"browseId",J(string("UCMJqedJ0OZgh9mjdRf26Xzg"))}});
  check(!c.browse(request("artist","UCMJqedJ0OZgh9mjdRf26Xzg")).title.empty(),"artist metadata");
  h.add(web,"",read(dir,"web-context.html"));
  h.add(web,browse,read(dir,"channel.json"),{{"browseId",J(cid)}});
  h.add(web,browse,read(dir,"channel-videos.json"),{{"browseId",J(cid)},{"params",J(string("EgZ2aWRlb3PyBgQKAjoA"))}});
  auto q = request("channel",cid); p = c.browse(q);
  check(p.title == "Google for Developers" && !p.banner.empty(),"channel tab preserves header");
  int first = int(p.items.size()); check(first > 0 && !p.continuation.empty(),"first channel page has cursor");
  h.add(web,browse,read(dir,"continuation.json"),{{"continuation",J(p.continuation)}});
  q["offset"] = std::to_string(first); auto more = c.browse(q);
  check(more.items.size() == 2 && more.title == p.title && more.continuation.empty(),"continuation deduplicates and stops repeated token");
  q["offset"] = "999"; check(c.browse(q).error == "continuation_expired","out of order cursor rejected");
  for (const auto& tab : {string("live"),string("playlists")}) {
    J channelData; check(parse(read(dir,"channel.json"),channelData),"channel tab fixture");
    string suffix = tab == "live" ? "/streams" : "/playlists", params;
    const auto* tabs = find(get(channelData,"contents"),"tabs");
    if (tabs) for (const auto& entry : array(*tabs)) {
      const auto& renderer = get(entry,"tabRenderer");
      const auto* metadata = find(get(renderer,"endpoint"),"webCommandMetadata");
      if (!metadata) continue;
      string url = str(*metadata,"url");
      if (url.size() >= suffix.size() && url.substr(url.size()-suffix.size()) == suffix)
        params = str(*find(get(renderer,"endpoint"),"browseEndpoint"),"params");
    }
    check(!params.empty(),"tab endpoint recorded");
    h.add(web,browse,read(dir,"channel.json"),{{"browseId",J(cid)}});
    h.add(web,browse,read(dir,"channel-"+tab+".json"),{{"browseId",J(cid)},{"params",J(params)}});
    auto tabQuery = request("channel",cid); tabQuery["tab"] = tab;
    auto selected = c.browse(tabQuery);
    check(!selected.items.empty() && selected.title == "Google for Developers","tab content and header");
    if (tab == "playlists") check(selected.items.front().type == "playlist","playlist tab card type");
  }
  h.add(web,browse,read(dir,"playlist.json"),{{"browseId",J(string("VLPLF12"))}});
  h.add(web,browse,read(dir,"playlist-more.json"),{{"continuation",J(string("CONT_PLAYLIST"))}});
  string queue = c.handle("/api/nav/play?kind=playlist&id=PLF12&lang=pt");
  check(queue.find("F12audio001") != string::npos && queue.find("F12audio003") != string::npos,"play all includes subsequent page");
  h.add(music,next,read(dir,"radio.json"),{{"videoId",J(string("F12audio001"))},{"playlistId",J(string("RDAMVMF12audio001"))}});
  check(c.handle("/api/nav?kind=radio&id=F12audio001&lang=pt").find("F12audio002") != string::npos,"radio queue");
  h.add(music,next,read(dir,"lyrics-next.json"),{{"videoId",J(string("F12audio001"))}});
  h.add(music,browse,read(dir,"lyrics.json"),{{"browseId",J(string("MPLYfixture"))}});
  check(c.handle("/api/nav/lyrics?id=F12audio001&lang=pt").find("Letra de teste") != string::npos,"lyrics endpoint discovery");
  {  // F65: aba Relacionados do Tocando agora (tab MPTR do `next`), só músicas; sem a aba volta lista vazia
    string nextJson = read(dir,"lyrics-next.json"); auto at = nextJson.find("MPLYfixture"); check(at != string::npos,"fixture lyrics tab");
    nextJson.replace(at,11,"MPTRfixture");
    h.add(music,next,nextJson,{{"videoId",J(string("F12audio001"))}});
    h.add(music,browse,read(dir,"music-album.json"),{{"browseId",J(string("MPTRfixture"))}});
    auto related = c.handle("/api/nav/related?id=F12audio001&lang=pt");
    check(related.rfind("ok\t",0) == 0 && related.find("\nvideo\t") != string::npos,"related tab lists the songs of its page");
    check(related.find("\nalbum\t") == string::npos && related.find("\nartist\t") == string::npos,"related keeps only songs");
    h.add(music,next,read(dir,"lyrics-next.json"),{{"videoId",J(string("F12audio001"))}});
    auto none = c.handle("/api/nav/related?id=F12audio001&lang=pt");
    check(none.rfind("ok\t",0) == 0 && none.find("\nvideo\t") == string::npos,"no related tab gives an empty list");
    check(c.handle("/api/nav/related?id=bad&lang=pt").rfind("error\t",0) == 0,"related refuses a bad video id");
  }
  {  // F65: aba Biblioteca não faz pedido (os atalhos são do cliente); sem conta pede login
    auto library = request("music"); library["q"] = "library";
    check(c.browse(library).error == "signed_out","library tab is account-only and makes no request");
  }
  {  // F65b: respostas reais da TV e da web de música (formato do console), com dados inventados
    h.add(music,search,read(dir,"tv-music-search.json"),{{"query",J(string("banda ficticia"))}});
    auto found = c.browse({{"kind","music_search"},{"q","banda ficticia"},{"lang","pt"}});
    const Item *clip = nullptr, *track = nullptr, *reel = nullptr;
    for (const auto& i : found.items) { if (i.id == "VIDfix00001") clip = &i; if (i.id == "TRKfix00001") track = &i; if (i.id == "SHRTfix0001") reel = &i; }
    check(clip && clip->channel == "Banda Ficticia" && clip->views.find("bi") != string::npos && clip->date == "há 15 anos" && clip->duration == 261,"TV search: plain-text channel row, views and date, duration");
    check(track && track->channel == "Banda Ficticia" && track->views.find("370") != string::npos && track->date.empty(),"TV search: a square track keeps the artist and drops the kind label");
    check(reel && reel->type == "video" && reel->shortForm,"TV search: a Short is a video (contentId), not the channel it taps to");
    check(std::none_of(found.items.begin(),found.items.end(),[](const Item& i) { return i.id.compare(0,2,"UC") == 0; }),"TV search: no Short turns into an artist card");
    h.add(music,browse,read(dir,"music-podcasts.json"),{{"browseId",J(string("FEmusic_non_music_audio"))}});
    auto pods = c.browse({{"kind","chip"},{"id","FEmusic_non_music_audio||Podcasts"},{"lang","pt"}});
    const Item *show = nullptr, *episode = nullptr;
    for (const auto& i : pods.items) { if (i.id == "MPSPPLfixtureshow000001") show = &i; if (i.id == "EPIfix00001") episode = &i; }
    check(show && show->type == "album" && show->title == "Programa Ficticio","podcast show opens as a page (MPSPP), not dropped");
    check(episode && episode->type == "video" && episode->channel == "Programa Ficticio" && episode->duration == -1 && episode->date == "há 1 dia","podcast episode (multi-row item) plays, with the show as author and unknown duration");
  }
  check(!c.setToken("invalid\r\nheader"),"token rejects header injection");
  check(c.setToken("fixture-token"),"token accepted");
  h.add(music,"",read(dir,"music-context.html"));
  h.add(music,browse+"&alt=json",read(dir,"music-home.json"),{{"browseId",J(string("FEmusic_home"))}},true);
  check(!c.browse(request("music")).items.empty(),"account change invalidates cache");
  {  // F65b: com a conta, letra, relacionados e busca vão pela web pública: o `next` da TV não traz as abas e a busca da TV ignora os filtros
    h.add(music,"",read(dir,"music-context.html"));
    h.add(music,next,read(dir,"lyrics-next.json"),{{"videoId",J(string("F12audio001"))}});
    h.add(music,browse,read(dir,"lyrics.json"),{{"browseId",J(string("MPLYfixture"))}});
    check(c.handle("/api/nav/lyrics?id=F12audio001&lang=pt").find("Letra de teste") != string::npos,"signed in: lyrics come from the public next, without the account token");
    string nextJson = read(dir,"lyrics-next.json"); nextJson.replace(nextJson.find("MPLYfixture"),11,"MPTRfixture");
    h.add(music,next,nextJson,{{"videoId",J(string("F12audio001"))}});
    h.add(music,browse,read(dir,"music-album.json"),{{"browseId",J(string("MPTRfixture"))}});
    check(c.handle("/api/nav/related?id=F12audio001&lang=pt").find("\nvideo\t") != string::npos,"signed in: related songs come from the public next");
    h.add(music,search,read(dir,"music-search.json"),{{"query",J(string("jazz instrumental"))},{"params",J(string("EgWKAQIYAQ%3D%3D"))}});
    auto q = request("music_search"); q["q"] = "jazz instrumental"; q["filter"] = "2";
    auto filtered = c.browse(q);
    check(!filtered.items.empty() && filtered.musicSource == "public","signed in: a filtered search is sent with its params to the public search");
  }
  {  // F65b: biblioteca na TV: "Artistas" usa as inscrições (o id da web dá 400 lá) e nunca cai na reserva de convidado
    Http lib; Client tv(lib);
    check(tv.setToken("fixture-token"),"library client token");
    lib.add(music,"",read(dir,"music-context.html"));
    lib.add(music,browse+"&alt=json","{}",{},true,400);
    lib.add(web,"tv",read(dir,"tv-context.html"));
    lib.add(web,browse+"&alt=json",read(dir,"tv-library-artists.json"),{{"browseId",J(string("FEmusic_library_corpus_artists"))}},true);
    auto artists = tv.browse({{"kind","chip"},{"id","FEmusic_library_corpus_track_artists||Artistas"},{"lang","pt"}});
    check(artists.items.size() == 2 && artists.items[0].type == "artist" && artists.musicSource == "tv","library Artistas asks the TV for the subscribed artists and lists them");
    lib.add(web,browse+"&alt=json","{}",{},true,400);
    auto albums = tv.browse({{"kind","chip"},{"id","FEmusic_liked_albums||Albuns"},{"lang","pt"}});
    check(albums.error == "account_music_unavailable" && lib.steps.empty(),"a library route that the TV refuses never falls back to the guest page");
  }
  for (int refusal : {400,401,403}) {
    Http rejected; Client guest(rejected);
    check(guest.setToken("fixture-token"),"refused token is syntactically valid");
    rejected.add(music,"",read(dir,"music-context.html"));
    rejected.add(music,browse+"&alt=json","{}",{},true,refusal);
    rejected.add(web,"tv",read(dir,"tv-context.html"));
    rejected.add(web,browse+"&alt=json","{}",{},true,refusal);
    rejected.add(music,"",read(dir,"music-context.html"));
    rejected.add(music,browse,read(dir,"music-home.json"));
    check(!guest.browse(request("music")).items.empty(),"refused account token retries Music as guest");
    check(guest.authError.find(std::to_string(refusal))!=string::npos,"token refusal remains observable");
    rejected.add(music,search,read(dir,"music-search.json"));  // F65b: a busca de música é sempre pública (a TV ignora os filtros)
    check(!guest.browse({{"kind","music_search"},{"q","jazz"},{"lang","pt"}}).items.empty(),"subsequent Music requests stay signed out");
    check(rejected.steps.empty(),"refusal retry consumes exactly the expected exchanges");
  }
  check(c.setToken(""),"logout clears token");
  h.add(music,"",read(dir,"music-context.html"));
  h.add(music,browse,"{}",{},false,503);
  check(c.browse(request("music")).error == "HTTP 503","provider error exposed");
  check(h.steps.empty(),"every expected exchange consumed");
  std::cout << checks << " checks passed\n";
}
