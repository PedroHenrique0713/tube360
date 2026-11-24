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

  bool parse(const std::string& raw) {
    auto lines = net::split(raw, '\n');
    auto head = net::split(lines[0], '\t');
    if (head.size() != 5 || head[0] != "ok") { failed = true; return false; }
    selected = head[1]; live = head[3] == "1"; reason = head[4];
    for (size_t i = 1; i < lines.size(); i++) {
      auto f = net::split(lines[i], '\t');
      if (f.size() == 5 && f[0] == "meta") { title = f[1]; channel = f[2]; date = f[3]; channelId = f[4]; }
      else if (f.size() == 4 && f[0] == "track") tracks.push_back({f[1], f[2], f[3] == "auto"});
      else if (f.size() == 3 && f[0] == "chapter") {
        double start = std::atof(f[1].c_str());
        if (std::isfinite(start) && start >= 0) chapters.push_back({start, f[2]});
      } else if (f.size() == 4 && f[0] == "cue") {
        double start = std::atof(f[1].c_str()), end = std::atof(f[2].c_str());
        if (std::isfinite(start) && std::isfinite(end) && start >= 0 && end > start) cues.push_back({start, end, f[3]});
      }
    }
    std::sort(cues.begin(), cues.end(), [](const Cue& a, const Cue& b) { return a.start < b.start; });
    std::sort(chapters.begin(), chapters.end(), [](const Chapter& a, const Chapter& b) { return a.start < b.start; });
    return true;
  }
  int at(double position) const {
    auto it = std::upper_bound(cues.begin(), cues.end(), position, [](double p, const Cue& c) { return p < c.start; });
    if (it == cues.begin()) return -1;
    --it;
    return position < it->end ? (int)(it - cues.begin()) : -1;
  }
};

struct Loader {
  SDL_Mutex* mu = SDL_CreateMutex(); SDL_Condition* cv = SDL_CreateCondition(); SDL_Thread* thread = nullptr;
  net::Server srv;
  std::string path;
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
  void request(const net::Server& server, const std::string& id, const char* lang, const std::string& track, const yt::Video* media = nullptr, bool reuseTracks = false) {
    SDL_LockMutex(mu);
    native = media != nullptr || (reuseTracks && native);
    if (media) {
      tracks = media->captions; live = media->live || (!media->hls.empty() && !media->duration);
      metadata = {}; metadata.title = media->title; metadata.channel = media->channel;
      metadata.channelId = media->channelId; metadata.date = media->date;
    } else if (!reuseTracks) metadata = {};
    language = lang; preferred = track;
    srv = server; path = "/api/captions/" + id + "?lang=" + lang;
    if (!track.empty()) path += "&track=" + net::urlEncode(track);
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
      std::string url = path; net::Server server = srv; uint64_t ticket = revision; pending = false;
      auto options = tracks; auto lang = language, selected = preferred; bool direct = native, broadcast = live;
      Data data = metadata;
      SDL_UnlockMutex(mu);
      net::Response response;
      if (direct) {
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
      }
      if (!direct || (data.failed && net::serverEnabled)) {
        if (!net::get(server,url,response,30000,2 << 20) || response.body.size() > (2 << 20)) data.failed = true;
        else { data = {}; data.parse(std::string(response.body.begin(),response.body.end())); }
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
