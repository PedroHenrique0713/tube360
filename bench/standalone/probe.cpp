#include "yt/client.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <ctime>
#ifdef YT_XBOX
#include <xsdk/log.h>
#define REPORT xsdk_log_write
#else
#define REPORT(...) do { std::printf(__VA_ARGS__); std::puts(""); } while (0)
#endif

namespace {
struct ProbeTransport : yt::Transport {
    std::unique_ptr<yt::Transport> net=yt::https();
    unsigned count=0;
    yt::Response request(const yt::string& url,const yt::string& body,bool form) override {
        if (count>=3) return {0,{},"request_limit"};
        if (count++) { timespec delay{4,0}; nanosleep(&delay,nullptr); }
        auto start=std::chrono::steady_clock::now();
        auto r=net->request(url,body,form);
        auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        REPORT("request %u HTTP %ld bytes %u elapsed_ms %ld",count,r.status,(unsigned)r.body.size(),(long)ms);
        return r;
    }
};
void* run(void* result) {
    ProbeTransport transport;
    yt::Client client(transport);
    auto p=client.search("ge tv");
    REPORT("search cards %u status %s",(unsigned)p.cards.size(),p.error.empty()?"OK":p.error.c_str());
    if (p.cards.empty()) return nullptr;
    auto v=client.player("4p_9OPKgcDE");
    REPORT("player status %s height %d fps %.2f audio_itag %d muxed %d hls %d ttl_s %d source %s",
        v.status.c_str(),v.video.height,v.video.fps,v.audio.itag,v.muxed,!v.hls.empty(),v.ttl, v.status=="OK" && !v.reason.empty() ? v.reason.c_str() : "-");
    *static_cast<int*>(result)=(v.status=="OK" && !v.video.url.empty() && (v.muxed || !v.audio.url.empty())) ? 0 : 2;
    return nullptr;
}
}
int main(int argc,char** argv) {
#ifndef YT_XBOX
    if (argc!=2 || std::strcmp(argv[1],"--live")) {
        std::puts("yt-check --live (three anonymous requests at most)");
        return 1;
    }
#endif
    pthread_t worker;
    int result=1;
    pthread_attr_t attr;
    if (pthread_attr_init(&attr)!=0) return 1;
    int rc=pthread_attr_setstacksize(&attr,512*1024);
    if (rc==0) rc=pthread_create(&worker,&attr,run,&result);
    pthread_attr_destroy(&attr);
    if (rc!=0) return 1;
    pthread_join(worker,nullptr);
    return result;
}
