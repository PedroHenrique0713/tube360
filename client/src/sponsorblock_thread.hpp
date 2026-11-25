#pragma once
#include <cstdio>
#ifdef __XSDK__
#include <xsdk/thread.h>
#include <xsdk/log.h>
#include <pthread.h>
#endif

namespace sponsorblock {
template<class... Args> void report(const char* format, Args... args) {
#ifdef __XSDK__
    xsdk_log_write(format,args...);
#else
    std::printf(format,args...); std::puts(""); std::fflush(stdout);
#endif
}
struct ThreadPolicy {
    static constexpr unsigned hardware=5;
    static constexpr int priority=3;
    int effective=-1;
    unsigned moveStatus=0xffffffffu, priorityStatus=0xffffffffu;
    unsigned restoreStatus=0xffffffffu;
    int effectivePriority=-999, restoredPriority=-999, queryStatus=-1, restoredQuery=-1;
    int previous=0;
    void place() {
#ifdef __XSDK__
        // Todos os HW são ocupados. HW5 evita apresentação/HW0 e áudio/HW1;
        // o pedido curto precisa de prioridade acima de H.264 e dos outros jobs neste HW.
        moveStatus=(unsigned)xsdk_thread_move(hardware);
        xsdk_thread_sleep(0);
        effective=(int)xsdk_thread_hardware_thread();
#endif
    }
    void query() {
#ifdef __XSDK__
        int scheduling=0; sched_param value{};
        queryStatus=pthread_getschedparam(pthread_self(),&scheduling,&value);
        effectivePriority=queryStatus ? -999 : value.sched_priority;
#endif
    }
    void enter() {
#ifdef __XSDK__
        priorityStatus=(unsigned)xsdk_thread_set_priority(XSDK_CURRENT_THREAD,priority,&previous);
        effective=(int)xsdk_thread_hardware_thread();
        query();
#endif
    }
    void leave() {
#ifdef __XSDK__
        if (NT_SUCCESS(priorityStatus)) restoreStatus=(unsigned)xsdk_thread_set_priority(XSDK_CURRENT_THREAD,previous,nullptr);
        query(); restoredPriority=effectivePriority; restoredQuery=queryStatus;
#endif
    }
};
}
