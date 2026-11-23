#include "yt/client.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
        yt::Request r; r.url=url; r.body=body; r.form=form;
        return send(r);
    }
    // Sessão de visitante + player JS + player: até 6 pedidos espaçados no total.
    yt::Response send(const yt::Request& q) override {
        if (count>=6) return {0,{},"request_limit"};
        if (count++) { timespec delay{2,0}; nanosleep(&delay,nullptr); }
        auto start=std::chrono::steady_clock::now();
        auto r=net->send(q);
        auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        REPORT("request %u HTTP %ld bytes %u elapsed_ms %ld",count,r.status,(unsigned)r.body.size(),(long)ms);
        return r;
    }
};
int media(yt::Client& client) {
    yt::MediaOffers offers;
    auto vod = client.player("4p_9OPKgcDE",720,&offers);
    REPORT("vod status %s reason %s ttl_s %d video_itag %d audio_itag %d",vod.status.c_str(),vod.reason.c_str(),vod.ttl,vod.video.itag,vod.audio.itag);
#ifndef YT_XBOX
    // Só no PC: os links resolvidos vão para um arquivo fora do repositório, para o ffmpeg conferir.
    if (const char* path=std::getenv("YT_URLS")) if (FILE* f=std::fopen(path,"w")) {
        std::fprintf(f,"%s\n%s\n",vod.video.url.c_str(),vod.audio.url.c_str());
        std::fclose(f);
    }
#endif
    auto report = [&](int height,const yt::OfferCount& video) {
        REPORT("vod %dp30 avc_listed %u avc_direct %u muxed_aac_direct %u",
            height,video.listed,video.direct,video.muxedAac);
        return video.muxedAac || (video.direct && offers.aac.direct);
    };
    bool high = report(720,offers.avc720);
    bool low = report(480,offers.avc480);
    REPORT("vod aac_listed %u aac_direct %u",offers.aac.listed,offers.aac.direct);
    auto live = client.player("HvZt-nh9sGg");
    REPORT("live status %s hls %d ttl_s %d",live.status.c_str(),!live.hls.empty(),live.ttl);
#ifndef YT_XBOX
    if (const char* path=std::getenv("YT_URLS")) if (FILE* f=std::fopen(path,"a")) {
        std::fprintf(f,"%s\n",live.hls.c_str());
        std::fclose(f);
    }
#endif
    return high && low && !live.hls.empty() ? 0 : 2;
}
struct Check { bool media; int result=1; };
void* run(void* arg) {
    auto& check=*static_cast<Check*>(arg);
    ProbeTransport transport;
    yt::Client client(transport);
    if (check.media) { check.result=media(client); return nullptr; }
    auto p=client.search("ge tv");
    REPORT("search cards %u status %s",(unsigned)p.cards.size(),p.error.empty()?"OK":p.error.c_str());
    if (p.cards.empty()) return nullptr;
    auto v=client.player("4p_9OPKgcDE");
    REPORT("player status %s height %d fps %.2f audio_itag %d muxed %d hls %d ttl_s %d source %s",
        v.status.c_str(),v.video.height,v.video.fps,v.audio.itag,v.muxed,!v.hls.empty(),v.ttl, v.status=="OK" && !v.reason.empty() ? v.reason.c_str() : "-");
    check.result=(v.status=="OK" && !v.video.url.empty() && (v.muxed || !v.audio.url.empty())) ? 0 : 2;
    return nullptr;
}
}
int main(int argc,char** argv) {
    bool inspectMedia = argc==2 && std::strcmp(argv[1],"--media")==0;
#ifdef YT_XBOX
    if (argc<=1) inspectMedia=true;
#endif
    if (!inspectMedia && !(argc==2 && std::strcmp(argv[1],"--live")==0)) {
        REPORT("yt-check --media (VOD and live) | --live (search and VOD)");
        return 1;
    }
    pthread_t worker;
    Check check{inspectMedia};
    pthread_attr_t attr;
    if (pthread_attr_init(&attr)!=0) return 1;
    int rc=pthread_attr_setstacksize(&attr,512*1024);
    if (rc==0) rc=pthread_create(&worker,&attr,run,&check);
    pthread_attr_destroy(&attr);
    if (rc!=0) return 1;
    pthread_join(worker,nullptr);
    return check.result;
}
