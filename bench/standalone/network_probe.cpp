// O mesmo curl/mbedTLS do produto; a bancada local liga um roots.hpp exclusivo de teste.
#include "yt/media.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>

struct Run {
    yt::string url;
    int64_t size=0, chunk=0;
    int lanes=1, buffer=65536;
    bool fresh=false;
    std::atomic<int64_t> cursor{0}, bytes{0};
    std::atomic<int> requests{0}, connections{0}, failed{0}, receive{0}, send{0};
    std::atomic<uint64_t> first{0};
    uint64_t began=0;
    static int worker(void* p) {
        auto& run=*static_cast<Run*>(p);
        auto net=yt::https();
        for (;;) {
            int64_t first=run.cursor.fetch_add(run.chunk);
            if (first>=run.size) break;
            int64_t last=std::min(first+run.chunk,run.size)-1;
            yt::Request q; q.url=run.url; q.media=true; q.readBuffer=run.buffer; q.freshConnection=run.fresh;
            q.headers={"Range: bytes="+std::to_string(first)+"-"+std::to_string(last)};
            auto r=net->send(q);
            run.requests++; run.connections+=r.newConnections;
            run.receive=r.receiveBuffer; run.send=r.sendBuffer;
            if (!r.error.empty() || r.status!=206 || int64_t(r.body.size())!=last-first+1) {
                run.failed++; std::fprintf(stderr,"transfer %ld %s\n",r.status,r.error.c_str()); break;
            }
            run.bytes+=r.body.size();
            uint64_t zero=0; run.first.compare_exchange_strong(zero,SDL_GetTicks()-run.began);
        }
        return 0;
    }
};
int main(int argc,char** argv) {
    if (argc<3) return 2;
    SDL_Init(0);
    Run run; run.url=argv[1]; run.size=std::atoll(argv[2]); run.began=SDL_GetTicks();
    if (argc==4 && yt::string(argv[3])=="cancel") {
        struct Transfer { yt::Transport* net; yt::string url; yt::Response reply; } transfer;
        auto net=yt::https(); transfer.net=net.get(); transfer.url=run.url;
        auto* thread=yt::worker([](void* data) {
            auto& t=*static_cast<Transfer*>(data); yt::Request q; q.media=true; q.url=t.url;
            q.headers={"Range: bytes=0-524287"}; t.reply=t.net->send(q); return 0;
        },"cancel-probe",&transfer);
        if (!thread) return 5;
        SDL_Delay(300); uint64_t began=SDL_GetTicks(); net->cancel(); SDL_WaitThread(thread,nullptr);
        uint64_t ms=SDL_GetTicks()-began;
        std::printf("{\"mode\":\"cancel\",\"ms\":%llu,\"cancelled\":%d}\n",(unsigned long long)ms,int(transfer.reply.failure==yt::Failure::CANCELLED));
        return ms<2000 && transfer.reply.failure==yt::Failure::CANCELLED ? 0 : 6;
    }
    if (argc==4 && yt::string(argv[3])=="read") {
        SDL_AtomicInt bytes{};
        yt::RangeFile file(yt::https(),run.url,&bytes,yt::https());
        uint8_t out[16384]; int n; int64_t offset=0;
        while ((n=file.read(out,sizeof(out)))>0) {
            for (int i=0;i<n;++i) if (out[i]!=uint8_t((offset+i)%251)) return 3;
            offset+=n;
        }
        std::printf("{\"mode\":\"read\",\"bytes\":%lld,\"ms\":%llu,\"requests\":%u,\"resumes\":%u,\"connections\":%u,\"result\":%d}\n",
            (long long)offset,(unsigned long long)(SDL_GetTicks()-run.began),file.requests.load(),file.resumes.load(),file.connections.load(),n);
        return offset==run.size && n==AVERROR_EOF ? 0 : 4;
    }
    if (argc!=7) return 2;
    run.chunk=std::atoll(argv[3]); run.lanes=std::clamp(std::atoi(argv[4]),1,2);
    run.buffer=std::atoi(argv[5]); run.fresh=std::atoi(argv[6]);
    if (run.size<=0 || run.chunk<=0) return 2;
    SDL_Thread* threads[2]{};
    for (int i=0;i<run.lanes;++i) threads[i]=yt::worker(Run::worker,"net-bench",&run);
    for (int i=0;i<run.lanes;++i) if (threads[i]) SDL_WaitThread(threads[i],nullptr); else return 5;
    uint64_t ms=SDL_GetTicks()-run.began;
    std::printf("{\"chunk\":%lld,\"lanes\":%d,\"buffer\":%d,\"fresh\":%d,\"bytes\":%lld,\"ms\":%llu,\"first_ms\":%llu,\"mbps\":%.3f,\"requests\":%d,\"connections\":%d,\"tcp_receive\":%d,\"tcp_send\":%d}\n",
        (long long)run.chunk,run.lanes,run.buffer,run.fresh,(long long)run.bytes.load(),(unsigned long long)ms,
        (unsigned long long)run.first.load(),ms ? run.bytes.load()*.008/ms : 0,run.requests.load(),run.connections.load(),run.receive.load(),run.send.load());
    return run.failed || run.bytes!=run.size ? 1 : 0;
}
