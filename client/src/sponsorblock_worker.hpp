#pragma once
#include "sponsorblock.hpp"
#include "yt/thread.hpp"
#include "sponsorblock_retry.hpp"
#include "sponsorblock_thread.hpp"

namespace sponsorblock {
struct Worker {
    SDL_Mutex* mu=SDL_CreateMutex(); SDL_Condition* cv=SDL_CreateCondition();
    SDL_Thread* thread=nullptr;
    std::atomic_bool stop{false};
    std::atomic_bool interrupted{false};
    uint64_t revision=0;
    uint64_t selectedAt=0;
    RetryPlan plan;
    std::string id;
    bool pending=false, ready=false;
    yt::sponsorblock::Segments reply;
    yt::sponsorblock::Diagnostic replyDiagnostic, diagnostic;
    void init() { thread=yt::worker([](void* p) { static_cast<Worker*>(p)->run(); return 0; },"SponsorBlock",this); }
    void select(const std::string& video) {
        SDL_LockMutex(mu);
        ++revision; interrupted=true; id=video; selectedAt=SDL_GetTicks(); plan={};
        pending=yt::sponsorblock::validVideo(video); ready=false; reply.clear();
        SDL_SignalCondition(cv); SDL_UnlockMutex(mu);
    }
    bool poll(Playback& playback, bool playing=false, uint64_t now=SDL_GetTicks()) {
        SDL_LockMutex(mu);
        bool changedPlayback=playing!=plan.playing;
        plan.playback(playing,now);
        if (changedPlayback) SDL_SignalCondition(cv);
        bool changed=ready;
        if (ready) { playback.segments=std::move(reply); diagnostic=std::move(replyDiagnostic); ready=false; }
        SDL_UnlockMutex(mu); return changed;
    }
    void run() {
        ThreadPolicy policy; policy.place();
        auto net=yt::https(&interrupted); yt::sponsorblock::Client client(*net);
        while (!stop) {
            SDL_LockMutex(mu);
            while (!stop && !pending && !plan.due(SDL_GetTicks())) SDL_WaitConditionTimeout(cv,mu,50);
            if (stop) { SDL_UnlockMutex(mu); break; }
            auto video=id; auto rev=revision; auto selected=selectedAt;
            bool refresh=!pending; pending=false; interrupted=false;
            plan.begin(); auto attempt=plan.attempt;
            SDL_UnlockMutex(mu);
            policy.enter();
            uint64_t begin=SDL_GetTicks();
            report("SponsorBlock: tentativa %u/2 inicio rev=%llu prefix=%s fila_ms=%llu connect_ms=%ld total_ms=%ld hw_alvo=%u hw_efetivo=%d afinidade=0x%08x prioridade=%d prioridade_status=0x%08x",
                attempt,(unsigned long long)rev,yt::sponsorblock::hashPrefix(video).c_str(),(unsigned long long)(begin-selected),
                yt::sponsorblock::connectTimeoutMs,yt::sponsorblock::requestTimeoutMs,ThreadPolicy::hardware,policy.effective,policy.moveStatus,ThreadPolicy::priority,policy.priorityStatus);
            auto result=client.fetch(video,begin,refresh);
            auto info=client.diagnostic(); auto ended=SDL_GetTicks();
            policy.leave();
            report("SponsorBlock: tentativa %u/2 fim rev=%llu parede_ms=%llu %s",attempt,(unsigned long long)rev,(unsigned long long)(ended-begin),info.line().c_str());
            SDL_LockMutex(mu);
            bool retry=false;
            if (revision==rev && !stop) {
                reply=std::move(result); replyDiagnostic=info; ready=true;
                retry=plan.finish(info,ended);
            }
            SDL_UnlockMutex(mu);
            if (retry) report("SponsorBlock: tentativa 2/2 aguardando rev=%llu reproducao_estavel_ms=%llu intervalo_ms=%llu",
                (unsigned long long)rev,(unsigned long long)RetryPlan::settleMs,(unsigned long long)RetryPlan::delayMs);
        }
    }
    void close() {
        if (!mu) return;
        SDL_LockMutex(mu); stop=true; interrupted=true; SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
        if (thread) SDL_WaitThread(thread,nullptr);
        SDL_DestroyCondition(cv); SDL_DestroyMutex(mu); thread=nullptr; mu=nullptr; cv=nullptr;
    }
    ~Worker() { close(); }
};
}
