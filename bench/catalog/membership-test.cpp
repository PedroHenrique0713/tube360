#include "yt/catalog.hpp"
#include "yt/console_account.hpp"
#include "i18n.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>
using namespace yt::catalog;
using yt::string;
int checks = 0;
void check(bool ok, const char* message) { ++checks; if (!ok) { std::cerr << "FAIL " << message << '\n'; std::exit(1); } }
string read(const string& path) { std::ifstream f(path); check(bool(f),"fixture exists"); std::ostringstream s; s << f.rdbuf(); return s.str(); }
struct Http : yt::Transport {
  string page, refusal, language="en"; unsigned players = 0; bool accountPlayer = false;
  yt::Response request(const string& url, const string& body = {}, bool form = false) override {
    yt::Request r; r.url=url; r.body=body; return send(r);
  }
  yt::Response send(const yt::Request& r) override {
    if (r.url == "https://www.youtube.com/") return {200,R"({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB"}}})",{}};
    if (r.url.find("/watch?") != string::npos) return {200,R"({"VISITOR_DATA":"fixture","jsUrl":"/s/player/fixture/base.js"})",{}};
    if (r.url.find("base.js") != string::npos) return {200,"signatureTimestamp:20775",{}};
    if (r.url.find("/player?") != string::npos) {
      if (accountPlayer) {
        check(std::find(r.headers.begin(),r.headers.end(),"Authorization: Bearer fake-a")!=r.headers.end(),"membership player uses existing TV OAuth");
        check(r.body.find("TVHTML5")!=string::npos && r.body.find("Samsung")!=string::npos,"membership player uses TV Samsung identity");
        check(r.body.find("\"hl\":\""+language+"\"")!=string::npos,"membership player retains requested language");
      }
      ++players; return {200,refusal,{}};
    }
    check(r.url.find("/browse?") != string::npos || r.url.find("/search?") != string::npos,"only recorded catalog requests");
    return {200,page,{}};
  }
};
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  string dir=argv[1]; Http h; yt::Client client(h);
  for (auto lang : {"en","pt"}) {
    h.page=read(dir+"/members-"+lang+".json"); J data; check(parse(h.page,data),"real response parses");
    auto p=decode(data,false);
    check(p.items.size()==1 && p.items[0].members==yt::Membership::Only,"modern recorded badge retained");
    check(wire(p).find("\tmembers\n")!=string::npos,"navigation wire retains membership");
    for (auto page : {client.home(),client.search("fixture")}) check(page.cards.size()==1 && page.cards[0].members==yt::Membership::Only,"Home and search retain real badge");
  }
  J cases; check(parse(read(dir+"/members-cases.json"),cases),"synthetic variants parse");
  for (const auto& c : array(get(cases,"cases"))) {
    const auto& node=get(c,"node"); string expected=str(c,"expected");
    check(yt::membershipWire(yt::membership(node))==expected,str(c,"name").c_str());
    J data(Object{{"contents",J(Array{J(Object{{str(c,"renderer"),node}})})}});
    auto p=decode(data,true);
    check(p.items.size()==1 && yt::membershipWire(p.items[0].members)==expected,"all navigation renderers preserve membership");
  }
  h.refusal=read(dir+"/members-player.json");
  auto media=client.player("member00001");
  check(media.status=="UNPLAYABLE" && media.reason=="members" && h.players==1,"real refusal is specific and never rotates clients");
  h.refusal=R"({"playabilityStatus":{"status":"OK","reason":"Members only"},"videoDetails":{"videoId":"member00001","lengthSeconds":"600"},"streamingData":{"formats":[{"itag":22,"width":1280,"height":720,"fps":30,"mimeType":"video/mp4; codecs=\"avc1, mp4a\"","url":"https://fixture.googlevideo.com/fixture"}]}})";
  media=client.player("member00001");
  check(media.status=="OK" && !media.video.url.empty(),"authorized player response follows normal playback");
  check(i18n::playerError("Vídeo só para membros do canal")==i18n::Key::VideoMembers,"player error translates");
  char path[]="/tmp/tube360-members-XXXXXX"; int fd=mkstemp(path); check(fd>=0,"isolated account file"); close(fd);
  { std::ofstream f(path); f << R"({"access_token":"fake-a","refresh_token":"fake-r","expires_at":5000,"client":{"client_id":"fake-client","client_secret":"fake-secret"}})"; }
  yt::ConsoleAccount account(h,path);
  for (const auto& kind : {"home","subs","history","later"}) {
    auto reply=account.request(string("/api/feed?kind=")+kind,1000,10);
    check(reply.ok() && reply.body.find("\tmembers\t\t\t\n")!=string::npos,"TV feeds retain members");
  }
  for (const auto& kind : {"subscriptions","library","history","later"}) {
    auto reply=account.request(string("/api/nav?kind=")+kind,1000,10);
    check(reply.ok() && reply.body.find("\tmembers\n")!=string::npos,"TV navigation retains members");
  }
  h.accountPlayer=true;
  auto authorized=account.request("/api/video/member00001/player",1000,10);
  check(authorized.ok(),"account supplies authenticated player response");
  auto calls=h.players;
  media=client.player("member00001",720,nullptr,0,"original",authorized.body);
  check(media.status=="OK" && media.playerClient=="TVHTML5" && !media.video.url.empty() && h.players==calls,"authorized media reuses format selection without anonymous requests");
  h.language="es";
  check(account.request("/api/video/member00001/player?lang=es",1000,10).ok(),"Spanish authenticated member playback");
  calls=h.players;
  unlink(path);
  yt::ConsoleAccount guest(h,path);
  check(guest.request("/api/video/member00001/player",1000,10).error=="signed_out" && h.players==calls,"signed-out session cannot use member player");
  std::cout << checks << " membership checks passed\n";
}
