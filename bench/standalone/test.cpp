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
    std::vector<yt::Request> sent;
    std::vector<unsigned> waits;
    bool backoff(unsigned ms) override { waits.push_back(ms); return true; }
    yt::Response send(const yt::Request& r) override { sent.push_back(r); return request(r.url,r.body,r.form); }
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
    check(!v.channel.empty(),"native player preserves the recorded channel author");
    check(!v.muxed && v.video.itag==137 && v.fallbackVideo.itag==136 && v.audio.itag==140 && v.video.fps==30,"1080p30 with 720p fallback and AAC");
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
    check(v.highFrameRate720 && v.video.itag==137 && v.fallbackVideo.itag==136 && v.fallbackVideo.fps==60,"1080p30 keeps a 720p60 fallback");
    auto tooFast=json;
    size_t full=tooFast.find("\"fps\": 30",tooFast.find("\"itag\": 137"));
    check(full!=string::npos,"1080p mutation target");
    tooFast.replace(full,9,"\"fps\": 60");
    h.add(player,"\"clientName\":\"VISIONOS\"",tooFast);
    v=c.player(id);
    check(v.video.itag==136 && v.video.fps==60,"720p60 beats 480p30; 1080p60 is excluded");
    const string login=R"({"playabilityStatus":{"status":"LOGIN_REQUIRED","reason":"Sign in"},"streamingData":{"hlsManifestUrl":"https://fixture.googlevideo.com/old"}})";
    // A sessão nova vem antes dos outros clientes; o JS da mesma revisão não é baixado outra vez.
    h.add(player,"\"clientName\":\"VISIONOS\"",login);
    h.add(watch,"",session);
    h.add(player,"\"clientName\":\"VISIONOS\"",login);
    h.add(player,"\"clientName\":\"ANDROID_VR\"",login);
    h.add(player,"\"clientName\":\"IOS\"",login);
    v=c.player(id);
    check(v.status=="LOGIN_REQUIRED" && v.hls.empty() && v.video.url.empty(),"challenge stops after one new session and two bounded alternate clients");
    h.add(player,"\"clientName\":\"VISIONOS\"","oops",503);
    h.add(player,"\"clientName\":\"VISIONOS\"","oops",503);
    check(c.player(id).status=="ERROR","player failure");
    check(c.player("bad").status=="INVALID_VIDEO_ID","bad ID locally rejected");
    const string onlyFast=R"({"playabilityStatus":{"status":"OK"},"videoDetails":{"videoId":"4p_9OPKgcDE"},"streamingData":{"adaptiveFormats":[{"itag":298,"mimeType":"video/mp4; codecs=\"avc1\"","width":1280,"height":720,"fps":60,"url":"https://fixture.googlevideo.com/fast"}]}})";
    h.add(player,"\"clientName\":\"VISIONOS\"",onlyFast);
    v=c.player(id);
    check(v.status=="OK" && v.video.itag==298 && v.reason=="no_direct_aac","60 fps qualifies but still requires AAC");
    const string bothRates=R"({"playabilityStatus":{"status":"OK"},"videoDetails":{"videoId":"4p_9OPKgcDE"},"streamingData":{"adaptiveFormats":[{"itag":298,"mimeType":"video/mp4; codecs=\"avc1\"","width":1280,"height":720,"fps":59.94,"url":"https://fixture.googlevideo.com/fast"},{"itag":136,"mimeType":"video/mp4; codecs=\"avc1\"","width":1280,"height":720,"fps":29.97,"url":"https://fixture.googlevideo.com/normal"}]}})";
    h.add(player,"\"clientName\":\"VISIONOS\"",bothRates);
    check(c.player(id).video.itag==136,"720p30 avoids needless 60 fps decoding when both exist");
    auto muxed=json.substr(0,json.find("\"adaptiveFormats\""))+json.substr(json.find("\"formats\":"));
    h.add(player,"\"clientName\":\"VISIONOS\"",muxed);
    v=c.player(id);
    check(v.video.itag==18 && v.muxed && v.audio.url.empty(),"muxed fallback without AAC input");
}
void audioProfiles(Http& h,yt::Client& c) {
    const string reply=R"json({"playabilityStatus":{"status":"OK"},"videoDetails":{"videoId":"4p_9OPKgcDE"},"streamingData":{"adaptiveFormats":[
      {"itag":137,"mimeType":"video/mp4; codecs=\"avc1\"","width":1920,"height":1080,"fps":30,"url":"https://fixture.googlevideo.com/1080"},
      {"itag":298,"mimeType":"video/mp4; codecs=\"avc1\"","width":1280,"height":720,"fps":60,"url":"https://fixture.googlevideo.com/72060"},
      {"itag":136,"mimeType":"video/mp4; codecs=\"avc1\"","width":1280,"height":720,"fps":30,"url":"https://fixture.googlevideo.com/72030"},
      {"itag":135,"mimeType":"video/mp4; codecs=\"avc1\"","width":854,"height":480,"fps":30,"url":"https://fixture.googlevideo.com/48030"},
      {"itag":140,"mimeType":"audio/mp4; codecs=\"mp4a\"","bitrate":128000,"audioTrack":{"id":"pt.4","displayName":"Portuguese (auto-dubbed)","audioIsDefault":true},"url":"https://fixture.googlevideo.com/pt"},
      {"itag":140,"mimeType":"audio/mp4; codecs=\"mp4a\"","bitrate":128000,"audioTrack":{"id":"en.4","displayName":"English (original)","audioIsDefault":false},"url":"https://fixture.googlevideo.com/en"}]}})json";
    for (int mode : {0,1,2}) {
        h.add(player,"\"clientName\":\"VISIONOS\"",reply);
        auto v=c.player(id,720,nullptr,mode);
        check(v.video.itag==(mode==1 ? 298 : 137),"saved playback profile selects 720p60 or 1080p30");
        check(mode!=1 || v.fallbackVideo.itag==136,"720p60 has a distinct 720p30 network fallback");
        check(mode!=1 || (v.lowerVideo.size()==2 && v.lowerVideo[1].itag==135),"720p30 falls to 480p30 before a server");
        check(v.audio.url=="https://fixture.googlevideo.com/en" && v.selectedAudio=="en-original","27f6785: original beats default automatic dub");
        check(v.audioTracks.size()==2 && !v.muxed,"audio catalog has both independent languages");
        check(v.speakerAudio.url==v.audio.url,"native Nest uses original audio in each video profile");
    }
    for (const auto& choice : {string("pt-auto"),string("pt-BR-auto"),string("lang:pt"),string("lang:ja")}) {
        h.add(player,"\"clientName\":\"VISIONOS\"",reply);
        auto v=c.player(id,720,nullptr,1,choice);
        check(v.audio.url==(choice=="lang:ja" ? "https://fixture.googlevideo.com/en" : "https://fixture.googlevideo.com/pt"),"explicit dub or interface language; unavailable language keeps original");
        check(v.speakerAudio.url==v.audio.url,"native Nest follows explicit and interface audio selection");
    }
}
void playerFailures(const string& dir) {
    const auto json=read(dir+"/player.json");
    Http h; yt::Client c(h);
    h.add(watch,"",session); h.add(web+"s/player/fixture/base.js","","signatureTimestamp:20775;");
    h.steps.push_back({player,"VISIONOS",{0,"","Timeout was reached"},false});
    h.add(player,"VISIONOS",json);
    auto v=c.player(id);
    check(v.status=="OK" && v.playerAttempts==2 && h.sent.back().freshConnection && h.waits.size()==1,"transient network retries same client on a fresh connection with backoff");
    h.steps.push_back({player,"VISIONOS",{0,"","tls_peer_verification"},false});
    v=c.player(id);
    check(v.status=="ERROR" && v.failure==yt::Failure::TLS && v.playerAttempts==1,"TLS trust failure never bypasses verification or rotates clients");
    h.add(player,"VISIONOS","bad JSON"); h.add(player,"VISIONOS","bad JSON");
    v=c.player(id);
    check(v.failure==yt::Failure::JSON && v.playerAttempts==2,"JSON retry is bounded and classified");
    h.add(player,"VISIONOS","refused",403);
    v=c.player(id);
    check(v.failure==yt::Failure::HTTP && v.playerAttempts==1,"HTTP 403 is distinct from a YouTube playability refusal");
    const string refusal=R"({"playabilityStatus":{"status":"UNPLAYABLE","reason":"not available on this app"}})";
    h.add(player,"VISIONOS",refusal); h.add(player,"ANDROID_VR",json);
    v=c.player(id);
    check(v.status=="OK" && v.playerClient=="ANDROID_VR" && v.playerAttempts==2 && !v.video.url.empty(),"client refusal rotates to embedded direct-link client");
    check(h.sent.back().agent.find("youtube.vr.oculus")!=string::npos && h.sent.back().headers[0]=="X-Youtube-Client-Name: 28","alternate client carries matching agent and numeric identity");
    h.add(player,"VISIONOS",refusal); h.add(player,"ANDROID_VR",refusal); h.add(player,"IOS",json);
    v=c.player(id);
    check(v.status=="OK" && v.playerClient=="IOS" && v.playerAttempts==3 && h.steps.empty(),"second alternate remains bounded");
    Http sessionNet; yt::Client fresh(sessionNet);
    yt::Response tls{0,"","SSL connect error"}; tls.failure=yt::Failure::TLS; tls.retryable=true;
    sessionNet.steps.push_back({watch,"",tls,false}); sessionNet.steps.push_back({watch,"",tls,false});
    v=fresh.player(id);
    check(v.failure==yt::Failure::TLS && v.status=="ERROR" && sessionNet.sent.back().freshConnection && sessionNet.steps.empty(),"TLS classification survives watch-session errors without relying on error text");
    sessionNet.add(watch,"","denied",403);
    v=fresh.player(id);
    check(v.failure==yt::Failure::HTTP && sessionNet.steps.empty(),"watch HTTP 403 is not retried as a network failure");
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
    h.add(player,"\"clientName\":\"VISIONOS\"",R"({"videoDetails":{"videoId":"4p_9OPKgcDE"},"playabilityStatus":{"status":"OK"},"streamingData":{"hlsManifestUrl":"https://fixture.googlevideo.com/live"},"captions":{"playerCaptionsTracklistRenderer":{"captionTracks":[{"languageCode":"pt","name":{"simpleText":"Português"},"baseUrl":"https://www.youtube.com/api/timedtext?v=example&fmt=srv3"}]}}})");
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
    playback(h,c,argv[1]); audioProfiles(h,c); playerFailures(argv[1]); browse(h,c); captions(h,c); login(h);
    check(h.steps.empty(),"all expected exchanges exercised");
    std::cout << checks << " checks passed\n";
}
