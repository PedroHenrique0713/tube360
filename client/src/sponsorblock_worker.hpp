#pragma once
#include "sponsorblock.hpp"
#include "yt/thread.hpp"
#include "sponsorblock_retry.hpp"
#include "sponsorblock_thread.hpp"
#include <array>

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
        plan.selectedAt=selectedAt;
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
            int activePriority=policy.effectivePriority, activeQuery=policy.queryStatus;
            struct Event { const char* phase; double ms; int priority, status; };
            std::array<Event,16> events{}; size_t count=0;
            client.trace=[&](const char* phase,double ms) {
                policy.query();
                if (count<events.size()) events[count++]={phase,ms,policy.effectivePriority,policy.queryStatus};
            };
            auto result=client.fetch(video,begin,refresh);
            auto info=client.diagnostic(); auto ended=SDL_GetTicks();
            SDL_LockMutex(mu);
            bool retry=false;
            if (revision==rev && !stop) {
                reply=std::move(result); replyDiagnostic=info; ready=true;
                retry=plan.finish(info,ended);
            }
            SDL_UnlockMutex(mu);
            policy.leave();
            // O logger usa uma seção crítica global. Capturar fases na RAM e publicar
            // antes do log evita esperar por disco/outra thread dentro do handshake.
            report("SponsorBlock: tentativa %u/2 inicio rev=%llu prefix=%s fila_ms=%llu connect_ms=%ld total_ms=%ld hw_alvo=%u hw_efetivo=%d afinidade=0x%08x prioridade=%d prioridade_status=0x%08x log_adiado=1",
                attempt,(unsigned long long)rev,info.prefix.c_str(),(unsigned long long)(begin-selected),
                yt::sponsorblock::connectTimeoutMs,yt::sponsorblock::requestTimeoutMs,ThreadPolicy::hardware,policy.effective,policy.moveStatus,ThreadPolicy::priority,policy.priorityStatus);
            report("SponsorBlock: tentativa %u/2 prioridade rev=%llu efetiva=%d consulta_status=%d anterior=%d restaurada=%d restaurar_status=0x%08x restaurada_consulta=%d",
                attempt,(unsigned long long)rev,activePriority,activeQuery,policy.previous,policy.restoredPriority,policy.restoreStatus,policy.restoredQuery);
            for (size_t i=0;i<count;++i) report("SponsorBlock: tentativa %u/2 fase=%s rev=%llu elapsed_ms=%.1f prioridade_efetiva=%d consulta_status=%d",
                attempt,events[i].phase,(unsigned long long)rev,events[i].ms,events[i].priority,events[i].status);
            // O xsdk limita cada linha a 512 bytes; fases separadas preservam o motivo da falha.
            const auto& t=info.timing;
            report("SponsorBlock: tentativa %u/2 fases rev=%llu phase=%s dns_ms=%.1f tcp_ms=%.1f tls_ms=%.1f first_byte_ms=%.1f callback_gap_ms=%.1f nonblocking=%d os_errno=%ld",
                attempt,(unsigned long long)rev,t.phase,t.dnsMs,t.tcpMs,t.tlsMs,t.firstByteMs,t.callbackGapMs,t.nonblocking,t.osError);
            report("SponsorBlock: tentativa %u/2 prazo rev=%llu desde_escolha_ms=%llu meta_ms=2000 dentro_prazo=%d dados_ok=%d",
                attempt,(unsigned long long)rev,(unsigned long long)(ended-selected),ended-selected<=2000,
                info.state==yt::sponsorblock::State::SEGMENTS || info.state==yt::sponsorblock::State::EMPTY);
            report("SponsorBlock: tentativa %u/2 fim rev=%llu parede_ms=%llu %s",attempt,(unsigned long long)rev,(unsigned long long)(ended-begin),info.line().c_str());
            if (retry) report("SponsorBlock: tentativa 2/2 aguardando rev=%llu desde_escolha_ms=%llu intervalo_minimo_ms=%llu independente_primeiro_quadro=1",
                (unsigned long long)rev,(unsigned long long)RetryPlan::initialRetryMs,(unsigned long long)RetryPlan::delayMs);
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
