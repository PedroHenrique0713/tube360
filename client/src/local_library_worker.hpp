#pragma once
#include <SDL3/SDL.h>
#include "local_library.hpp"
#include "yt/thread.hpp"
namespace local {
// Uma vaga pendente: alterações rápidas substituem o snapshot ainda não escrito.
class Writer {
  SDL_Mutex* mu=SDL_CreateMutex(); SDL_Condition* cv=SDL_CreateCondition(); SDL_Thread* thread=nullptr;
  Library pending; bool dirty=false,quit=false; uint64_t done=0; bool success=true;
  std::string path;
  static int run(void* p){static_cast<Writer*>(p)->loop();return 0;}
  void loop(){
    for(;;){
      SDL_LockMutex(mu);
      while(!dirty && !quit)SDL_WaitCondition(cv,mu);
      if(!dirty && quit){SDL_UnlockMutex(mu);return;}
      auto snapshot=std::move(pending); dirty=false; SDL_UnlockMutex(mu);
      bool ok=snapshot.save(path);
      SDL_LockMutex(mu); done=snapshot.revision; success=ok; SDL_UnlockMutex(mu);
    }
  }
public:
  explicit Writer(std::string p):path(std::move(p)){if(mu && cv)thread=yt::worker(run,"local-library",this);}
  // Sinaliza o fim, espera a thread terminar (ela grava a última vaga antes) e só então o mutex e a condição podem morrer.
  // Idempotente: o main chama antes de SDL_Quit; o destrutor chama de novo sem efeito.
  void stop(){
    if(thread){SDL_LockMutex(mu);quit=true;SDL_BroadcastCondition(cv);SDL_UnlockMutex(mu);SDL_WaitThread(thread,nullptr);thread=nullptr;}
  }
  ~Writer(){stop();if(cv){SDL_DestroyCondition(cv);cv=nullptr;}if(mu){SDL_DestroyMutex(mu);mu=nullptr;}}
  Writer(const Writer&)=delete; Writer& operator=(const Writer&)=delete;
  void submit(const Library& library){if(!thread || quit)return;SDL_LockMutex(mu);pending=library;dirty=true;SDL_SignalCondition(cv);SDL_UnlockMutex(mu);}
  bool poll(uint64_t& revision,bool& ok){if(!thread){if(revision)return false;revision=1;ok=false;return true;}SDL_LockMutex(mu);bool changed=done && revision!=done;if(changed){revision=done;ok=success;}SDL_UnlockMutex(mu);return changed;}
};
}
