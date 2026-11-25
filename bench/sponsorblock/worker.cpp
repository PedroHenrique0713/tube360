#include "sponsorblock_worker.hpp"
#include <cstdio>
#include <stdexcept>

std::atomic_bool first{false}, releaseFirst{false}, shuttingDown{false};
std::atomic_int calls{0};
struct Fixture : yt::Transport {
    const std::atomic_bool* stop;
    explicit Fixture(const std::atomic_bool* flag):stop(flag) {}
    bool cancelled() const override { return stop && *stop; }
    yt::Response request(const std::string&,const std::string&,bool) override {
        int call=++calls;
        if (call==1) { first=true; while (!releaseFirst && !cancelled()) SDL_Delay(1); }
        if (shuttingDown) { while (!cancelled()) SDL_Delay(1); return {0,"","cancelled"}; }
        return {200,call==1 ? R"([{"videoID":"wWC8wVI4E3E","segments":[{"category":"sponsor","segment":[1,9]}]}])" : R"([{"videoID":"ifI_fwg55k8","segments":[{"category":"sponsor","segment":[10,20]}]}])"};
    }
};
namespace yt { std::unique_ptr<Transport> https(const std::atomic_bool* flag) { return std::make_unique<Fixture>(flag); } }
void check(bool ok,const char* m) { if (!ok) throw std::runtime_error(m); }
template<class F> void waitFor(F f) { auto until=SDL_GetTicks()+3000; do { if (f()) return; SDL_Delay(1); } while (SDL_GetTicks()<until); check(false,"worker timeout"); }
int main() {
    SDL_Init(0);
    try {
        sponsorblock::Worker worker; worker.init(); sponsorblock::Playback playback;
        worker.select("wWC8wVI4E3E"); waitFor([]{ return first.load(); });
        check(!worker.poll(playback),"fetch blocked rendering/published before response");
        worker.select("ifI_fwg55k8"); releaseFirst=true;
        waitFor([&]{ worker.poll(playback); return !playback.segments.empty(); });
        check(playback.segments[0].start==10 && calls==2,"previous video's delayed response escaped revision fence");
        playback.reset(); worker.select("ifI_fwg55k8");
        waitFor([&]{ return worker.poll(playback); }); check(calls==2,"same video was not cached");
        worker.select(""); SDL_Delay(20); check(!worker.poll(playback) && calls==2,"off/closed selection generated request");
        shuttingDown=true; worker.select("AAAAAAAAAAA"); waitFor([]{ return calls==3; });
        auto start=SDL_GetTicks(); worker.close(); check(SDL_GetTicks()-start<500,"in-flight fetch did not cancel on shutdown");
        std::puts("PASS worker: nonblocking request, stale video fenced, session cache, off sends nothing, cancellation joins");
    } catch (const std::exception& e) { releaseFirst=true; std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; }
    SDL_Quit();
}
