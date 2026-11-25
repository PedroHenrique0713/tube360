#pragma once
#include "yt/search.hpp"
#include "yt/thread.hpp"

namespace search {
struct Worker {
    static constexpr uint64_t debounceMs=300, intervalMs=750;
    SDL_Mutex* mu=SDL_CreateMutex(); SDL_Condition* cv=SDL_CreateCondition(); SDL_Thread* thread=nullptr;
    std::atomic_bool stop{false}, interrupted{false}, ready{false};
    uint64_t revision=0, changedAt=0;
    std::string query, language;
    bool pending=false;
    std::vector<std::string> reply;
    std::unique_ptr<yt::Transport> transport;
    void init(std::unique_ptr<yt::Transport> test={}) {
        transport=std::move(test);
        if (mu && cv) thread=yt::worker([](void* p) { static_cast<Worker*>(p)->run(); return 0; },"suggestions",this);
    }
    void select(const std::string& text,const std::string& lang) {
        SDL_LockMutex(mu);
        if (text!=query || lang!=language) {
            ++revision; interrupted=true; ready=false; reply.clear(); query=text; language=lang;
            pending=!text.empty() && text.size()<=256; changedAt=SDL_GetTicks(); SDL_SignalCondition(cv);
        }
        SDL_UnlockMutex(mu);
    }
    bool poll(std::vector<std::string>& out) {
        if (!ready.load()) return false;
        SDL_LockMutex(mu);
        bool changed=ready.exchange(false);
        if (changed) out=std::move(reply);
        SDL_UnlockMutex(mu); return changed;
    }
    void run() {
        if (!transport) transport=yt::https(&interrupted);
        uint64_t last=0;
        while (!stop) {
            SDL_LockMutex(mu);
            while (!stop) {
                if (!pending) { SDL_WaitCondition(cv,mu); continue; }
                auto now=SDL_GetTicks(), due=std::max(changedAt+debounceMs,last ? last+intervalMs : uint64_t(0));
                if (now>=due) break;
                SDL_WaitConditionTimeout(cv,mu,int(due-now));
            }
            if (stop) { SDL_UnlockMutex(mu); break; }
            auto rev=revision; auto text=query; auto lang=language;
            pending=false; interrupted=false; last=SDL_GetTicks(); SDL_UnlockMutex(mu);
            auto result=yt::search::fetch(*transport,text,lang);
            SDL_LockMutex(mu);
            if (!stop && rev==revision) { reply=std::move(result); ready=true; }
            SDL_UnlockMutex(mu);
        }
    }
    void close() {
        if (!mu) return;
        SDL_LockMutex(mu); stop=true; interrupted=true; SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
        if (thread) SDL_WaitThread(thread,nullptr);
        transport.reset(); SDL_DestroyCondition(cv); SDL_DestroyMutex(mu); thread=nullptr; mu=nullptr; cv=nullptr;
    }
    ~Worker() { close(); }
};
}
