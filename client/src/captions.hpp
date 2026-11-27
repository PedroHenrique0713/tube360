#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>
#include "net.hpp"
#include "yt/client.hpp"
#include "yt/thread.hpp"

namespace cc {
struct Cue { double start = 0, end = 0; std::string text; };
struct Track { std::string code, name; bool automatic = false; };
struct Chapter { double start = 0; std::string title; };
struct Data {
  std::string selected, title, channel, date, channelId, reason;
  bool live = false, failed = false;
  std::vector<Cue> cues;
  std::vector<Track> tracks;
  std::vector<Chapter> chapters;

  int at(double position) const {
    auto it = std::upper_bound(cues.begin(), cues.end(), position, [](double p, const Cue& c) { return p < c.start; });
    if (it == cues.begin()) return -1;
    --it;
    return position < it->end ? (int)(it - cues.begin()) : -1;
  }
};

struct Loader {
  SDL_Mutex* mu = SDL_CreateMutex(); SDL_Condition* cv = SDL_CreateCondition(); SDL_Thread* thread = nullptr;
  Data ready;
  Data metadata;
  uint64_t revision = 0, readyRevision = 0;
  bool quit = false, pending = false, available = false;
  bool loading = false, native = false, live = false;
  std::string language, preferred;
  std::vector<yt::Track> tracks;
  void cancel() {
    SDL_LockMutex(mu); revision++; pending = available = loading = native = false; tracks.clear(); SDL_UnlockMutex(mu);
  }
  void stop() {
    SDL_LockMutex(mu); quit = true; SDL_SignalCondition(cv); SDL_UnlockMutex(mu);
    if (thread) SDL_WaitThread(thread, nullptr);
    thread = nullptr;
  }
  ~Loader() {
    if (!quit) stop();
    SDL_DestroyCondition(cv); SDL_DestroyMutex(mu);
  }
  // Sem `media` repete o pedido com as faixas e os metadados do vídeo que já está aberto (troca de idioma, nova tentativa).
  void request(const char* lang, const std::string& track, const yt::Video* media = nullptr) {
    SDL_LockMutex(mu);
    if (media) {
      native = true;
      tracks = media->captions; live = media->live || (!media->hls.empty() && !media->duration);
      metadata = {}; metadata.title = media->title; metadata.channel = media->channel;
      metadata.channelId = media->channelId; metadata.date = media->date;
    }
    language = lang; preferred = track;
    revision++; pending = true; loading = true; available = false;
    SDL_SignalCondition(cv); SDL_UnlockMutex(mu);
    if (!thread) thread = yt::worker([](void* p) -> int { ((Loader*)p)->loop(); return 0; }, "legendas", this);
  }
  void loop() {
#ifdef __XSDK__
    xsdk_thread_move(5);
#endif
    for (;;) {
      SDL_LockMutex(mu);
      while (!pending && !quit) SDL_WaitCondition(cv, mu);
      if (quit) { SDL_UnlockMutex(mu); return; }
      uint64_t ticket = revision; pending = false;
      auto options = tracks; auto lang = language, selected = preferred; bool broadcast = live;
      Data data = metadata;
      SDL_UnlockMutex(mu);
      data.live = broadcast;
      const yt::Track* best = nullptr;
      for (auto& t : options) {
        data.tracks.push_back({t.language,t.name,false});
        if (!selected.empty()) { if (t.language == selected) best = &t; }
        else if (!best || t.language == lang) best = &t;
      }
      if (best) data.selected = best->language;
      if (broadcast) data.reason = "live_clock";
      else if (best) {
        auto https = yt::https(); yt::Client youtube(*https);
        std::vector<yt::Cue> cues; std::string error;
        data.failed = !youtube.captions(*best,cues,error);
        for (auto& cue : cues) if (cue.durationMs > 0)
          data.cues.push_back({cue.startMs * 0.001, (double(cue.startMs)+cue.durationMs)*0.001, std::move(cue.text)});
        std::stable_sort(data.cues.begin(),data.cues.end(),[](const Cue& a,const Cue& b){ return a.start < b.start; });
      }
      SDL_LockMutex(mu);
      if (ticket == revision && !quit) { ready = std::move(data); readyRevision = ticket; available = true; }
      SDL_UnlockMutex(mu);
    }
  }
  bool pop(Data& data) {
    SDL_LockMutex(mu); bool ok = available && readyRevision == revision;
    if (ok) { data = std::move(ready); available = false; loading = false; }
    SDL_UnlockMutex(mu); return ok;
  }
};
}  // namespace cc
