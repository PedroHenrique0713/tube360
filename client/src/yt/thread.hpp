#pragma once
#include <SDL3/SDL.h>
#include <cstring>
#include <new>
#ifdef __XSDK__
#include <xsdk/thread.h>
#include <xsdk/log.h>
#endif

namespace yt {
inline void placeThread(const char* name,unsigned hw,int priority) {
#ifdef __XSDK__
    auto moved=xsdk_thread_move(hw);
    auto raised=xsdk_thread_set_priority(XSDK_CURRENT_THREAD,priority,nullptr);
    xsdk_thread_sleep(0);
    xsdk_log_write("app-thread: %s HW %u actual %u priority %d affinity_status %08x priority_status %08x",
        name,hw,xsdk_thread_hardware_thread(),priority,(unsigned)moved,(unsigned)raised);
#else
    (void)name;(void)hw;(void)priority;
#endif
}
struct ThreadRole { unsigned hw; int priority; };
inline ThreadRole threadRole(const char* name) {
    if (!std::strcmp(name,"youtube")) return {1,1}; // demux, AAC e PCM
    if (!std::strcmp(name,"music-aac")) return {2,1};
    if (!std::strcmp(name,"range") || !std::strcmp(name,"hls") || !std::strcmp(name,"playlists")) return {3,1};
    if (!std::strcmp(name,"cast-v2") || !std::strcmp(name,"nest") || !std::strcmp(name,"music-nest")) return {5,1};
    if (!std::strcmp(name,"cast-rpc") || !std::strcmp(name,"cast-post") || !std::strcmp(name,"cast-dial") ||
        !std::strcmp(name,"conta") || !std::strcmp(name,"timer")) return {5,0};
    return {5,-1}; // catálogo, miniaturas, comentários, descoberta e atualizações
}
#ifdef __XSDK__
struct ThreadStart { SDL_ThreadFunction fn; void* data; const char* name; };
inline int startWorker(void* data) {
    auto value=*static_cast<ThreadStart*>(data);delete static_cast<ThreadStart*>(data);
    auto role=threadRole(value.name);placeThread(value.name,role.hw,role.priority);
    return value.fn(value.data);
}
#endif
// O teste da F7 mediu a sessão TLS/JSON do Xenon com 512 KiB de pilha.
inline SDL_Thread* worker(SDL_ThreadFunction fn, const char* name, void* data) {
#ifdef __XSDK__
    auto* start=new(std::nothrow) ThreadStart{fn,data,name};
    if (!start) return nullptr;
    auto props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, reinterpret_cast<void*>(startWorker));
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, start);
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, name);
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 512*1024);
    auto thread = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
    if (!thread) delete start;
    return thread;
#else
    return SDL_CreateThread(fn,name,data);
#endif
}
}
