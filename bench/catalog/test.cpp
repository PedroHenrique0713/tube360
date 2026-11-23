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
  h.add(music,browse,read(dir,"music-explore.json"),{{"browseId",J(string("FEmusic_explore"))}});
  h.add(music,browse,read(dir,"music-charts.json"),{{"browseId",J(string("FEmusic_charts"))},{"params",J(string("sgYPRkVtdXNpY19leHBsb3Jl"))}});
  auto charts = request("music"); charts["q"] = "charts";
  check(!c.browse(charts).items.empty(),"charts follows the charts endpoint after releases");
  for (int filter = 0; filter < 4; ++filter) {
    Object args{{"query",J(string("jazz instrumental"))}};
    const char* params[] = {"","EgWKAQIIAQ%3D%3D","EgWKAQIYAQ%3D%3D","EgWKAQIgAQ%3D%3D"};
    if (filter) args["params"] = J(string(params[filter]));
    h.add(music,search,read(dir,"music-search.json"),args);
    auto q = request("music_search"); q["q"] = "jazz instrumental"; q["filter"] = std::to_string(filter);
    check(!c.browse(q).items.empty(),"search filter contract");
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
  check(!c.setToken("invalid\r\nheader"),"token rejects header injection");
  check(c.setToken("fixture-token"),"token accepted");
  h.add(music,"",read(dir,"music-context.html"));
  h.add(music,browse,read(dir,"music-home.json"),{{"browseId",J(string("FEmusic_home"))}},true);
  check(!c.browse(request("music")).items.empty(),"account change invalidates cache");
  for (int refusal : {401,403}) {
    Http rejected; Client guest(rejected);
    check(guest.setToken("fixture-token"),"refused token is syntactically valid");
    rejected.add(music,"",read(dir,"music-context.html"));
    rejected.add(music,browse,"{}",{},true,refusal);
    rejected.add(music,browse,read(dir,"music-home.json"));
    check(!guest.browse(request("music")).items.empty(),"refused account token retries Music as guest");
    check(guest.authError.find(std::to_string(refusal))!=string::npos,"token refusal remains observable");
    rejected.add(music,search,read(dir,"music-search.json"));
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
