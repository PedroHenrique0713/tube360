#include "yt/client.hpp"
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
using yt::string;
int checks = 0;
void check(bool ok,const char* message) {
    if (!ok) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
    ++checks;
}
struct Http : yt::Transport {
    struct Step { string url, body; yt::Response res; bool form; };
    std::deque<Step> steps;
    void add(string url,string expected,string reply,long status=200,bool form=false) {
        steps.push_back({url,expected,{status,reply,{}},form});
    }
    yt::Response request(const string& url,const string& body,bool form) override {
        check(!steps.empty(),"unexpected network request");
        auto step=steps.front(); steps.pop_front();
        check(url==step.url,"endpoint");
        check(step.form==form,"content type");
        check(step.body.empty()?body.empty():body.find(step.body)!=string::npos,"request payload");
        return step.res;
    }
};
const string web="https://www.youtube.com/";
const string api=web+"youtubei/v1/";
const string id="4p_9OPKgcDE";
const string watch=web+"watch?v="+id+"&bpctr=9999999999&has_verified=1";
const string player=api+"player?prettyPrint=false";
const string session=R"(<script>ytcfg.set({"VISITOR_DATA":"Cgtmaxture","jsUrl":"\/s\/player\/fixture\/base.js"})</script>)";
string read(const string& path) {
    std::ifstream f(path);
    check(bool(f),"fixture exists");
    std::ostringstream out; out<<f.rdbuf(); return out.str();
}
void playback(Http& h,yt::Client& c,const string& dir) {
    auto json=read(dir+"/player.json");
    // Primeira reprodução: sessão de visitante (página + player JS) e o pedido VISIONOS com o signatureTimestamp.
    h.add(watch,"",session);
    h.add(web+"s/player/fixture/base.js","","var a={signatureTimestamp:20775};");
    h.add(player,"\"signatureTimestamp\":20775",json);
    yt::MediaOffers offers;
    auto v=c.player(id,720,&offers);
    check(offers.avc720.direct==1 && offers.avc480.direct==1 && offers.aac.direct==2,"recorded direct offers");
    check(v.status=="OK" && v.id==id && !v.title.empty() && v.ttl==21540,"recorded metadata");
    check(!v.muxed && v.video.itag==136 && v.audio.itag==140 && v.video.fps==30,"recorded AVC 720p and AAC");
    auto metadataOnly=json;
    for (size_t i=0; (i=metadataOnly.find("\"url\"",i))!=string::npos; ++i)
        metadataOnly.replace(i,5,"\"unused\"");
    h.add(player,"\"clientName\":\"VISIONOS\"",metadataOnly);
    v=c.player(id,480,&offers);
    check(offers.avc720.listed==1 && offers.avc480.listed==1 && offers.aac.listed==2 &&
        !offers.avc720.direct && !offers.avc480.direct && !offers.aac.direct && v.video.url.empty(),
        "listed formats without URLs do not qualify");
    h.add(player,"\"videoId\":\"4p_9OPKgcDE\"",json);
    check(c.player(id,480).video.itag==135,"480p ceiling");
    size_t pos=json.find("\"itag\": 136");
    const string fps="\"fps\": 30";
    size_t f=json.find(fps,pos);
    check(f!=string::npos,"720p mutation target");
    json.replace(f,fps.size(),"\"fps\": 60");
    h.add(player,"\"clientName\":\"VISIONOS\"",json);
    v=c.player(id);
    check(v.highFrameRate720 && v.video.itag==135 && v.video.fps==30,"record 720p60 falling back to 480p30");
    const string login=R"({"playabilityStatus":{"status":"LOGIN_REQUIRED","reason":"Sign in"},"streamingData":{"hlsManifestUrl":"https://media.example/old"}})";
    // Sessão recusada: abre uma nova (o player JS é o mesmo, sem baixar de novo) e tenta uma única vez mais.
    h.add(player,"\"clientName\":\"VISIONOS\"",login);
    h.add(watch,"",session);
    h.add(player,"\"clientName\":\"VISIONOS\"",login);
    v=c.player(id);
    check(v.status=="LOGIN_REQUIRED" && v.hls.empty() && v.video.url.empty(),"challenge stops playback after one new session");
    h.add(player,"\"clientName\":\"VISIONOS\"","oops",503);
    check(c.player(id).status=="ERROR","player failure");
    check(c.player("bad").status=="INVALID_VIDEO_ID","bad ID locally rejected");
    const string onlyFast=R"({"playabilityStatus":{"status":"OK"},"videoDetails":{"videoId":"4p_9OPKgcDE"},"streamingData":{"adaptiveFormats":[{"itag":298,"mimeType":"video/mp4; codecs=\"avc1\"","width":1280,"height":720,"fps":60,"url":"https://media.example/fast"}]}})";
    h.add(player,"\"clientName\":\"VISIONOS\"",onlyFast);
    v=c.player(id);
    check(v.status=="OK" && v.video.url.empty() && v.reason=="no_direct_avc","60 fps only is unavailable");
    auto muxed=json.substr(0,json.find("\"adaptiveFormats\""))+json.substr(json.find("\"formats\":"));
    h.add(player,"\"clientName\":\"VISIONOS\"",muxed);
    v=c.player(id);
    check(v.video.itag==18 && v.muxed && v.audio.url.empty(),"muxed fallback without AAC input");
}
void browse(Http& h,yt::Client& c) {
    h.add(web,"",R"(<script>ytcfg.set({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"fixture"}}})</script>)");
    const string list=R"({"contents":{"rows":[{"videoRenderer":{"videoId":"4p_9OPKgcDE","title":{"runs":[{"text":"ge tv"}]}}},{"compactVideoRenderer":{"videoId":"4p_9OPKgcDE","title":{"simpleText":"duplicate"}}},{"lockupViewModel":{"contentId":"another","contentType":"LOCKUP_CONTENT_TYPE_VIDEO","metadata":{"lockupMetadataViewModel":{"title":{"content":"Second"}}}}}]}})";
    h.add(api+"search?prettyPrint=false","\"query\":\"ge \\\"tv\\\"\"",list);
    auto p=c.search("ge \"tv\"");
    check(p.error.empty() && p.cards.size()==2 && p.cards[0].title=="ge tv","search order and deduplication");
    h.add(api+"browse?prettyPrint=false","\"browseId\":\"FEwhat_to_watch\"",list);
    check(c.home().cards.size()==2,"home context reused");
    h.add(api+"next?prettyPrint=false","\"videoId\":\"4p_9OPKgcDE\"",list);
    p=c.next(id);
    check(p.cards.size()==1 && p.cards[0].title=="Second","next excludes current video");
    h.add(api+"search?prettyPrint=false","\"query\"",string(82,'{')+string(82,'}'));
    check(!c.search("x").error.empty(),"depth limit");
    h.add(api+"search?prettyPrint=false","\"query\"","oops",503);
    check(c.search("x").error=="HTTP 503","HTTP failure");
}
void captions(Http& h,yt::Client& c) {
    const string base=web+"api/timedtext?v=example";
    h.add(player,"\"clientName\":\"VISIONOS\"",R"({"videoDetails":{"videoId":"4p_9OPKgcDE"},"playabilityStatus":{"status":"OK"},"streamingData":{"hlsManifestUrl":"https://media.example/live"},"captions":{"playerCaptionsTracklistRenderer":{"captionTracks":[{"languageCode":"pt","name":{"simpleText":"Português"},"baseUrl":"https://www.youtube.com/api/timedtext?v=example&fmt=srv3"}]}}})");
    auto v=c.player(id);
    check(!v.hls.empty() && v.captions.size()==1 && v.captions[0].language=="pt","manifest and caption tracks");
    h.add(base+"&fmt=json3","","{\"events\":[{\"tStartMs\":120,\"dDurationMs\":700,\"segs\":[{\"utf8\":\"olá\"},{\"utf8\":\" mundo\"}]}]}");
    std::vector<yt::Cue> cues;
    string error;
    check(c.captions(v.captions[0],cues,error) && cues.size()==1 && cues[0].text=="olá mundo" &&
        cues[0].startMs==120 && cues[0].durationMs==700,"timed text");
    check(!c.captions({"http://other",{},{}},cues,error) && cues.empty(),"track failure clears cues");
}
void login(Http& h) {
    const string origin="https://oauth2.googleapis.com/";
    yt::DeviceLogin auth(h,"local client","local secret");
    yt::DeviceCode d;
    yt::Tokens t;
    h.add(origin+"device/code","client_id=local%20client",R"({"device_code":"fixture","user_code":"TEST","verification_url":"https://www.google.com/device","interval":5,"expires_in":90})",200,true);
    check(auth.start(10,d).empty() && d.expires==100 && d.user=="TEST","device grant");
    check(auth.poll(12,d,t)=="authorization_pending","early poll stays local");
    h.add(origin+"token","device_code=fixture",R"({"error":"authorization_pending"})",428,true);
    check(auth.poll(15,d,t)=="authorization_pending","consent pending");
    h.add(origin+"token","client_secret=local%20secret",R"({"error":"slow_down"})",403,true);
    check(auth.poll(20,d,t)=="slow_down" && d.nextPoll==30,"provider backoff");
    check(auth.poll(29,d,t)=="authorization_pending","backoff enforced");
    h.add(origin+"token","grant_type=urn%3Aietf",R"({"access_token":"fake-a","refresh_token":"fake-r","token_type":"Bearer","expires_in":30})",200,true);
    check(auth.poll(30,d,t).empty() && d.device.empty() && t.expires==60,"authorized exchange");
    h.add(origin+"token","refresh_token=fake-r",R"({"access_token":"fake-b","token_type":"Bearer","expires_in":45})",200,true);
    check(auth.refresh(50,t).empty() && t.refresh=="fake-r" && t.access=="fake-b" && t.expires==95,"refresh preserves token");
    h.add(origin+"token","grant_type=refresh_token",R"({"error":"invalid_grant"})",400,true);
    check(auth.refresh(90,t)=="invalid_grant" && t.access.empty() && t.refresh.empty(),"revoked grant cleared");
    d.device="fixture"; d.expires=10;
    check(auth.poll(10,d,t)=="expired_token" && d.device.empty(),"expired grant");
}
}
int main(int argc,char** argv) {
    if (argc==3 && string(argv[1])=="--untrusted") {
        auto res=yt::https()->request(argv[2]);
        check(res.status==0 && res.body.empty() && res.error=="tls_peer_verification","untrusted TLS issuer");
        return 0;
    }
    if (argc!=2) return 1;
    std::atomic_bool cancel{true};
    auto net=yt::https(&cancel);
    check(net->request("https://localhost/").error=="cancelled","cancel before DNS");
    cancel.store(false);
    auto plain=net->request("http://127.0.0.1:0/");
    check(plain.status==0 && !plain.error.empty(),"reject cleartext HTTP");
    Http h; yt::Client c(h);
    playback(h,c,argv[1]); browse(h,c); captions(h,c); login(h);
    check(h.steps.empty(),"all expected exchanges exercised");
    std::cout << checks << " checks passed\n";
}
