#include "yt/catalog.hpp"
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace yt::catalog;
using yt::string;
namespace {
int checks=0;
void check(bool ok,const char* message) { ++checks; if (!ok) { std::cerr<<"FAIL "<<message<<'\n'; std::exit(1); } }
string read(const string& dir,const string& name) {
  std::ifstream in(dir+"/"+name); check(bool(in),"fixture exists"); std::ostringstream s; s<<in.rdbuf(); return s.str();
}
const string music="https://music.youtube.com/", web="https://www.youtube.com/";
const string browse="youtubei/v1/browse?prettyPrint=false", next="youtubei/v1/next?prettyPrint=false";
struct Http : yt::Transport {
  struct Step { string url,reply,identity,bid; int status; bool guest; };
  std::deque<Step> steps;
  std::vector<yt::Request> sent;
  string expectedToken="console-fixture-token";
  void add(string url,string reply,string identity={},string bid={},int status=200,bool guest=false) {
    steps.push_back({url,reply,identity,bid,status,guest});
  }
  yt::Response request(const string& url,const string& body,bool form) override { return send({url,body,{},form,{}}); }
  yt::Response send(const yt::Request& r) override {
    check(!steps.empty(),"no unplanned request or retry"); auto s=steps.front(); steps.pop_front(); sent.push_back(r);
    if (r.url!=s.url) std::cerr<<"URL "<<r.url<<" expected "<<s.url<<'\n';
    check(r.url==s.url,"endpoint matches recorded client route");
    auto header=[&](const string& key) { for (auto& h:r.headers) if (h.compare(0,key.size()+1,key+":")==0) return h.substr(key.size()+1); return string{}; };
    if (r.body.empty()) {
      check(header("Authorization").empty(),"bootstrap never sends Bearer");
      if (!s.guest) check(std::find(r.headers.begin(),r.headers.end(),"Cookie:")!=r.headers.end(),"bootstrap suppresses cookie jar");
    } else if (!s.identity.empty()) {
      J data; check(parse(r.body,data),"request JSON"); const auto& client=get(get(data,"context"),"client");
      check(str(client,"clientName")==s.identity,"identity must match endpoint and headers");
      check(str(client,"clientVersion")== (s.identity=="TVHTML5" ? "7.20261123.19.00" : "1.20261122.17.00"),"live bootstrap version used instead of stale fallback");
      check(str(client,"hl")=="pt" && str(client,"gl")=="BR","request locale");
      if (!s.bid.empty()) {
        if (str(data,"browseId")!=s.bid && str(data,"continuation")!=s.bid) std::cerr<<"browse="<<str(data,"browseId")<<" cursor="<<str(data,"continuation")<<" expected="<<s.bid<<'\n';
        check(str(data,"browseId")==s.bid || str(data,"continuation")==s.bid,"browse ID or cursor belongs to selected client");
      }
      check(header("X-Youtube-Client-Name")== (s.identity=="TVHTML5" ? " 7" : " 67"),"numeric client header agrees with context");
      check(header("X-Youtube-Client-Version")==" "+str(client,"clientVersion"),"version header agrees with context");
      check(header("X-Goog-Visitor-Id")== (str(client,"visitorData").empty() ? "" : " "+str(client,"visitorData")),"visitor header agrees with the same bootstrap context");
      check(header("Origin")== (s.identity=="TVHTML5" ? " https://www.youtube.com" : " https://music.youtube.com"),"origin agrees with client");
      check(header("Authorization")==" Bearer "+expectedToken,"current existing TV token reused");
      check(std::find(r.headers.begin(),r.headers.end(),"Cookie:")!=r.headers.end(),"authenticated request has no cookie jar");
      auto now=header("X-Goog-Request-Time");
      check(!now.empty() && std::abs(std::atoll(now.c_str())-std::time(nullptr))<10,"ytmusicapi request timestamp uses seconds");
      check(header("X-Goog-AuthUser").empty(),"cookie account selector is absent for Bearer");
      check(!client.contains("remoteHost") && !client.contains("mainAppWebInfo"),"no desktop session/IP transplanted into Music context");
    } else check(header("Authorization").empty(),"public fallback never sends Bearer");
    return {s.status,s.reply,{}};
  }
};
auto q(string kind="music") { return std::map<string,string>{{"kind",kind},{"lang","pt"}}; }
void webRefused(Http& h,const string& dir,int status=401) {
  h.add(music,read(dir,"music-context.html"));
  h.add(music+browse+"&alt=json",read(dir,"music-invalid-bearer.json"),"WEB_REMIX","FEmusic_home",status);
  h.add(web+"tv",read(dir,"tv-context.html"));
}
}
int main(int argc,char** argv) {
  if (argc!=2) return 2; string dir=argv[1],home=read(dir,"tv-music-public.json");
  J data; check(parse(home,data),"recorded TV music response parses"); auto parsed=decode(data,true);
  check(parsed.items.size()==12 && parsed.items.front().duration==175 && !parsed.items.front().channel.empty(),"actual TV shape preserves cards, duration and artist line");
  check(std::any_of(parsed.items.begin(),parsed.items.end(),[](const Item& i){return i.type=="playlist";}),"TV mixes remain playlists");
  J carousel; check(parse(R"({"contents":{"shelfRenderer":{"content":{"horizontalListRenderer":{"items":[{"tileRenderer":{"contentId":"F35audio001","contentType":"TILE_CONTENT_TYPE_VIDEO","title":{"simpleText":"Demo"}}}],"continuations":[{"nextContinuationData":{"continuation":"CAROUSEL_ONLY"}}]}}}}})",carousel),"carousel fixture parses");
  check(decode(carousel,true).continuation.empty(),"carousel cursor cannot become a page cursor when outer continuation is absent");
  for (int refusal:{400,401,403}) {
    Http h; Client c(h); c.setToken("console-fixture-token"); webRefused(h,dir,refusal);
    h.add(web+browse+"&alt=json",home,"TVHTML5","FEtopics_music");
    auto page=c.browse(q()); check(page.error.empty() && page.items.size()==12 && page.musicSource=="tv" && c.authError.empty(),"WEB refusal recovers account Music through TV, never public");
    auto sent=h.sent.size(); check(c.browse(q()).musicSource=="tv" && h.sent.size()==sent,"account page cached under selected backend");
    h.add(web+browse+"&alt=json",read(dir,"playlist.json"),"TVHTML5","VLLM");
    auto liked=q("music_playlist"); liked["id"]="VLLM"; check(!c.browse(liked).items.empty(),"liked Music uses VLLM with TV token");
    h.add(web+browse+"&alt=json",home,"TVHTML5","FEmusic_last_played"); check(!c.browse(q("music_recent")).items.empty(),"recent Music uses TV music history, not general history");
    h.add(web+browse+"&alt=json",home,"TVHTML5","FEplaylist_aggregation"); check(!c.browse(q("music_library")).items.empty(),"account playlists through TV");
    h.add(web+next+"&alt=json",read(dir,"radio.json"),"TVHTML5"); auto radio=q("radio"); radio["id"]="F12audio001";
    check(c.browse(radio).items.size()==3,"radio returns account queue through TV");
    c.setToken(""); h.add(music,read(dir,"music-context.html"),{},{},200,true); h.add(music+browse,read(dir,"music-home.json"));
    check(c.browse(q()).musicSource=="public","logout clears private context and cache");
    check(c.browse(q("music_recent")).error=="signed_out" && h.steps.empty(),"signed-out history cannot become public data");
  }
  for (int status:{401,403}) {
    Http h; Client c(h); c.setToken("console-fixture-token"); webRefused(h,dir);
    h.add(web+browse+"&alt=json","{}","TVHTML5","FEtopics_music",status);
    h.add(music,read(dir,"music-context.html"),{},{},200,true); h.add(music+browse,read(dir,"music-home.json"));
    check(c.browse(q()).musicSource=="public" && c.authError=="TVHTML5 browse HTTP "+std::to_string(status),"both clients refused: labeled public fallback");
    c.setToken("other-fake-token"); check(c.musicSource.empty() && c.authError.empty(),"refresh clears backend rejection state");
    h.expectedToken="other-fake-token"; h.add(music,read(dir,"music-context.html"));
    h.add(music+browse+"&alt=json",read(dir,"music-home.json"),"WEB_REMIX","FEmusic_home");
    check(c.browse(q()).musicSource=="web" && h.steps.empty(),"renewed token reboots context and retries account before cached public data");
  }
  // O cursor é sintético sobre o formato público gravado; nunca é de uma conta real.
  Object root=data.get<Object>(); root["continuationContents"]=J(Object{{"sectionListContinuation",J(Object{{"continuations",J(Array{J(Object{{"nextContinuationData",J(Object{{"continuation",J(string("TV_PRIVATE_CURSOR"))}})}})})}})}});
  Http cursor; Client c(cursor); c.setToken("console-fixture-token"); webRefused(cursor,dir);
  cursor.add(web+browse+"&alt=json",J(root).serialize(),"TVHTML5","FEtopics_music"); auto first=c.browse(q());
  auto more=q(); more["offset"]=std::to_string(first.items.size()); cursor.add(web+browse+"&alt=json","{}","TVHTML5","TV_PRIVATE_CURSOR",401);
  check(c.browse(more).error=="account_music_unavailable" && cursor.steps.empty(),"private cursor never sent to public or another client");
  for (int status:{429,500,503}) {
    Http h; Client transient(h); transient.setToken("console-fixture-token"); h.add(music,read(dir,"music-context.html"));
    h.add(music+browse+"&alt=json","{}","WEB_REMIX","FEmusic_home",status);
    check(transient.browse(q()).error=="HTTP "+std::to_string(status) && transient.authError.empty() && h.steps.empty(),"transient errors do not downgrade account");
    Http tv; Client transientTV(tv); transientTV.setToken("console-fixture-token"); webRefused(tv,dir);
    tv.add(web+browse+"&alt=json","{}","TVHTML5","FEtopics_music",status);
    check(transientTV.browse(q()).error=="HTTP "+std::to_string(status) && tv.steps.empty(),"TV transient errors never trigger public fallback");
  }
  Http webOnly; Client w(webOnly); w.setToken("console-fixture-token"); webOnly.add(music,read(dir,"music-context.html"));
  webOnly.add(music+browse+"&alt=json",read(dir,"music-home.json"),"WEB_REMIX","FEmusic_home");
  check(w.browse(q()).musicSource=="web" && webOnly.steps.empty(),"accepted WEB_REMIX remains supported");
  for (const auto& responseContext:{Object{{"loggedOut",J(true)}},Object{{"serviceTrackingParams",J(Array{J(Object{{"service",J(string("GFEEDBACK"))},{"params",J(Array{J(Object{{"key",J(string("logged_in"))},{"value",J(string("0"))}})})}})})}}}) {
    auto publicData=data.get<Object>(); publicData["responseContext"]=J(responseContext);
    Http h; Client ignored(h); ignored.setToken("console-fixture-token"); webRefused(h,dir);
    h.add(web+browse+"&alt=json",J(publicData).serialize(),"TVHTML5","FEtopics_music");
    h.add(music,read(dir,"music-context.html"),{},{},200,true); h.add(music+browse,read(dir,"music-home.json"));
    check(ignored.browse(q()).musicSource=="public" && ignored.authError=="TVHTML5 browse signed_out" && h.steps.empty(),"HTTP 200 explicitly signed out never labeled account Music");
  }
  for (auto bad:{string("bad\r\nheader"),string("bad token"),string("bad\0token",9),string(8193,'x')}) check(!w.setToken(bad),"unsafe token rejected");
  for (auto bad:{string("<html>unsupported</html>"),string("{\"INNERTUBE_CONTEXT\":{}}"),string(R"({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB_REMIX","clientVersion":"valid","visitorData":"bad\r\nvisitor"}}})")}) {
    Http h; Client malformed(h); malformed.setToken("console-fixture-token"); h.add(music,bad);
    check(malformed.browse(q()).error=="invalid_context" && h.steps.empty(),"bad bootstrap fails before sending token");
  }
  std::cout<<checks<<" OAuth Music checks passed\n";
}
