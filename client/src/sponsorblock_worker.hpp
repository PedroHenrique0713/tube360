#pragma once
#include "sponsorblock.hpp"
#include "yt/thread.hpp"

namespace sponsorblock {
struct Worker {
    SDL_Mutex* mu=SDL_CreateMutex(); SDL_Condition* cv=SDL_CreateCondition();
    SDL_Thread* thread=nullptr;
    std::atomic_bool stop{false};
    uint64_t revision=0;
    std::string id;
    bool pending=false, ready=false;
    yt::sponsorblock::Segments reply;
    void init() { thread=yt::worker([](void* p) { static_cast<Worker*>(p)->run(); return 0; },"SponsorBlock",this); }
    void select(const std::string& video) {
        SDL_LockMutex(mu);
        ++revision; id=video; pending=yt::sponsorblock::validVideo(video); ready=false; reply.clear();
        SDL_SignalCondition(cv); SDL_UnlockMutex(mu);
    }
    bool poll(Playback& playback) {
        SDL_LockMutex(mu);
        bool changed=ready;
        if (ready) { playback.segments=std::move(reply); ready=false; }
        SDL_UnlockMutex(mu); return changed;
    }
    void run() {
        auto net=yt::https(&stop); yt::sponsorblock::Client client(*net);
        while (!stop) {
            SDL_LockMutex(mu);
            while (!stop && !pending) SDL_WaitConditionTimeout(cv,mu,100);
            if (stop) { SDL_UnlockMutex(mu); break; }
            auto video=id; auto rev=revision; pending=false;
            SDL_UnlockMutex(mu);
            auto result=client.fetch(video,SDL_GetTicks());
            SDL_LockMutex(mu);
            if (revision==rev && !stop) { reply=std::move(result); ready=true; }
            SDL_UnlockMutex(mu);
        }
    }
    void close() {
        if (!mu) return;
        stop=true; SDL_LockMutex(mu); SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
        if (thread) SDL_WaitThread(thread,nullptr);
        SDL_DestroyCondition(cv); SDL_DestroyMutex(mu); thread=nullptr; mu=nullptr; cv=nullptr;
    }
    ~Worker() { close(); }
};
}
