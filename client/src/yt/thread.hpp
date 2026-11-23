#pragma once
#include <SDL3/SDL.h>

namespace yt {
// O teste da F7 mediu a sessão TLS/JSON do Xenon com 512 KiB de pilha.
inline SDL_Thread* worker(SDL_ThreadFunction fn, const char* name, void* data) {
#ifdef __XSDK__
    auto props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, reinterpret_cast<void*>(fn));
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, data);
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, name);
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 512*1024);
    auto thread = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
    return thread;
#else
    return SDL_CreateThread(fn,name,data);
#endif
}
}
