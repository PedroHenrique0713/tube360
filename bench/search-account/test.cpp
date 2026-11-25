#include "yt/catalog.hpp"
#include "playback_mode.hpp"
#include "h264_policy.hpp"
#include <iostream>
using namespace yt::catalog;
using yt::string;
static int checks=0, failures=0;
static void check(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
// Contrato adversarial: WEB pode recusar o grant TV de várias formas; anônimo funciona.
struct Web final:yt::Transport {
  int refusal, requests=0, auth=0; bool badGuest=false, invalid=false, network=false;
  bool allCookieless=true;
  std::vector<yt::Request> seen;
  explicit Web(int status):refusal(status){}
  yt::Response request(const string& u,const string& b={},bool=false) override{return send({u,b});}
  yt::Response send(const yt::Request& r) override {
    bool bearer=false,cookieless=false;
    for(const auto& h:r.headers){bearer|=h.starts_with("Authorization:");cookieless|=h=="Cookie:";}
    allCookieless &= cookieless;seen.push_back(r);
    if(network){yt::Response out;out.error="PRIVATE_TOKEN private@example.test";out.transportCode=60;out.failure=yt::Failure::TLS;out.tlsCode=-9984;return out;}
    if(r.body.empty()) return {200,"ytcfg.set({\"INNERTUBE_CONTEXT\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"fixture\"}}});",{}};
    ++requests; auth+=bearer;
    if(invalid)return {200,"not-json PRIVATE_TOKEN",{}};
    if(bearer || badGuest) return {refusal,"{\"error\":{\"code\":400,\"status\":\"INVALID_ARGUMENT\",\"message\":\"PRIVATE_TOKEN private@example.test\\nforged\"}}",{}};
    J body;check(parse(r.body,body),"request JSON");
    if(get(body,"continuation").is<string>())return {200,"{\"contents\":[{\"videoRenderer\":{\"videoId\":\"next0000001\",\"title\":{\"simpleText\":\"Next\"}}}]}",{}};
    return {200,"{\"contents\":[{\"videoRenderer\":{\"videoId\":\"demo0000001\",\"title\":{\"simpleText\":\"Demo\"}}},{\"continuationCommand\":{\"token\":\"CURSOR_PRIVATE\"}}]}",{}};
  }
};
int main(){
  for(int status:{400,401,403,200})for(bool signedIn:{false,true})for(bool filtered:{false,true}){
    Web http(status);Client c(http);if(signedIn)c.setToken("PRIVATE_TOKEN");
    std::map<string,string> q{{"kind","search"},{"q","private@example.test"},{"lang","pt"}};
    if(filtered)q["params"]=yt::search::Filters{1,1,2,1}.params();
    auto p=c.browse(q);check(p.error.empty() && p.items.size()==1,"search works regardless of WEB bearer response/account/filters");
    check(http.allCookieless,"bootstrap and search suppress cookie jar");
    check(http.auth==0 && http.requests==1,"public search never sends TV bearer or needs retry");
    if(!p.error.empty())continue;
    J sent;parse(http.seen.back().body,sent);
    check(str(sent,"params")==q["params"],"filter bytes are preserved");
    q["offset"]="1";p=c.browse(q);check(p.error.empty() && p.items.size()==1,"anonymous continuation works");
    check(http.auth==0 && http.requests==2,"cursor stays anonymous");
  }
  Web error(200);error.badGuest=true;Client c(error);
  check(c.browse({{"kind","search"},{"q","demo"}}).error=="youtube_unavailable","HTTP 200 error body is provider failure");
  // O formato recortado continua com 800 linhas reais; só o menu reconhece sua classe 1080p.
  Web unused(200);yt::Client media(unused);
  auto video=media.player("R6MlUcmOul8",1080,nullptr,0,"original",R"({"playabilityStatus":{"status":"OK"},"videoDetails":{"videoId":"R6MlUcmOul8"},"streamingData":{"adaptiveFormats":[{"itag":137,"height":800,"width":1920,"fps":24,"mimeType":"video/mp4; codecs=avc1.640028","url":"https://fixture.googlevideo.com/video"},{"itag":140,"mimeType":"audio/mp4; codecs=mp4a.40.2","url":"https://fixture.googlevideo.com/audio"}]}})");
  check(video.video.height==800 && video.video.width==1920 && video.video.fps==24,"decoder keeps real geometry and frame rate");
  auto decode=playback::h264Policy(video.video.height,video.video.fps,false);
  check(decode.threads==4 && !decode.skipLoop,"800-line source keeps four workers and full loop filter");
  check(video.qualityModes & (1u<<playback::DETAIL),"1080p profile is offered for 1920x800 source");
#ifndef F47_BASELINE
  check(playback::labelHeight(1920,800)==1080 && playback::labelHeight(1280,534)==720,"cropped display classes");
  check(playback::labelHeight(640,480)==480 && playback::labelHeight(1080,1920)==1080,"4:3 and portrait display classes");
  check(playback::labelHeight(0,800)==800 && playback::fixedHeight(playback::FIXED480)==480,"unknown width and manual policy remain real");
  for(int status:{400,401,403,200,429,503}) {
    Web h(status);h.badGuest=true;Client client(h);string logs;
    client.setLogger([&](const string& line){logs+=line+"\n";});
    auto reply=client.handle("/api/nav?kind=search&q=private@example.test&params=CURSOR_PRIVATE");
    check(reply.starts_with("error\t"),"provider failure stays a failure");
    check(logs.find("rota=WEB/search http="+std::to_string(status))!=string::npos && logs.find("api_code=400")!=string::npos,"provider failure logs route HTTP and numeric code");
    check(logs.find("PRIVATE_TOKEN")==string::npos && logs.find("private@example.test")==string::npos && logs.find("CURSOR_PRIVATE")==string::npos,"logs omit credentials, PII and query/cursor");
  }
  for(bool network:{false,true}){
    Web h(200);h.invalid=!network;h.network=network;Client client(h);string logs;
    client.setLogger([&](const string& line){logs+=line+"\n";});
    client.handle("/api/nav?kind=search&q=private@example.test");
    check(logs.find(network?"transport=60":"invalid_json")!=string::npos,"malformed JSON and transport error retain reason");
    check(logs.find("PRIVATE_TOKEN")==string::npos && logs.find("private@example.test")==string::npos,"raw transport errors never logged");
  }
#endif
  std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
