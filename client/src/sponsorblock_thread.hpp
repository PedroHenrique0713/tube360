#pragma once
#include <cstdio>
#ifdef __XSDK__
#include <xsdk/thread.h>
#include <xsdk/log.h>
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
    static constexpr unsigned hardware=1;
    static constexpr int priority=1;
    int effective=-1;
    unsigned moveStatus=0xffffffffu, priorityStatus=0xffffffffu;
    int previous=0;
    void place() {
#ifdef __XSDK__
        // HW0 desenha; HW2/4/3/5 são os ajudantes H.264. HW1 hospeda resolução/demux, fora desses ajudantes.
        moveStatus=(unsigned)xsdk_thread_move(hardware);
        xsdk_thread_sleep(0);
        effective=(int)xsdk_thread_hardware_thread();
#endif
    }
    void enter() {
#ifdef __XSDK__
        priorityStatus=(unsigned)xsdk_thread_set_priority(XSDK_CURRENT_THREAD,priority,&previous);
        effective=(int)xsdk_thread_hardware_thread();
#endif
    }
    void leave() {
#ifdef __XSDK__
        if (NT_SUCCESS(priorityStatus)) xsdk_thread_set_priority(XSDK_CURRENT_THREAD,previous,nullptr);
#endif
    }
};
}
