#pragma once
#include "app.hpp"
#include "rules.hpp"
#include "yt/client.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <memory>
#include <mutex>

namespace updates {
enum class State { IDLE, CHECKING, AVAILABLE, DOWNLOADING, READY, ERROR, CHECK_FAILED };
struct Status {State state=State::IDLE; Manifest app; int progress=0; string error;};
class Service {
    string dir,base,manifestBytes,manifestSig;
    std::shared_ptr<RuleStore> store;
    std::unique_ptr<yt::Transport> net;
    SDL_Thread* thread=nullptr;
    std::mutex mu;
    Status state;
    std::atomic_bool stop{false},download{false},abortDownload{false},home{true},healthy{false},refresh{false};
    std::atomic<unsigned> revision{0};
    static int run(void*);
    void work();
    yt::Response fetch(const string&,size_t limit=maxDocument);
    void check();
    void install();
    void publish(Status);
    void event(const string&);
public:
    explicit Service(string directory);
    ~Service();
    void tick(bool onHome,bool bootHealthy) {home=onHome;if(bootHealthy)healthy=true;}
    unsigned poll(Status&,unsigned previous);
    void requestDownload() {abortDownload=false;download=true;}
    void cancelDownload() {abortDownload=true;}
    void requestCheck() {refresh=true;}
};
}
