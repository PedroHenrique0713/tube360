#include "yt/catalog_replay.hpp"
#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
extern "C" int __real_avcodec_receive_frame(AVCodecContext*,AVFrame*);
extern "C" int __wrap_avcodec_receive_frame(AVCodecContext* c,AVFrame* f) {
  int result=__real_avcodec_receive_frame(c,f);
  if(result>=0 && c->codec_type==AVMEDIA_TYPE_VIDEO && std::getenv("F47_SLOW_DECODE"))SDL_Delay(60);
  return result;
}
class Fixture final:public yt::Transport {
  yt::catalog::Replay replay;
public:
  explicit Fixture(const char* path):replay(path){}
  yt::Response request(const yt::string& u,const yt::string& b={},bool form=false) override{return send({u,b,{},form});}
  yt::Response send(const yt::Request& r) override {
    using namespace yt::catalog;
    J body;parse(r.body,body);
    auto identity=str(get(get(body,"context"),"client"),"clientName");
    bool bearer=false,cookieless=false;
    for(const auto& h:r.headers){bearer|=h.starts_with("Authorization:");cookieless|=h=="Cookie:";}
    bool search=identity=="WEB" && r.url.find("/search?")!=yt::string::npos;
    if(const char* path=std::getenv("F47_AUDIT"))if(FILE* f=std::fopen(path,"a")){
      std::fprintf(f,"client=%s search=%d bearer=%d cookieless=%d params=%d cursor=%d\n",identity.c_str(),search,bearer,cookieless,body.is<Object>() && body.contains("params"),body.is<Object>() && body.contains("continuation"));std::fclose(f);
    }
    if(search && (bearer || std::getenv("F47_FAIL_SEARCH"))) {
      int status=std::getenv("F47_WEB_STATUS") ? std::atoi(std::getenv("F47_WEB_STATUS")) : 400;
      return {status,R"({"error":{"code":400,"status":"INVALID_ARGUMENT","message":"PRIVATE_TOKEN private@example.test"}})",{}};
    }
    if(std::getenv("F47_SLOW_DECODE") && r.url.find("/player?")!=yt::string::npos) {
      auto file=r;file.url="https://fixture.googlevideo.com/pressure-player";return replay.send(file);
    }
    return replay.send(r);
  }
};
std::unique_ptr<yt::Transport> replayHttps(const std::atomic_bool*) asm("__wrap__ZN2yt5httpsEPKSt6atomicIbE");
std::unique_ptr<yt::Transport> replayHttps(const std::atomic_bool*) {
  const char* path=std::getenv("F47_REPLAY");return std::make_unique<Fixture>(path ? path : "");
}
