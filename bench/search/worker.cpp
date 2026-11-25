#include "search_worker.hpp"
#include <cstdio>
#include <cstdlib>
#include <mutex>
static std::atomic_int checks{0};
static void check(bool ok,const char* what) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL %s\n",what); std::exit(1); } }
struct Slow : yt::Transport {
    std::atomic_int calls{0}; std::atomic_bool entered{false}, release{false};
    std::mutex mutex; std::vector<uint64_t> starts;
    yt::Response request(const std::string&,const std::string& = {},bool=false) override { return {}; }
    yt::Response send(const yt::Request& r) override {
        check(r.maxBytes==32768 && r.timeoutMs==2500 && r.connectTimeoutMs==1500,"bounded request");
        check(r.body.empty() && r.headers[0]=="Cookie:","anonymous GET");
        int call=++calls;
        { std::lock_guard<std::mutex> lock(mutex); starts.push_back(SDL_GetTicks()); }
        if (call==1) { entered=true; while (!release) SDL_Delay(1); }
        std::string q=r.url.substr(r.url.find("&q=")+3);
        // Deliberately ignore transport cancellation: generation must reject late data too.
        return {200,"[\""+q+"\",[[\""+q+" result\",0]]]",{}};
    }
};
template<class F> static void until(F f,const char* what) { auto start=SDL_GetTicks(); while (!f() && SDL_GetTicks()-start<3000) SDL_Delay(2); check(f(),what); }
int main() {
    SDL_Init(0); search::Worker worker; auto slow=std::make_unique<Slow>(); auto* http=slow.get(); worker.init(std::move(slow)); check(worker.thread,"worker started");
    for (auto text:{"a","ab","abc","abcd"}) { worker.select(text,"en"); SDL_Delay(35); }
    check(http->calls==0,"typing burst is debounced");
    until([&]{return http->entered.load();},"slow request entered");
    auto begin=SDL_GetPerformanceCounter();
    for (int i=0;i<5000;++i) { worker.select(i%2 ? "fresh" : "latest","en"); std::vector<std::string> result; check(!worker.poll(result),"network never blocks poll"); }
    double elapsed=1000.*(SDL_GetPerformanceCounter()-begin)/SDL_GetPerformanceFrequency();
    check(elapsed<250,"5000 edits stay responsive during blocked transport");
    worker.select("newest","pt"); http->release=true;
    std::vector<std::string> result;
    until([&]{worker.poll(result); return !result.empty();},"latest query completes");
    check(result==std::vector<std::string>{"newest result"},"late response is discarded");
    check(http->calls==2,"burst keeps only final query");
    { std::lock_guard<std::mutex> lock(http->mutex); check(http->starts[1]-http->starts[0]>=search::Worker::intervalMs,"requests are spaced"); }
    worker.select("cancel","en"); worker.select("","en"); SDL_Delay(400);
    check(http->calls==2 && !worker.poll(result),"leaving keyboard cancels pending work");
    worker.close();
    std::printf("%d worker checks passed; 5000 edits %.3f ms (%.3f us/edit)\n",checks.load(),elapsed,elapsed*1000/5000); SDL_Quit();
}
