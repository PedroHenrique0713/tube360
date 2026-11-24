#pragma once
#include <SDL3/SDL.h>
#include "yt/player_extras.hpp"
#include "yt/thread.hpp"

// Um pedido de página em voo; a revisão impede publicar dados do vídeo anterior.
struct PlayerExtras {
  struct Reply { uint64_t revision=0; bool comments=false; yt::extras::Comments page; yt::extras::Votes votes; };
  SDL_Mutex* mu=SDL_CreateMutex(); SDL_Condition* cv=SDL_CreateCondition();
  SDL_Thread* thread=nullptr;
  std::atomic_bool stop{false};
  uint64_t revision=0;
  std::string id, language="en", continuation;
  bool wantVotes=false, wantComments=false, ready=false;
  Reply reply;
  bool loading=false, loaded=false;
  yt::extras::Comments page;
  yt::extras::Votes votes;
  std::string likesLabel, dislikesLabel;
  int selected=0, line=0;
  void init() { thread=yt::worker([](void* p) { static_cast<PlayerExtras*>(p)->run(); return 0; },"comments",this); }
  void select(const std::string& video, const std::string& lang) {
    SDL_LockMutex(mu);
    ++revision; id=video; language=lang; wantVotes=!id.empty(); wantComments=ready=false; continuation.clear();
    SDL_SignalCondition(cv); SDL_UnlockMutex(mu);
    page={}; votes={}; likesLabel.clear(); dislikesLabel.clear(); selected=line=0; loaded=loading=false;
  }
  void request(bool more=false) {
    if (loading || (more && page.continuation.empty())) return;
    if (!thread) { page.error="comments_worker"; loaded=true; return; }
    SDL_LockMutex(mu); continuation=more ? page.continuation : ""; wantComments=true;
    SDL_SignalCondition(cv); SDL_UnlockMutex(mu); loading=true;
  }
  bool poll() {
    SDL_LockMutex(mu);
    bool changed=ready && reply.revision==revision;
    if (changed) {
      if (reply.comments) {
        if (reply.page.error.empty()) { page=std::move(reply.page); selected=line=0; }
        else page.error=std::move(reply.page.error);
        loading=false; loaded=true;
      } else {
        votes=reply.votes;
        likesLabel=votes.available ? std::to_string(votes.likes) : "";
        dislikesLabel=votes.available ? std::to_string(votes.dislikes)+" · RYD" : "";
      }
    }
    ready=false; SDL_SignalCondition(cv); SDL_UnlockMutex(mu); return changed;
  }
  void run() {
    auto net=yt::https(&stop); yt::extras::Client client(*net);
    while (!stop) {
      SDL_LockMutex(mu);
      while (!stop && (ready || (!wantVotes && !wantComments))) SDL_WaitConditionTimeout(cv,mu,100);
      if (stop) { SDL_UnlockMutex(mu); break; }
      auto video=id, lang=language, token=continuation; auto rev=revision;
      bool comments=wantComments;
      if (comments) wantComments=false; else wantVotes=false;
      SDL_UnlockMutex(mu);
      Reply result; result.revision=rev; result.comments=comments;
      if (comments) result.page=client.comments(video,token,lang);
      else result.votes=client.votes(video,SDL_GetTicks());
      SDL_LockMutex(mu);
      if (revision==rev) { reply=std::move(result); ready=true; }
      SDL_UnlockMutex(mu);
    }
  }
  void close() {
    if (!mu) return;
    stop=true; SDL_LockMutex(mu); SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
    if (thread) SDL_WaitThread(thread,nullptr);
    SDL_DestroyCondition(cv); SDL_DestroyMutex(mu);
    thread=nullptr; mu=nullptr; cv=nullptr;
  }
  ~PlayerExtras() { close(); }
};
