#include "sponsorblock_worker.hpp"
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
std::atomic_int calls{0}, scenario{0};
std::atomic_bool releaseFirst{false}, loadStop{false};
std::atomic_uint64_t secondAt{0}, work{0};
std::atomic_int checks{0};
void check(bool ok,const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
yt::sponsorblock::Diagnostic eof() {
    yt::sponsorblock::Diagnostic d; d.state=yt::sponsorblock::State::TLS; d.transport=35; d.tls=-0x7280;
    return d;
}
struct Fixture : yt::Transport {
    const std::atomic_bool* flag;
    explicit Fixture(const std::atomic_bool* cancel):flag(cancel) {}
    bool cancelled() const override { return flag && flag->load(); }
    yt::Response request(const std::string&,const std::string&,bool) override { throw std::runtime_error("request lost deadline fields"); }
    yt::Response send(const yt::Request& r) override {
        check(r.connectTimeoutMs==2500 && r.timeoutMs==3500,"SponsorBlock request has short connect and total budgets");
        int n=++calls;
        if (n==1) while (!releaseFirst && !cancelled()) SDL_Delay(1);
        if (cancelled()) return {0,{},"cancelled"};
        if (n==2) { secondAt=SDL_GetTicks(); check(r.freshConnection,"retry uses a fresh connection and bypasses negative cache"); }
        if (n==1 || scenario==1) {
            yt::Response failed; failed.failure=yt::Failure::TLS; failed.transportCode=35; failed.tlsCode=-0x7280;
            failed.error="SSL connect error"; failed.transportDetail="ssl_handshake returned: (-0x7280) SSL - The connection indicated an EOF";
            return failed;
        }
        return {200,R"([{"videoID":"wWC8wVI4E3E","segments":[{"category":"sponsor","segment":[7.774863,20.877296],"videoDuration":666.681}]}])"};
    }
};
template<class F> void waitFor(F f, uint64_t timeout=4500) {
    uint64_t end=SDL_GetTicks()+timeout;
    do { if (f()) return; SDL_Delay(1); } while (SDL_GetTicks()<end);
    check(false,"worker timed out");
}
void pump(sponsorblock::Worker& worker,sponsorblock::Playback& state,bool playing,uint64_t ms) {
    uint64_t end=SDL_GetTicks()+ms;
    while (SDL_GetTicks()<end) { worker.poll(state,playing); SDL_Delay(1); }
}
void policy() {
    sponsorblock::RetryPlan p; p.begin(); check(p.finish(eof(),100),"EOF is retried once");
    check(!p.due(2999) && p.due(3000),"retry is due three seconds from selection even before playback");
    p.playback(true,4000);
    check(p.due(4000),"first frame does not postpone an already due retry");
    p.playback(false,7000); check(p.due(100000),"buffering does not suppress recovery");
    p.playback(true,8000); check(p.due(8000),"resume does not restart the retry clock");
    p.begin(); check(!p.finish(eof(),11000) && !p.due(100000),"second failure cannot schedule a third attempt");
    sponsorblock::RetryPlan gap; gap.playback(true,1); gap.begin(); gap.finish(eof(),10000);
    check(!gap.due(10249) && gap.due(10250),"an overrun retries after 250 ms, not first frame plus three seconds");
    for (auto kind : {yt::sponsorblock::State::EMPTY,yt::sponsorblock::State::PARSE,yt::sponsorblock::State::CANCELLED}) {
        sponsorblock::RetryPlan one; one.begin(); auto d=eof(); d.state=kind;
        check(!one.finish(d,100),"empty, parse and cancellation do not retry");
    }
    sponsorblock::RetryPlan certificate; certificate.begin(); auto d=eof(); d.transport=60;
    check(!certificate.finish(d,100),"untrusted certificate is not retried");
    sponsorblock::RetryPlan cached; cached.begin(); d=eof(); d.cached=true;
    check(!cached.finish(d,100),"cached failure does not start a new retry loop");
}
void reset(int mode) { calls=0; scenario=mode; releaseFirst=false; secondAt=0; }
void integration() {
    reset(0);
    sponsorblock::Worker worker; worker.init(); sponsorblock::Playback state;
    auto selected=SDL_GetTicks(); worker.select("wWC8wVI4E3E");
    check(SDL_GetTicks()-selected<50,"selection does not wait for transport");
    waitFor([]{ return calls==1; });
    std::vector<std::thread> load;
    loadStop=false;
    for (int i=0;i<2;++i) load.emplace_back([] {
        uint64_t n=1;
        while (!loadStop) { for (int i=0;i<10000;++i) n=n*6364136223846793005ULL+1; work.fetch_add(n); }
    });
    std::atomic_bool cancel{false}, leadDone{false};
    std::thread lead([&] { worker.beforeMedia(cancel); leadDone=true; });
    uint64_t ticks=0, end=SDL_GetTicks()+250;
    while (SDL_GetTicks()<end) { worker.poll(state,false); ++ticks; SDL_Delay(1); }
    loadStop=true; for (auto& t : load) t.join();
    check(ticks>20 && calls==1 && state.segments.empty(),"UI keeps ticking under simulated decode load and a blocked handshake");
    check(!leadDone,"media gate is still held while request is unresolved");
    waitFor([&]{ return leadDone.load(); }); lead.join();
    check(SDL_GetTicks()-selected>=750 && SDL_GetTicks()-selected<1000 && calls==1,"media gate releases at 750 ms while request is still held");
    releaseFirst=true;
    waitFor([&]{ return worker.poll(state,false); });
    check(worker.diagnostic.state==yt::sponsorblock::State::TLS && worker.diagnostic.tls==-0x7280,"first EOF is published, not hidden as empty");
    waitFor([&]{ worker.poll(state,false); return !state.segments.empty(); });
    check(calls==2 && secondAt>=selected+3000 && secondAt<selected+3300,"retry completes before any first frame, at selection plus three seconds");
    check(!worker.diagnostic.cached && state.segments[0].end==20.877296,"retry fetched real data instead of negative cache");
    worker.close();

    reset(1); releaseFirst=true;
    sponsorblock::Worker limited; limited.init(); state.reset(); limited.select("wWC8wVI4E3E");
    waitFor([&]{ return limited.poll(state,true); });
    waitFor([&]{ limited.poll(state,true); return calls==2; });
    pump(limited,state,true,3300);
    check(calls==2,"continuous TLS failure sends exactly two requests");
    limited.close();

    reset(1); releaseFirst=true;
    sponsorblock::Worker cancelled; cancelled.init(); cancelled.select("wWC8wVI4E3E");
    waitFor([&]{ return cancelled.poll(state,true); });
    cancelled.select(""); pump(cancelled,state,true,3300);
    check(calls==1,"closing video cancels the pending retry");
    cancelled.close();
    std::printf("PASS scheduling: %llu UI ticks under two CPU burners, bounded media gate, delayed EOF -> retry before first frame, two-attempt cap, close fencing\n",(unsigned long long)ticks);
}
}
namespace yt { std::unique_ptr<Transport> https(const std::atomic_bool* flag) { return std::make_unique<Fixture>(flag); } }
int main() {
    SDL_Init(0);
    try { policy(); integration(); std::printf("PASS %d retry checks\n",checks.load()); }
    catch (const std::exception& e) { releaseFirst=true; loadStop=true; std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; }
    SDL_Quit();
}
