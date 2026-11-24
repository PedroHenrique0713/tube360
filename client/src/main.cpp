// Tube360: YouTube nativo no Xbox 360 (BadUpdate), servido pelo notebook (server/, docs/PROTOCOL.md).
// Um código só: no Fedora (janela do SDL, teclado ou controle) e no 360 (vulkan4xenon + SDL-xenon).
// Interface de TV: menu lateral, categorias, grade e configurações, com a cor do vídeo em foco no ambiente.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#undef STB_TRUETYPE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#undef PL_MPEG_IMPLEMENTATION

#include "gfx.hpp"
#include "net.hpp"
#include "player.hpp"
#include "music_player.hpp"
#include "player_ui.hpp"
#include "player_text.hpp"
#include "captions.hpp"
#include "settings.hpp"
#include "audio_track_label.hpp"
#include "account.hpp"
#include "yt/console_account.hpp"
#include "sleep.hpp"
#include "icons.hpp"
#include "cast/receiver.hpp"

#ifdef __XSDK__
#include "xsdk/net.h"
extern "C" void install_player_fault_handler();
// XAM começa com recepção de 16 KB por socket; 63 KB segura o fluxo de vídeo com folga (xsdk/net.h).
extern "C" const XsdkNetworkLimits xsdk_network_limits = {32, 8, 63, 16};
#endif

using gfx::Color;
using gfx::rgb;
using K = i18n::Key;

// ---------------------------------------------------------------- tema
namespace ui {
const Color BG = rgb(0x0b0b0e), SURFACE = rgb(0x18181d), SURFACE2 = rgb(0x24242b), TEXT = rgb(0xf5f5f7),
            MUTED = rgb(0xa1a1aa), FAINT = rgb(0x6b6b74), A1 = rgb(0xff0f3c), A2 = rgb(0xff5a52), INK = rgb(0x0b0b0e);
enum { F_BODY, F_SMALL, F_CARD, F_H2, F_LOGO, F_BIG, F_KEY, F_TINY, F_HERO, F_CHAN, F_LOGOL, F_CODE, F_VIDEO_TITLE, F_META, F_DETAIL, F_CLOCK, F_BADGE };
constexpr float W = 1280, H = 720, RAIL = 96, X0 = 136;
inline Color alpha(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }
inline Color mix(Color a, Color b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t}; }
}  // namespace ui

struct Video { std::string id, channel, views, title; int dur = -1; double pos = 0; };
struct Feed { std::string title; std::vector<Video> items; bool loading = false; K error = K::None; bool more = true, loadingMore = false, direct = false; };
struct Shelf { std::string title, query; Feed feed; int sel = 0; std::string kind = "search"; uint64_t stamp = 0; };

static std::string fmtTime(double s) {
  if (s < 0) s = 0;
  int t = (int)s, h = t / 3600, m = (t / 60) % 60, sec = t % 60;
  char b[32];
  if (h) std::snprintf(b, sizeof(b), "%d:%02d:%02d", h, m, sec); else std::snprintf(b, sizeof(b), "%d:%02d", m, sec);
  return b;
}

// ---------------------------------------------------------------- trabalho em segundo plano
// kind: 0 home, 1 busca do usuário, 2 miniatura, 3 categoria, 5 controle/conta.
struct Job { int kind; std::string a; uint64_t stamp; bool fresh = false; int offset = 0; i18n::Lang lang = i18n::Lang::EN; std::string feedKind; int w = 320, h = 180; bool native = false; std::string query; uint64_t backend = 0, catalogBackend = 0; };
struct Result { int kind = -1; std::string key; Feed feed; std::vector<uint8_t> rgba; int w = 0, h = 0; uint64_t stamp = 0; int offset = 0; bool localAccount = false, accountRoute = false, catalog = false, catalogRoute = false; uint64_t backend = 0, catalogBackend = 0; };

static void parseFeed(const std::vector<uint8_t>& raw, Feed& feed) {
  std::string body(raw.begin(), raw.end());
  auto lines = net::split(body, '\n');
  auto head = lines.empty() ? std::vector<std::string>{} : net::split(lines[0], '\t');
  if (head.empty() || head[0] != "ok") { feed.error = K::BadResponse; return; }
  feed.title = head.size() > 2 ? head[2] : "";
  for (size_t i = 1; i < lines.size(); i++) {
    auto f = net::split(lines[i], '\t');
    if (f.size() < 5 || f[0].empty()) continue;
    Video v; v.id = f[0]; v.dur = std::atoi(f[1].c_str()); v.channel = f[2]; v.views = f[3]; v.title = f[4];
    if (f.size() > 5) { double pos = std::atof(f[5].c_str()); if (std::isfinite(pos)) v.pos = std::max(0.0, pos); }
    feed.items.push_back(v);
  }
}

struct Worker {
  net::Server srv; std::shared_ptr<yt::ConsoleAccount> consoleAccount; SDL_Mutex* mu = SDL_CreateMutex(); SDL_Condition* cv = SDL_CreateCondition();
  std::deque<Job> jobs; std::deque<Result> results; bool quit = false, finishJobs = false; int hw = 4;
  std::string musicToken;
  std::atomic_uint64_t musicRevision{0};
  bool setMusicToken(const std::string& access) {
    if (access.size() > 8192 || access.find_first_of("\r\n\t ") != std::string::npos) return false;
    SDL_LockMutex(mu);
    if (musicToken != access) { musicToken = access; musicRevision.fetch_add(1); }
    SDL_UnlockMutex(mu); return true;
  }
  static int main(void* p) { ((Worker*)p)->loop(); return 0; }
  void add(Job j, bool front = false) { if (consoleAccount) j.backend = consoleAccount->revision(); SDL_LockMutex(mu); j.catalogBackend = musicRevision.load(); if (front) jobs.push_front(j); else jobs.push_back(j); SDL_SignalCondition(cv); SDL_UnlockMutex(mu); }
  void loop() {
    player::moveToHardwareThread(hw);
    auto https = yt::catalog::transport();
    yt::Client youtube(*https);
    yt::catalog::Client catalog(*https);
    for (;;) {
      SDL_LockMutex(mu);
      while (jobs.empty() && !quit) SDL_WaitCondition(cv, mu);
      if (quit && (!finishJobs || jobs.empty())) { SDL_UnlockMutex(mu); return; }
      Job j = jobs.front(); jobs.pop_front();
      std::string access = musicToken;
      SDL_UnlockMutex(mu);
      Result r; r.kind = j.kind; r.key = j.a; r.stamp = j.stamp; r.offset = j.offset;
      net::Response resp;
      if (j.kind == 6 && yt::catalog::supported(j.a)) {
        if (j.catalogBackend != musicRevision.load() || (consoleAccount && j.backend != consoleAccount->revision())) continue;
        r.catalogRoute = true; r.catalogBackend = j.catalogBackend;
        r.backend = j.backend;
        catalog.setToken(access);
        r.feed.title = catalog.handle(j.a);
        if (r.feed.title.rfind("ok\t",0) == 0) r.catalog = true;
        bool accountFallback = !r.catalog && consoleAccount && consoleAccount->active() && yt::ConsoleAccount::supports(j.a);
        if (!r.catalog && !net::serverEnabled && !accountFallback) { r.feed.error = K::NoResponse; r.feed.title.clear(); }
        if (r.catalog || (!net::serverEnabled && !accountFallback)) {
          LOG("catalogo console: %s, %u bytes%s%s",j.a.substr(0,j.a.find('?')).c_str(),(unsigned)r.feed.title.size(),
              catalog.authError.empty() ? "" : ", conta recusada: ",catalog.authError.c_str());
          SDL_LockMutex(mu); results.push_back(std::move(r)); SDL_UnlockMutex(mu); continue;
        }
        r.feed.title.clear();
      }
      std::string accountPath = j.a;
      bool feedJob = j.kind == 0 || j.kind == 1 || j.kind == 3;
      if (feedJob) accountPath = "/api/feed?kind=" + j.feedKind + "&offset=" + std::to_string(j.offset) +
          "&fresh=" + (j.fresh ? "1" : "0") + "&lang=" + i18n::code(j.lang);
      bool accountRoute = consoleAccount && yt::ConsoleAccount::supports(accountPath);
      if (accountRoute && j.backend != consoleAccount->revision()) continue;
      r.accountRoute = accountRoute; r.backend = j.backend;
      auto nativeAccount = [&]() {
        auto path = accountPath;
        if (path.find("lang=") == std::string::npos) path += (path.find('?') == std::string::npos ? "?lang=" : "&lang=") + std::string(i18n::code(j.lang));
        auto reply = consoleAccount->request(path);
        r.localAccount = true; r.backend = consoleAccount->revision();
        if (reply.ok()) {
          if (feedJob) {
            std::vector<uint8_t> raw(reply.body.begin(), reply.body.end()); parseFeed(raw, r.feed);
            auto header = net::split(reply.body.substr(0, reply.body.find('\n')), '\t');
            r.feed.more = header.size() > 3 && header[3] == "1";
          } else r.feed.title = std::move(reply.body);
        } else if (j.kind == 6) {
          r.feed.title = std::string("erro\t") + i18n::get(path.find("lang=pt") != std::string::npos ? i18n::Lang::PT : i18n::Lang::EN,
              reply.error == "signed_out" ? K::AccountRequired : K::ActionFailed) + "\n";
        } else r.feed.error = K::BadResponse;
        if (j.kind == 7 && !reply.ok()) LOG("histórico nativo: %s", reply.error.c_str());
      };
      if (accountRoute && (!net::serverEnabled || consoleAccount->active()) && !(j.native && feedJob && j.feedKind == "home")) {
        nativeAccount();
        if (j.kind == 7) continue;
        SDL_LockMutex(mu); results.push_back(std::move(r)); SDL_UnlockMutex(mu);
        continue;
      }
      if (j.native && j.kind != 2) {
        yt::Page page;
        if (j.kind == 5) page = youtube.next(j.a.substr(11,11));
        else if (j.feedKind == "home") page = youtube.home();
        else page = youtube.search(j.kind == 6 ? j.query : j.a);
        if (j.feedKind == "home" && page.error.empty() && page.cards.empty())
          page = youtube.search(i18n::get(j.lang, K::FootballQuery));
        if (page.error.empty() && (j.kind != 5 || !page.cards.empty())) {
          r.feed.direct = true;
          for (auto& card : page.cards) {
            Video v; v.id = std::move(card.id); v.title = std::move(card.title);
            r.feed.items.push_back(std::move(v));
          }
          r.feed.title = j.kind == 6 ? j.query : j.a;
        } else if (!net::serverEnabled) {
          r.feed.error = K::BadResponse;
        }
      }
      if (r.feed.direct || r.feed.error != K::None) {
        SDL_LockMutex(mu); results.push_back(std::move(r)); SDL_UnlockMutex(mu);
        continue;
      }
      if (j.kind == 6) {
        if (!net::get(srv, j.a, resp, 30000, 512 * 1024)) r.feed.error = K::NoResponse;
        r.feed.title.assign(resp.body.begin(), resp.body.end());
      } else if (j.kind == 5) {
        if (!net::get(srv, j.a, resp, 8000, 4096)) r.feed.error = K::NoResponse;
        r.feed.title.assign(resp.body.begin(), resp.body.end());
      } else if (j.kind == 0 || j.kind == 1 || j.kind == 3) {
        std::string path = "/api/feed?kind=" + j.feedKind + "&n=24&lang=" + i18n::code(j.lang);
        if (j.feedKind == "search") path += "&q=" + net::urlEncode(j.a);
        if (j.fresh) path += "&fresh=1";
        if (j.offset) path += "&offset=" + std::to_string(j.offset);
        if (!net::get(srv, path, resp, 30000)) r.feed.error = K::NoResponse;
        else parseFeed(resp.body, r.feed);
      } else {
        std::string size = "?w=" + std::to_string(j.w) + "&h=" + std::to_string(j.h);
        bool native = false;
        if (consoleAccount && j.a.rfind("~yt", 0) == 0) {
          auto url = consoleAccount->pictureUrl(j.a);
          auto img = url.empty() ? yt::Response{} : https->request(url);
          native = img.status == 200 && img.error.empty();
          if (native) resp.body.assign(img.body.begin(), img.body.end());
        }
        bool markedBanner = !j.a.empty() && j.a.front() == '!';
        bool banner = markedBanner || (j.w == 1184 && j.h == 160);
        std::string address = markedBanner ? j.a.substr(1) : j.a;
        bool artwork = !yt::catalog::imageUrl(address).empty();
        if (artwork) {
          auto img = https->request(address);
          native = img.status == 200 && img.error.empty();
          if (native) resp.body.assign(img.body.begin(),img.body.end());
        }
        if (!native && (j.native || !net::serverEnabled || (consoleAccount && consoleAccount->active())) && j.a.size() == 11 && j.a.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos) {
          auto img = https->request("https://i.ytimg.com/vi/" + j.a + "/mqdefault.jpg");
          native = img.status == 200 && img.error.empty();
          if (native) resp.body.assign(img.body.begin(), img.body.end());
        }
        if (native || (!artwork && net::get(srv, "/api/thumb/" + (j.a[0] == '~' ? "nav/" + j.a.substr(1) : j.a) + size, resp, 10000))) {
          int w, h, n;
          // Não decodifica dimensões arbitrárias recebidas pela rede.
          if (!stbi_info_from_memory(resp.body.data(), (int)resp.body.size(), &w, &h, &n) || (artwork || (native && j.a.rfind("~yt",0) == 0) ? (w < 1 || h < 1 || w > 2048 || h > 2048 || int64_t(w)*h > 2097152) : native ? (w != 320 || h != 180) : (w != j.w || h != j.h))) { w = h = 0; }
          unsigned char* px = w ? stbi_load_from_memory(resp.body.data(), (int)resp.body.size(), &w, &h, &n, 4) : nullptr;
          if (px) {
            r.w = j.w; r.h = j.h;
            if (artwork || (native && j.a.rfind("~yt",0) == 0)) {
              r.rgba.assign((size_t)j.w*j.h*4,0);
              double scale = std::min(double(j.w)/w,double(j.h)/h);
              int dw = std::max(1,int(w*scale)), dh = std::max(1,int(h*scale));
              if (banner) { dw = j.w; dh = j.h; }
              int dx = (j.w-dw)/2, dy = (j.h-dh)/2;
              for (int y = 0; y < dh; ++y) for (int x = 0; x < dw; ++x)
                std::memcpy(r.rgba.data()+4*((y+dy)*j.w+x+dx),px+4*((y*h/dh)*w+x*w/dw),4);
            } else if (w == j.w && h == j.h) r.rgba.assign(px, px + (size_t)w*h*4);
            else {
              r.rgba.resize((size_t)j.w*j.h*4);
              for (int y=0; y<j.h; ++y) for (int x=0; x<j.w; ++x)
                std::memcpy(r.rgba.data() + 4*(y*j.w+x), px + 4*((y*h/j.h)*w + x*w/j.w), 4);
            }
            stbi_image_free(px);
          }
        }
      }
      bool safeFallback = feedJob || accountPath == "/api/account" || accountPath == "/api/account/login" ||
          accountPath.rfind("/api/nav?", 0) == 0 || accountPath.rfind("/api/nav/channel?", 0) == 0 || accountPath.ends_with("/next");
      if (accountRoute && safeFallback && r.feed.error == K::NoResponse) {
        consoleAccount->select(true);
        r.feed = {}; nativeAccount();
      }
      SDL_LockMutex(mu); results.push_back(std::move(r)); SDL_UnlockMutex(mu);
    }
  }
  bool pop(Result& r) { SDL_LockMutex(mu); bool ok = !results.empty(); if (ok) { r = std::move(results.front()); results.pop_front(); } SDL_UnlockMutex(mu); return ok; }
};

// ---------------------------------------------------------------- controle pelo celular (poll a cada ~500 ms)
struct Remote {
  net::Server srv; SDL_Mutex* mu = SDL_CreateMutex(); std::deque<std::vector<std::string>> cmds; std::atomic_bool quit{false};
  std::string playing, ended; double pos = 0, dur = 0; bool paused = false; bool online = false;
  static int main(void* p) { ((Remote*)p)->loop(); return 0; }
  void loop() {
    player::moveToHardwareThread(5);
    while (!quit) {
      if (!net::serverEnabled) { SDL_Delay(100); continue; }
      SDL_LockMutex(mu);
      char q[256];
      std::snprintf(q, sizeof(q), "/api/remote/poll?playing=%s&pos=%.1f&dur=%.1f&paused=%d&ended=%s", playing.c_str(), pos, dur, paused ? 1 : 0, ended.c_str());
      ended.clear();
      SDL_UnlockMutex(mu);
      net::Response r;
      bool ok = net::get(srv, q, r, 3000, 4096);
      SDL_LockMutex(mu);
      ok = ok && net::serverEnabled; online = ok;
      if (ok) {
        std::string line(r.body.begin(), r.body.end());
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (!line.empty() && line != "none") cmds.push_back(net::split(line, '\t'));
      }
      SDL_UnlockMutex(mu);
      SDL_Delay(ok ? 450 : 2000);
    }
  }
};

// ---------------------------------------------------------------- miniaturas na GPU (vagas reaproveitadas) e cor média
struct Thumbs {
  struct Slot { gfx::Tex tex; std::string id; uint64_t used = 0; Color avg{0.2f, 0.2f, 0.25f, 1}; };
  std::map<std::string,uint64_t> retryAt;
  std::vector<Slot> slots; std::map<std::string, int> byId; std::map<std::string, Result> waiting;
  static constexpr size_t budgetBytes = 64u * 320 * 180 * 4;
  int maxSlots = 64, width = 320, height = 180; uint64_t generation = 1;
  static constexpr int bannerWidth = 1184, bannerHeight = 160;
  Slot bannerSlot;
  size_t bannerBytes = 0;
  static size_t allocation(gfx::Gfx& g, const gfx::Tex& tex) {
    VkMemoryRequirements req; vkGetImageMemoryRequirements(g.dev, tex.img, &req);
#ifdef __XSDK__
    return (req.size + 65535u) & ~size_t(65535u);
#else
    return req.size;
#endif
  }
  static void release(gfx::Gfx& g, Slot& s) {
    vkFreeDescriptorSets(g.dev, g.pool, 1, &s.tex.set);
    vkDestroyImageView(g.dev, s.tex.view, nullptr); vkDestroyImage(g.dev, s.tex.img, nullptr); vkFreeMemory(g.dev, s.tex.mem, nullptr);
  }
  size_t slotBytes = 0, peakPending = 0;
  void configure(gfx::Gfx& g, Worker& worker, int cols, bool cover = false, bool banner = false) {
    int w = cover ? 640 : cols == 3 ? 384 : 320, h = cover ? 360 : cols == 3 ? 216 : 180;
    if (w == width && h == height && banner == bool(bannerBytes)) return;
    vkDeviceWaitIdle(g.dev);
    for (auto& s : slots) release(g, s);
    if (bannerBytes) { release(g, bannerSlot); bannerSlot = {}; bannerBytes = 0; }
    if (banner) {
      bannerSlot.tex = gfx::makeImage(g, bannerWidth, bannerHeight, VK_FORMAT_R8G8B8A8_UNORM, true);
      bannerBytes = allocation(g, bannerSlot.tex);
    }
    slots.clear(); byId.clear(); waiting.clear(); retryAt.clear(); generation++;
    width = w; height = h; maxSlots = 64; slotBytes = peakPending = 0;
    SDL_LockMutex(worker.mu); worker.jobs.clear(); worker.results.clear(); SDL_UnlockMutex(worker.mu);
  }
  const Slot* get(Worker& w, const std::string& id, uint64_t frame, bool direct = false, bool banner = false) {
    if (banner && bannerSlot.id == id) return &bannerSlot;
    auto it = byId.find(id);
    if (it != byId.end()) { Slot& s = slots[(size_t)it->second]; s.used = frame; return &s; }
    auto failed = retryAt.find(id);
    if (failed != retryAt.end()) {
      if (SDL_GetTicks() < failed->second) return nullptr;
      retryAt.erase(failed);
    }
    if (!waiting.count(id) && waiting.size() < 8) {
      Job j{2, id, generation}; j.native = direct; j.w = banner ? bannerWidth : width; j.h = banner ? bannerHeight : height; w.add(j); waiting[id] = Result{};
    }
    return nullptr;
  }
  void arrived(Result&& r) {
    if (r.stamp != generation) return;
    waiting[r.key] = std::move(r);
    size_t bytes = 0; for (auto& it : waiting) bytes += it.second.rgba.size();
    peakPending = std::max(peakPending, bytes);
  }
  void upload(gfx::Gfx& g, uint64_t frame, int budget) {
    for (auto it = waiting.begin(); it != waiting.end() && budget > 0;) {
      Result& r = it->second;
      if (r.kind != 2) { ++it; continue; }
      if (!r.rgba.empty() && r.w == bannerWidth && r.h == bannerHeight && bannerBytes) {
        gfx::upload(g, bannerSlot.tex, r.rgba.data(), r.rgba.size());
        bannerSlot.id = r.key; it = waiting.erase(it); budget--; continue;
      }
      if (r.rgba.empty() || r.w != width || r.h != height) {
        if (retryAt.size() >= 64) retryAt.erase(retryAt.begin());
        retryAt[r.key] = SDL_GetTicks() + 30000; it = waiting.erase(it); continue;
      }
      int idx;
      if ((int)slots.size() < maxSlots) {
        slots.push_back({}); idx = (int)slots.size() - 1;
        slots[(size_t)idx].tex = gfx::makeImage(g, width, height, VK_FORMAT_R8G8B8A8_UNORM, true);
        if (!slotBytes) {
          slotBytes = allocation(g, slots[(size_t)idx].tex);
          maxSlots = std::min(64, (int)((budgetBytes - bannerBytes) / slotBytes));
          LOG("miniaturas %dx%d: RGBA %u, alocação %u bytes/vaga, limite %d vagas = %u bytes", width, height,
              (unsigned)(width * height * 4), (unsigned)slotBytes, maxSlots, (unsigned)(maxSlots * slotBytes));
        }
      }
      else {
        idx = 0; for (size_t i = 1; i < slots.size(); i++) if (slots[i].used < slots[(size_t)idx].used) idx = (int)i;
        if (slots[(size_t)idx].used + 2 >= frame) break;  // tudo em uso agora: espera
        byId.erase(slots[(size_t)idx].id);
        vkDeviceWaitIdle(g.dev);
      }
      Slot& s = slots[(size_t)idx];
      gfx::upload(g, s.tex, r.rgba.data(), r.rgba.size());
      // cor média (amostrada) para o ambiente: mais saturada e escura, para tingir o fundo sem brigar com o texto
      double ar = 0, ag = 0, ab = 0; int n = 0;
      for (size_t i = 0; i < r.rgba.size(); i += 4 * 37) { ar += r.rgba[i]; ag += r.rgba[i + 1]; ab += r.rgba[i + 2]; n++; }
      Color c{(float)(ar / n / 255), (float)(ag / n / 255), (float)(ab / n / 255), 1};
      float l = (c.r + c.g + c.b) / 3;
      c = {std::clamp(l + (c.r - l) * 1.8f, 0.f, 1.f), std::clamp(l + (c.g - l) * 1.8f, 0.f, 1.f), std::clamp(l + (c.b - l) * 1.8f, 0.f, 1.f), 1};
      s.avg = c; s.id = r.key; s.used = frame; byId[r.key] = idx;
      it = waiting.erase(it); budget--;
    }
  }
};

// ---------------------------------------------------------------- entrada com repetição
struct Input {
  enum { UP, DOWN, LEFT, RIGHT, A, B, X, Y, START, BACK, LB, RB, LT, RT, N };
  bool held[N] = {}; uint64_t since[N] = {}, lastRep[N] = {}; bool fired[N] = {};
  void set(int b, bool down, uint64_t now) {
    if (down && !held[b]) { fired[b] = true; since[b] = now; lastRep[b] = now; }
    held[b] = down;
  }
  void tick(uint64_t now) {
    for (int b : {UP, DOWN, LEFT, RIGHT}) if (held[b] && now - since[b] > 380 && now - lastRep[b] > 90) { fired[b] = true; lastRep[b] = now; }
  }
  bool hit(int b) { bool f = fired[b]; fired[b] = false; return f; }
  void clear() { for (auto& f : fired) f = false; }
};

// ---------------------------------------------------------------- peças de desenho
static const Color XA = rgb(0x7ed957), XB = rgb(0xff5a52), XX = rgb(0x4ea8ff), XY = rgb(0xffd23f), XG = rgb(0xd4d4d8);

// Botões do controle no estilo do Xbox One/Series (pedido do Pedro: nada do padrão colorido do 360): botão escuro com
// aro fino e a letra colorida; View e Menu como ícones; LB/RB, setas e afins numa pílula escura com texto branco.
static float buttonGlyph(gfx::Gfx& g, float x, float y, const char* label, Color c) {
  const Color body = rgb(0x18181b), ring = ui::alpha(rgb(0xffffff), 0.22f);
  std::string l = label;
  const K buttons[] = {K::BtnA, K::BtnB, K::BtnX, K::BtnY, K::BtnView, K::BtnMenu, K::BtnShoulders, K::BtnTriggers, K::BtnHorizontal, K::BtnVertical};
  for (K key : buttons) if (l == i18n::get(i18n::Lang::EN, key)) { label = i18n::get(i18n::Lang::EN, key); break; }
  if (l == i18n::get(i18n::Lang::EN, K::BtnHorizontal)) {
    gfx::rect(g, x, y, 40, 24, ring, 12);
    gfx::rect(g, x + 1.2f, y + 1.2f, 37.6f, 21.6f, body, 10.8f);
    gfx::triangle(g, x + 9, y + 12, x + 15, y + 7, x + 15, y + 17, XG);
    gfx::triangle(g, x + 31, y + 12, x + 25, y + 7, x + 25, y + 17, XG);
    return 40;
  }
  if (l == "A" || l == "B" || l == "X" || l == "Y") {
    gfx::rect(g, x, y, 24, 24, ring, 12);
    gfx::rect(g, x + 1.2f, y + 1.2f, 21.6f, 21.6f, body, 10.8f);
    gfx::text(g, ui::F_TINY, label, x + 12, y + 5, c, 1);
    return 24;
  }
  if (l == "View" || l == "Menu") {
    gfx::rect(g, x, y, 24, 24, ring, 12);
    gfx::rect(g, x + 1.2f, y + 1.2f, 21.6f, 21.6f, body, 10.8f);
    Color w = rgb(0xf4f4f5);
    if (l == "Menu") for (int k = 0; k < 3; k++) gfx::rect(g, x + 7, y + 7.5f + k * 4, 10, 1.6f, w, 0.8f);
    else { gfx::rect(g, x + 6, y + 6, 8, 7, w, 1.2f); gfx::rect(g, x + 7.3f, y + 7.3f, 5.4f, 4.4f, body, 0.5f);
           gfx::rect(g, x + 10, y + 10, 8, 7, w, 1.2f); gfx::rect(g, x + 11.3f, y + 11.3f, 5.4f, 4.4f, body, 0.5f); }
    return 24;
  }
  float w = gfx::textWidth(g, ui::F_TINY, label) + 18;
  gfx::rect(g, x, y, w, 24, ring, 12);
  gfx::rect(g, x + 1.2f, y + 1.2f, w - 2.4f, 21.6f, body, 10.8f);
  gfx::text(g, ui::F_TINY, label, x + w / 2, y + 5, rgb(0xf4f4f5), 1);
  return w;
}
static float hint(gfx::Gfx& g, float x, float y, const char* btn, Color bc, const char* label) {
  float w = buttonGlyph(g, x, y, btn, bc);
  return x + w + 8 + gfx::text(g, ui::F_SMALL, label, x + w + 8, y + 2, ui::MUTED) + 26;
}

// Marca: o logo "Órbita" (anel fino em degradê em volta do play) com o nome ao lado; o "360" no fim sai em peso leve.
// Padrão: "Tube360" com play em círculo. marca.txt ao lado do .xex troca: 1ª linha o nome, 2ª linha "retangulo" para o
// play num retângulo arredondado.
static bool brandRect = false;
static std::string brandName = "Tube360";
static float wordmark(gfx::Gfx& g, float x, float y, float a, Color bg) {
  const float d = 40;
  gfx::rectGrad(g, x, y, d, d, ui::alpha(rgb(0xffffff), 0.9f * a), ui::alpha(rgb(0xffffff), 0.12f * a), ui::alpha(rgb(0xff0033), 0.95f * a),
                ui::alpha(rgb(0xffffff), 0.12f * a), d / 2);
  gfx::rect(g, x + 1.5f, y + 1.5f, d - 3, d - 3, bg, d / 2 - 1.5f);
  if (brandRect) {
    float pw = 25, ph = 17.5f, px = x + (d - pw) / 2, py = y + (d - ph) / 2;
    gfx::rect(g, px, py, pw, ph, ui::alpha(rgb(0xff0033), a), 5);
    gfx::triangle(g, px + pw * 0.40f, py + ph * 0.28f, px + pw * 0.40f, py + ph * 0.72f, px + pw * 0.67f, py + ph * 0.5f, ui::alpha(rgb(0xffffff), a));
  } else {
    float c = 22, cx = x + (d - c) / 2, cy = y + (d - c) / 2;
    gfx::rectGrad(g, cx, cy, c, c, ui::alpha(ui::A2, a), ui::alpha(ui::A1, a), ui::alpha(ui::A1, a), ui::alpha(ui::A1, a), c / 2);
    gfx::triangle(g, cx + c * 0.38f, cy + c * 0.27f, cx + c * 0.38f, cy + c * 0.73f, cx + c * 0.75f, cy + c * 0.5f, ui::alpha(rgb(0xffffff), a));
  }
  float tx = x + d + 14;
  std::string main = brandName, tail;
  if (main.size() > 3 && main.compare(main.size() - 3, 3, "360") == 0) { tail = "360"; main.resize(main.size() - 3); }
  bool gap = !main.empty() && main.back() == ' ';
  while (!main.empty() && main.back() == ' ') main.pop_back();
  float w = gfx::text(g, ui::F_LOGO, main, tx, y + 4, ui::alpha(ui::TEXT, a));
  float w2 = tail.empty() ? 0 : gfx::text(g, ui::F_LOGOL, tail, tx + w + (gap ? 12 : 4), y + 4, ui::alpha(ui::TEXT, 0.85f * a), 0, gap ? 3 : 1);
  return d + 14 + w + 12 + w2;
}

// Ícones do Phosphor (icons.hpp): atlas branco com o alfa do SVG, tingido na hora. big = 48 px, senão 24 px; desenhados
// no tamanho exato do atlas e em pixel inteiro, para não borrar.
static gfx::Tex iconTex;
static void loadIcons(gfx::Gfx& g) {
  std::vector<unsigned char> rgba((size_t)icons::W * icons::H * 4, 255);
  for (int i = 0; i < icons::W * icons::H; i++) rgba[(size_t)i * 4 + 3] = icons::ALPHA[i];
  iconTex = gfx::makeImage(g, icons::W, icons::H, VK_FORMAT_R8G8B8A8_UNORM, true);
  gfx::upload(g, iconTex, rgba.data(), rgba.size());
}
static void glyph(gfx::Gfx& g, int which, bool big, float x, float y, Color c) {
  int row=(g.scale > 1 ? 2 : 0) + (big ? 0 : 1), top=0;
  for (int i=0;i<row;++i) top+=icons::SIZES[i];
  const float s = (float)icons::SIZES[row], u = (float)(which * icons::CELL), v = (float)top;
  gfx::imagePart(g, iconTex, std::round(x), std::round(y), s / g.scale, s / g.scale, u / icons::W, v / icons::H, (u + s) / icons::W, (v + s) / icons::H, c);
}

// O UV usa a linha do peso escolhido; o tamanho do quad é o da rasterização.
static void playerIcon(gfx::Gfx& g, icons::PlayerIcon iconId, float cx, bool focused, bool marked = false) {
  int level = g.scale > 1 ? 1 : 0;
  const auto& sprite = icons::PLAYER[level][iconId];
  float size = sprite.size / g.scale, y = focused || marked ? icons::FILL_Y[level] : icons::REGULAR_Y[level];
  if (focused) gfx::rect(g, cx - 22, 644, 44, 44, rgb(0xf4f4f5), 22);
  gfx::imagePart(g, iconTex, cx - size / 2, 666 - size / 2, size, size,
      float(sprite.x) / icons::W, y / icons::H, float(sprite.x + sprite.size) / icons::W, (y + sprite.size) / icons::H,
      focused ? ui::INK : rgb(0xf4f4f5));
}

static void playerTip(gfx::Gfx& g, float cx, const PlayerText& label, const PlayerText& value, bool transport) {
  float w = std::max(label.width, value.width) + 24, h = transport ? 26 : value.text.empty() ? 31 : 48;
  float x = std::clamp(cx - w / 2, 32.f, ui::W - 32 - w), y = (transport ? 636 : 632) - h;
  gfx::rect(g, x, y, w, h, ui::alpha(rgb(0x18181b), 0.94f), 10);
  gfx::text(g, ui::F_CARD, label.text, x + w / 2, y + (transport ? 4 : 7), ui::TEXT, 1);
  if (!value.text.empty()) gfx::text(g, ui::F_DETAIL, value.text, x + w / 2, y + 26, ui::MUTED, 1);
}

// Ícones desenhados com formas (24 px), centrados em (cx, cy).
static void icon(gfx::Gfx& g, int which, float cx, float cy, Color c, Color bg) {
  if (which == 0) {  // início: casa
    gfx::triangle(g, cx - 12, cy - 1, cx, cy - 12, cx + 12, cy - 1, c);
    gfx::rect(g, cx - 8, cy - 2, 16, 13, c, 2);
    gfx::rect(g, cx - 3, cy + 3, 6, 8, bg, 1);
  } else if (which == 1) {  // buscar: lupa
    gfx::rect(g, cx - 11, cy - 11, 17, 17, c, 8.5f);
    gfx::rect(g, cx - 8, cy - 8, 11, 11, bg, 5.5f);
    gfx::triangle(g, cx + 2, cy + 5, cx + 5, cy + 2, cx + 12, cy + 9, c);
    gfx::triangle(g, cx + 2, cy + 5, cx + 12, cy + 9, cx + 9, cy + 12, c);
  } else if (which == 2) {  // celular
    gfx::rect(g, cx - 8, cy - 12, 16, 24, c, 4);
    gfx::rect(g, cx - 5.5f, cy - 9, 11, 16, bg, 2);
    gfx::rect(g, cx - 2, cy + 8, 4, 2, bg, 1);
  } else if (which == 3) {  // engrenagem: aro e oito dentes
    for (int i = 0; i < 8; i++) {
      float a = i * 6.2831853f / 8;
      gfx::rect(g, cx + std::cos(a) * 10 - 3, cy + std::sin(a) * 10 - 3, 6, 6, c, 1.5f);
    }
    gfx::rect(g, cx - 10, cy - 10, 20, 20, c, 10);
    gfx::rect(g, cx - 4, cy - 4, 8, 8, bg, 4);
  } else if (which == 5) {
    gfx::rect(g, cx + 4, cy - 12, 3, 20, c, 1); gfx::rect(g, cx - 4, cy - 12, 11, 4, c, 1);
    gfx::rect(g, cx - 8, cy + 2, 14, 9, c, 4.5f);
  } else if (which == 6) {
    for (int i = 0; i < 3; i++) gfx::rect(g, cx - 12 + i * 8, cy - 11, 5, 22, c, 1);
  } else if (which == 7) {
    gfx::rect(g, cx - 11, cy - 7, 22, 17, c, 3); gfx::rect(g, cx - 7, cy - 12, 14, 2, c, 1);
    gfx::triangle(g, cx - 3, cy - 3, cx - 3, cy + 6, cx + 5, cy + 1.5f, bg);
  } else {  // sair: liga/desliga
    gfx::rect(g, cx - 11, cy - 10, 22, 22, c, 11);
    gfx::rect(g, cx - 8, cy - 7, 16, 16, bg, 8);
    gfx::rect(g, cx - 4.5f, cy - 13, 9, 9, bg, 0);
    gfx::rect(g, cx - 1.5f, cy - 13, 3, 11, c, 1.5f);
  }
}

static void spinner(gfx::Gfx& g, float cx, float cy, float r, double t) {
  for (int i = 0; i < 12; i++) {
    float a = (float)(i / 12.0 * 6.2831853 + t * 4.5);
    float k = (float)i / 11.f;
    Color c = ui::mix(ui::A1, ui::A2, k); c.a = 0.15f + 0.85f * k;
    gfx::rect(g, cx + std::cos(a) * r - 4, cy + std::sin(a) * r - 4, 8, 8, c, 4);
  }
}

#include "music_navigation.hpp"

int main(int argc, char** argv) {
  (void)argc; (void)argv;
#ifdef __XSDK__
  install_player_fault_handler();
  if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) { LOG("SDL: %s", SDL_GetError()); return 1; }
  std::string assets = "";
#else
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) { LOG("SDL: %s", SDL_GetError()); return 1; }
  std::string assets = std::getenv("XT_ASSETS") ? std::getenv("XT_ASSETS") : "assets/";
#endif
  if (!assets.empty() && assets.back() != '/') assets += '/';
  Settings settings; settings.load(assets); net::serverEnabled = settings.server;
  auto tr = [&](K key) { return i18n::get(settings.lang, key); };
  if (FILE* f = std::fopen((assets + "marca.txt").c_str(), "r")) {
    char line[64] = {0};
    if (std::fgets(line, sizeof(line), f)) { brandName = line; while (!brandName.empty() && (brandName.back() == '\n' || brandName.back() == '\r')) brandName.pop_back(); }
    if (std::fgets(line, sizeof(line), f) && !std::strncmp(line, "retangulo", 9)) brandRect = true;
    if (brandName.empty()) brandName = "Tube360";
    std::fclose(f);
  }
  LOG("%s iniciando", brandName.c_str());
  net::Server srv;
  if (FILE* f = std::fopen((assets + "server.txt").c_str(), "r")) {
    char ip[64] = {0}; int port = 0;
    if (std::fscanf(f, "%63s %d", ip, &port) == 2) { srv.ip = ip; srv.port = port; }
    std::fclose(f);
  }
  if (const char* e = std::getenv("XT_SERVER")) { char ip[64] = {0}; int port = 0; if (std::sscanf(e, "%63[^:]:%d", ip, &port) == 2) { srv.ip = ip; srv.port = port; } }
  LOG("servidor %s:%d", srv.ip.c_str(), srv.port);

  gfx::Gfx g;
  if (FILE* f = std::fopen((assets + "video-metrics.txt").c_str(),"r")) { std::fscanf(f,"%d",&g.videoMetrics); std::fclose(f); }
  const char* shotPath = std::getenv("XT_SHOT");
  g.readback = shotPath != nullptr;
  if (!gfx::init(g, settings.outputHeight*16/9, settings.outputHeight, "Tube360")) return 1;
  const std::vector<gfx::FontSpec> fontSpecs={{"inter-400.ttf", 19}, {"inter-400.ttf", 16}, {"inter-600.ttf", 17}, {"inter-600.ttf", 23},
                                  {"inter-700.ttf", 27}, {"inter-700.ttf", 34}, {"inter-600.ttf", 22}, {"inter-700.ttf", 13},
                                  {"inter-700.ttf", 42}, {"inter-600.ttf", 18}, {"inter-300.ttf", 27}, {"inter-700.ttf", 58},
                                  {"inter-600.ttf", 24.2f}, {"inter-400.ttf", 17}, {"inter-400.ttf", 15.1f},
                                  {"inter-400.ttf", 18.2f}, {"inter-700.ttf", 11}};
  if (!gfx::loadFonts(g, assets, fontSpecs)) { LOG("fontes não carregaram"); return 1; }
  loadIcons(g);

  gfx::Video videoTex; g.video = &videoTex;
  music::Player pl; pl.cacheBase = assets + "yt-base"; pl.init(); pl.videoMetrics = g.videoMetrics; pl.directSpeaker.init(assets);
  auto accountNet = yt::accountTransport();
  auto consoleAccount = std::make_shared<yt::ConsoleAccount>(*accountNet, assets + "yt-console-account.json");
  consoleAccount->select(!settings.server);
  Worker worker; worker.srv = srv; worker.consoleAccount = consoleAccount;
  SDL_Thread* wt = yt::worker(Worker::main, "trabalho", &worker);
  // Conta e pareamento não esperam as buscas do feed.
  Worker control; control.srv = srv; control.consoleAccount = consoleAccount; control.finishJobs = true; control.hw = 5;
  SDL_Thread* ct = yt::worker(Worker::main, "conta", &control);
  Worker navWorker; navWorker.srv = srv; navWorker.consoleAccount = consoleAccount; navWorker.hw = 5;
  SDL_Thread* nt = yt::worker(Worker::main, "navegação", &navWorker);
  MusicNavigation nav;
  Worker sleepWorker; sleepWorker.srv = srv; sleepWorker.hw = 5;
  SDL_Thread* st = yt::worker(Worker::main, "timer", &sleepWorker);
  // miniaturas numa fila própria, com 3 threads: não esperam atrás das buscas das categorias
  Worker thumbsWorker; thumbsWorker.srv = srv; thumbsWorker.consoleAccount = consoleAccount; thumbsWorker.hw = 5;
  SDL_Thread* tw[3];
  for (auto& th : tw) th = yt::worker(Worker::main, "miniaturas", &thumbsWorker);
  Remote remote; remote.srv = srv;
  SDL_Thread* rt = SDL_CreateThread(Remote::main, "controle", &remote);
  cast::Receiver phone(assets); phone.enable(true);
  cast::Status phoneStatus; uint64_t phoneStateAt = 0, phonePlaybackSerial = 0;
  float phoneGain = 1;
  bool endTimerPending = false;
  Thumbs thumbs;
  thumbs.configure(g, thumbsWorker, settings.cols);
  std::vector<Shelf> cats;
  Shelf results; bool inResults = false; int cat = 0;
  Account account; bool accountBusy = false, accountKnown = false, accountFailed = false;
  uint64_t accountAt = 0, nestInfoAt = 0, stamp = 0, accountBackend = consoleAccount->revision();
  auto requestFeed = [&](Shelf& shelf, int kind, bool fresh = false, int offset = 0, bool front = false) {
    if (!offset) { shelf.stamp = ++stamp; shelf.feed.loading = true; shelf.feed.loadingMore = false; shelf.feed.error = K::None; }
    Job j{kind, shelf.query, shelf.stamp, fresh, offset}; j.lang = settings.lang; j.feedKind = shelf.kind;
    j.native = !offset && (shelf.kind == "search" || (shelf.kind == "home" && !account.signedIn()));
    worker.add(j, front);
  };
  auto rebuildCats = [&]() {
    SDL_LockMutex(worker.mu); worker.jobs.clear(); SDL_UnlockMutex(worker.mu);
    cats.clear(); cat = 0;
    cats.push_back({tr(account.signedIn() ? K::ForYou : K::All), "", {}, 0, "home"});
    if (account.signedIn()) {
      cats.push_back({tr(K::Subscriptions), "", {}, 0, "subs"});
      cats.push_back({tr(K::ContinueWatching), "", {}, 0, "history"});
      cats.push_back({tr(K::WatchLater), "", {}, 0, "later"});
    }
    const K titles[] = {K::Music, K::Football, K::Games, K::Podcasts, K::News};
    const K queries[] = {K::MusicQuery, K::FootballQuery, K::GamesQuery, K::PodcastsQuery, K::NewsQuery};
    for (int i = 0; i < 5; i++) cats.push_back({tr(titles[i]), tr(queries[i]), {}});
    requestFeed(cats[0], 0);
    if (inResults) requestFeed(results, 1, false, 0, true);
  };
  rebuildCats();

  enum Screen { HOME, SEARCH, PLAYER, SETTINGS, NAV } screen = HOME;
  Screen navReturn = HOME, searchBack = HOME; bool searchMusic = false, navReturnRail = false;
  int zone = 0;  // 0 grade, 1 categorias
  float scroll = 0; static float selAnim[600] = {};
  bool railFocus = false; int railSel = 0; float railOpen = 0;
  bool phonePanel = false;
  int settingsRow = 0; bool loginPanel = false, logoutPanel = false;
  K loginError = K::None;
  Color ambient{0.15f, 0.15f, 0.2f, 1}, ambientWant = ambient;
  std::string query; std::vector<std::string> recent;
  int keyRow = 0, keyCol = 0; bool keyInRecent = false; int recentSel = 0;
  PlayerUI playerUI; uint64_t& overlayUntil = playerUI.until;
  Screen backTo = HOME; std::string toast; uint64_t toastUntil = 0;
  Video playing; bool playerFromRemote = false;
  cc::Loader captionLoader; cc::Data captions;
  int captionIndex = -1; std::vector<std::string> captionLines; float captionWidth = 0;
  float captionLineWidths[2]{};
  int lastQuality = -1, lastSourceFps = -1, lastTargetFps=-1, lastOutput=-1; const char* lastMode = nullptr;
  std::string qualityLabel, resolutionLabel;
  std::vector<yt::AudioTrack> audioTracks;
  std::vector<std::string> trackLabels;
  std::string selectedAudio, selectedTrackLabel;
  i18n::Lang trackLang=settings.lang;
  PlayerText overlayTitle, overlayMeta, overlayChapter, tipLabel, tipValue;
  std::string metaChannel, metaDate, clockLabel;
  i18n::Lang metaLang = settings.lang;
  int clockSecond = -1, clockDuration = -1;
  float clockWidth = 0;
  std::string captionVideo, captionInterface;
  // like da conta e o "a seguir" do vídeo aberto (/api/video/<id>/next); nextAt: contagem do próximo automático
  std::string likeState; Video upNext; bool ratePanel = false; int rateSel = 0; uint64_t nextAt = 0; bool endedSent = false; uint64_t nextDeadline = 0; bool nextPending = false;
  bool livePaused = false;
  int& quality = settings.quality;
  quality=720;
  bool& nestOut = settings.nest; int& nestOffsetMs = settings.syncMs;
  auto saveSettings = [&]() {
    if (!settings.save(assets)) { toast = tr(K::SaveFailed); toastUntil = SDL_GetTicks() + 4000; }
  };
  std::string nestName = pl.directSpeaker.configured() ? pl.directSpeaker.settings().name : tr(K::Nest);
  bool nestAvailable = pl.directSpeaker.configured(), nestChecked = nestAvailable;
  bool nestSkip = false;  // o Nest falhou neste vídeo: segue com o som da TV até trocar de vídeo ou apertar X
  std::string castCode, castWho;  // castWho: quem está conectado pelo Transmitir  // código de "Assistir na TV" do receptor (cast/receptor.mjs)
  if (nestOut && !pl.directSpeaker.configured()) control.add({5, "/api/nest/prepare", 0});
  auto requestAccount = [&](const char* path) {
    Job j{5, path, 0}; j.lang = settings.lang;
    control.add(j); accountBusy = true;
  };
  auto beginLogin = [&]() {
    loginPanel = true; loginError = K::None;
    if (account.state != Account::PENDING && !accountBusy) requestAccount("/api/account/login");
    else if (account.state == Account::PENDING) accountAt = 0;
  };

  SleepState sleep;
  uint64_t sleepAt = 0, shutdownAt = 0; int sleepPending = 0, sleepChoice = 0; uint64_t sleepRevision = 0;
  bool sleepPanel = false;
  const int sleepMinutes[] = {0, 15, 30, 45, 60, 90, 120};
  auto sleepLabel = [&](int choice) -> std::string {
    return choice == 0 ? tr(K::SleepOff) : choice == 7 ? tr(K::SleepEnd) : std::to_string(sleepMinutes[choice]) + " min";
  };
  auto requestSleep = [&](const std::string& path) { sleepWorker.add({5, path, ++sleepRevision}); sleepPending++; };
  auto applySleep = [&]() {
    std::string target = settings.sleepNotebook ? "xbox_notebook" : "xbox";
    requestSleep(sleepChoice == 0 ? "/api/sleep/cancel" : "/api/sleep/set?what=" + target +
        (sleepChoice == 7 ? "&end=1" : "&min=" + std::to_string(sleepMinutes[sleepChoice])));
  };

  Input in; SDL_Gamepad* pad = nullptr;
  bool quit = false; uint64_t frame = 0, runStart = SDL_GetTicks(); int shotFrame = std::getenv("XT_SHOT_FRAME") ? std::atoi(std::getenv("XT_SHOT_FRAME")) : 90;
#ifndef __XSDK__
  if (const char* s = std::getenv("XT_SCREEN")) {
    if (!std::strcmp(s, "search")) { screen = SEARCH; query = "flamengo 0x3 palmeiras"; recent = {"palmeiras 4x1 corinthians", "reações dos corintianos", "flamengo 0x3 palmeiras"}; }
    if (!std::strcmp(s, "rail")) { railFocus = true; }
    if (!std::strcmp(s, "chips")) { zone = 1; }
    if (!std::strcmp(s, "settings")) { screen = SETTINGS; }
  }
  if (g.win) SDL_StartTextInput(g.win);
#endif
  uint64_t historyAt = 0; std::string historyVideo; bool historyStarted = false;
  auto recordHistory = [&]() {
    if (!account.signedIn() || !consoleAccount->active() || historyVideo.empty() || !historyStarted) return;
    Job j{7, "/api/account/watched?id=" + historyVideo + "&pos=" + std::to_string(pl.position()) + "&progress=1", 0};
    control.add(j);
  };
  auto startVideo = [&](const Video& v, double t, bool fromRemote, std::string track = {}) {
    if (track.empty()) track=v.id==playing.id ? pl.audioSelection : settings.originalAudio ? "original" : std::string("lang:")+i18n::code(settings.lang);
    if (!fromRemote) phonePlaybackSerial = 0;
    if (historyVideo != v.id) { recordHistory(); historyVideo = v.id; historyStarted = false; historyAt = 0; }
    if (!fromRemote && nav.ownsQueue && !nav.localQueue) {
      nav.ownsQueue = nav.musicSession = nav.musicNow = false;
      navWorker.add({6, "/api/nav/transport?action=clear", ++nav.serial}, true);
    }
    if (v.id != playing.id) {
      nestSkip = false; likeState.clear(); upNext = Video{};
      if (!nav.localQueue) {
        Job next{5, "/api/video/" + v.id + "/next", 0};
        next.native = !fromRemote && !account.signedIn(); control.add(next, true); nextPending = true;
      } else nextPending = false;
    }
    if (v.id != playing.id) { captions = {}; captionIndex = -1; captionLines.clear(); captionVideo.clear(); captionLoader.cancel(); }
    if (v.id != playing.id) { audioTracks.clear(); trackLabels.clear(); selectedTrackLabel.clear(); playerUI.tracks=false; }
    nextAt = nextDeadline = 0; endedSent = false; ratePanel = sleepPanel = false; livePaused = false;
    playing = v; playerFromRemote = fromRemote;
    videoTex.ready = false;
    phonePanel = loginPanel = logoutPanel = false;
    pl.nestOffset = nestOffsetMs / 1000.0;
    if (screen != PLAYER) nav.musicNow = nav.musicSession;
    pl.audioOnly = nav.musicSession && nav.musicNow;
    pl.start(srv, v.id, t, v.title, v.dur, quality, nestOut && nestAvailable && !nestSkip, !fromRemote || !net::serverEnabled || phonePlaybackSerial != 0,false,settings.videoMode,track);
    if (screen != PLAYER) { backTo = screen; nav.musicNow = nav.musicSession; }
    screen = PLAYER; playerUI.reset(SDL_GetTicks());
  };
  auto stopVideo = [&]() {
    recordHistory(); historyVideo.clear(); historyStarted = false;
    if (nav.localQueue) { nav.clearLocalQueue(); nav.musicSession = nav.musicNow = false; }
    if (nav.ownsQueue && pl.state != player::ENDED) {
      nav.ownsQueue = nav.musicSession = nav.musicNow = false; nav.busy = false; nav.cancelQueue = true;
      nav.action(navWorker, "/api/nav/transport?action=stop");
    }
    pl.halt();
    if (videoTex.set) { gfx::videoDestroy(g, videoTex); videoTex = {}; }
    ratePanel = sleepPanel = false; playerUI.panel = PlayerUI::NONE; playerUI.options = false; nextAt = 0;
    screen = backTo == PLAYER ? HOME : backTo; };
  // Live pausada não continua de onde parou: o ffmpeg do servidor para junto, e a janela de algumas lives no YouTube
  // tem só 30 s. Continuar abre de novo perto do ao vivo.
  auto resumeLive = [&]() { livePaused = false; startVideo(playing, 0, playerFromRemote); };
  auto backspace = [&]() {
    if (query.empty()) return;
    size_t at = query.size() - 1;
    while (at > 0 && ((unsigned char)query[at] & 0xC0) == 0x80) at--;
    query.resize(at);
  };
  auto railPage = [&]() {
    if (screen == NAV && !nav.pages.empty()) {
      const auto& root = nav.pages.front();
      return root.music ? 4 : root.kind == "subscriptions" ? 2 : root.kind == "library" ? 3 : root.kind == "search" ? 1 : 0;
    }
    return screen == SETTINGS ? 6 : screen == SEARCH || inResults ? 1 : 0;
  };
  auto openNav = [&](const std::string& kind, const std::string& id = "", const std::string& q = "", bool music = false) {
    if (screen != NAV) { navReturn = screen == SEARCH ? searchBack : screen; navReturnRail = railFocus; nav.pages.clear(); }
    nav.open(navWorker, settings.lang, kind, id, q, music); screen = NAV; railFocus = false;
  };
  auto openChannelForVideo = [&](const std::string& id) {
    if (screen != NAV) { navReturn = screen; navReturnRail = railFocus; nav.pages.clear(); }
    nav.channelFromVideo(navWorker, settings.lang, id); screen = NAV; railFocus = false;
  };
  // "Ir para o canal" do player: fecha o vídeo e abre o canal; sem o UC... do canal, descobre pelo vídeo
  auto openPlayerChannel = [&](const std::string& channelId, const std::string& videoId) {
    stopVideo();
    if (!channelId.empty()) openNav("channel", channelId); else openChannelForVideo(videoId);
  };
  auto navBack = [&]() {
    if (!nav.pages.empty()) nav.pages.pop_back();
    if (nav.pages.empty()) { screen = navReturn; if (screen == SEARCH || screen == PLAYER) screen = HOME; railFocus = screen == HOME && navReturnRail; }
    else screen = NAV;
  };
  auto openSearch = [&]() {
    searchBack = screen; searchMusic = screen == NAV && nav.page().music;
    screen = SEARCH; keyRow = keyCol = 0; keyInRecent = false; railFocus = false;
  };
  auto doSearch = [&](std::string q) {
    if (q.empty()) return;
    query = q; recent.erase(std::remove(recent.begin(), recent.end(), q), recent.end()); recent.insert(recent.begin(), q);
    if (recent.size() > 6) recent.pop_back();
    if (screen == SEARCH) screen = searchBack;
    openNav(searchMusic ? "music_search" : "search", "", q, searchMusic);
  };
  auto goHome = [&]() { nav.pages.clear(); nav.musicSession = false; inResults = false; zone = 0; scroll = 0; railFocus = false; screen = HOME; };
  uint64_t musicTokenRevision = ~uint64_t(0);
  bool musicAccountActive = false;
  std::string musicAccess;
  auto syncMusicToken = [&]() {
    auto revision = consoleAccount->tokenRevision(); bool active = consoleAccount->active();
    if (revision == musicTokenRevision && active == musicAccountActive) return;
    musicTokenRevision = revision; musicAccountActive = active;
    auto access = active ? consoleAccount->accessToken() : std::string{};
    if (access == musicAccess) return;
    musicAccess = std::move(access); navWorker.setMusicToken(musicAccess);
    nav.busy = false; nav.actionStamp = ++nav.serial; nav.cancelQueue = false;
    if (!nav.pages.empty()) {
      auto page = nav.page(); nav.pages.clear();
      nav.open(navWorker, settings.lang, page.kind, page.id, page.query, page.music);
    }
  };
  auto accountPhoto = [&]() {
    if (!account.signedIn() || account.avatar.empty()) return;
    if (const auto* slot = thumbs.get(thumbsWorker, account.avatar, frame)) {
      gfx::rect(g, 1206, 24, 44, 44, {1,1,1,0.20f}, 22);
      gfx::image(g, slot->tex, 1208, 26, 40, 40, 20, {1,1,1,1}, (float)thumbs.height / thumbs.width);
    }
  };
  if (const char* pv = std::getenv("XT_PLAY")) { Video v; v.id = pv; startVideo(v, 0, false); }
#ifndef __XSDK__
  if (const char* q = std::getenv("XT_QUERY")) doSearch(q);
#endif
#ifndef __XSDK__
  if (const char* kind = std::getenv("XT_NAV")) {
    const char* id = std::getenv("XT_NAV_ID"), *q = std::getenv("XT_NAV_QUERY");
    openNav(kind, id ? id : "", q ? q : "", std::getenv("XT_MUSIC") != nullptr);
  }
#endif
  // autoplay.txt ao lado do .xex ("<id> [segundos]"): abre direto no player, para testar no console sem controle
  if (FILE* f = std::fopen((assets + "autoplay.txt").c_str(), "r")) {
    char id[32] = {0}; double t0 = 0;
    if (std::fscanf(f, "%31s %lf", id, &t0) >= 1) { Video v; v.id = id; LOG("autoplay %s em %.0f s", id, t0); startVideo(v, t0, false); }
    std::fclose(f);
  }

  Screen loggedScreen = HOME;
  const char* screenNames[] = {"Home", "Busca", "Player", "Ajustes", "Navegacao"};
  while (!quit) {
    uint64_t now = SDL_GetTicks();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) quit = true;
      if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) { pad = SDL_OpenGamepad(e.gdevice.which); if (pad) LOG("controle: %s", SDL_GetGamepadName(pad)); }
      if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && e.gdevice.which == SDL_GetGamepadID(pad)) { SDL_CloseGamepad(pad); pad = nullptr; }
#ifndef __XSDK__
      if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        bool d = e.type == SDL_EVENT_KEY_DOWN; SDL_Keycode k = e.key.key;
        int b = k == SDLK_UP ? Input::UP : k == SDLK_DOWN ? Input::DOWN : k == SDLK_LEFT ? Input::LEFT : k == SDLK_RIGHT ? Input::RIGHT
              : k == SDLK_RETURN ? Input::A : k == SDLK_ESCAPE ? Input::B : k == SDLK_BACKSPACE ? Input::X : k == SDLK_TAB ? Input::Y
              : k == SDLK_PAGEUP ? Input::LB : k == SDLK_PAGEDOWN ? Input::RB : k == SDLK_F10 ? Input::BACK : k == SDLK_F2 ? Input::START : -1;
        if (b >= 0) in.set(b, d, now);
      }
      if (e.type == SDL_EVENT_TEXT_INPUT && screen == SEARCH) query += e.text.text;
#endif
    }
    if (pad) {
      auto bt = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
      float lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f, ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.f;
      in.set(Input::UP, bt(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -0.6f, now); in.set(Input::DOWN, bt(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > 0.6f, now);
      in.set(Input::LEFT, bt(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -0.6f, now); in.set(Input::RIGHT, bt(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > 0.6f, now);
      in.set(Input::A, bt(SDL_GAMEPAD_BUTTON_SOUTH), now); in.set(Input::B, bt(SDL_GAMEPAD_BUTTON_EAST), now);
      in.set(Input::X, bt(SDL_GAMEPAD_BUTTON_WEST), now); in.set(Input::Y, bt(SDL_GAMEPAD_BUTTON_NORTH), now);
      in.set(Input::START, bt(SDL_GAMEPAD_BUTTON_START), now); in.set(Input::BACK, bt(SDL_GAMEPAD_BUTTON_BACK), now);
      in.set(Input::LB, bt(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), now); in.set(Input::RB, bt(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), now);
      in.set(Input::LT, SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 12000, now); in.set(Input::RT, SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 12000, now);
    }
    in.tick(now);
    if (shotPath) {
      in.clear();
      static const char* script = std::getenv("XT_KEYS");
      int step = (int)frame - 40;
#ifndef __XSDK__
      static int interval = std::getenv("XT_KEYS_MS") ? std::atoi(std::getenv("XT_KEYS_MS")) : 0;
      static int keyIndex = 0; static uint64_t keyAt = runStart + (uint64_t)std::max(0, interval);
      if (interval > 0) { step = -1; if (now >= keyAt) { step = keyIndex++ * 24; keyAt = now + interval; } }
#endif
      if (script && step >= 0 && step % 24 == 0 && step / 24 < (int)std::strlen(script)) {
        char k = script[step / 24];
        int b = k == 'U' ? Input::UP : k == 'D' ? Input::DOWN : k == 'L' ? Input::LEFT : k == 'R' ? Input::RIGHT : k == 'A' ? Input::A
              : k == 'B' ? Input::B : k == 'X' ? Input::X : k == 'Y' ? Input::Y : k == 'S' ? Input::START : k == '[' ? Input::LB : k == ']' ? Input::RB : k == 'l' ? Input::LT : k == 'r' ? Input::RT : -1;
        if (b >= 0) in.fired[b] = true;
      }
    }

    // ---------------------------------------------------------------- resultados do trabalho em segundo plano
    if (accountBackend != consoleAccount->revision()) {
      accountBackend = consoleAccount->revision();
      accountBusy = false; accountKnown = false; accountAt = 0; account = {}; rebuildCats();
      nav.pages.clear(); nav.queue.clear(); nav.busy = false;
      if (screen == NAV) screen = HOME;
    }
    syncMusicToken();
    Result r;
    while (thumbsWorker.pop(r)) if (r.kind == 2) thumbs.arrived(std::move(r));
    while (sleepWorker.pop(r)) {
      sleepPending--;
      if (r.feed.error == K::None && sleep.parse(r.feed.title, now)) {
        if (!sleepPanel && r.stamp == sleepRevision) {
          sleepChoice = 0;
          if (sleep.active) {
            sleepChoice = 7;
            if (!sleep.end) {
              sleepChoice = 6;
              for (int i = 1; i <= 6; i++) if (sleep.seconds <= sleepMinutes[i] * 60) { sleepChoice = i; break; }
            }
          }
        }
        LOG("timer: %s %.0f s %s", sleep.active ? "on" : "off", sleep.seconds, sleep.end ? "fim_do_video" : "minutos");
      } else if (r.key != "/api/sleep") { toast = tr(K::SleepError); toastUntil = now + 4000; }
      sleepAt = now + 5000;
    }
    if (net::serverEnabled && !sleepPending && now >= sleepAt && !shutdownAt) requestSleep("/api/sleep");
    double sleepLeft = sleep.remaining(now, sleep.end && pl.paused);
    bool sleepWarning = sleep.active && sleepLeft >= 0 && sleepLeft <= 60 && !shutdownAt;
    double gain = sleepWarning ? std::clamp(sleepLeft / 60, 0.0, 1.0) : 1.0;
    pl.sleepGain((float)gain * phoneGain);
    pl.directSpeaker.fade(gain);
    if (!sleep.active || !sleep.end) endTimerPending = false;
    while (control.pop(r)) {
      if (r.accountRoute && r.backend != consoleAccount->revision()) continue;
      if (r.key == "/api/nest/info") {
        if (pl.directSpeaker.configured()) continue;
        auto f = net::split(r.feed.title.substr(0, r.feed.title.find('\n')), '\t');
        nestChecked = true; nestAvailable = f.size() > 1 && f[0] == "ok" && !f[1].empty();
        if (nestAvailable) nestName = f[1];
      } else if (r.key.rfind("/api/video/", 0) == 0) {
        auto f = net::split(r.feed.title.substr(0, r.feed.title.find('\n')), '\t');
        bool ok = r.feed.error == K::None && !f.empty() && f[0] == "ok";
        std::string id = r.key.substr(11, 11);
        if (id == playing.id && r.key.ends_with("/next")) nextPending = false;
        if (r.key.find("/rate?") != std::string::npos) {
          if (ok && f.size() > 1 && id == playing.id) likeState = f[1];
          toast = tr(!ok ? K::RateFailed : likeState == "LIKE" ? K::Liked : likeState == "DISLIKE" ? K::Disliked : K::RatingRemoved);
          toastUntil = now + 3000;
        } else if (!nav.localQueue && r.feed.direct && id == playing.id && !r.feed.items.empty()) {
          upNext = r.feed.items.front();
        } else if (ok && id == playing.id && f.size() >= 5) {
          if (nav.localQueue) { likeState = f[1]; continue; }
          likeState = f[1];
          upNext = Video{};
          if (f[2].size() == 11) { upNext.id = f[2]; upNext.channel = f[3]; upNext.title = f[4]; }
        }
      } else if (r.key.rfind("/api/account", 0) == 0) {
        accountBusy = false;
        Account next; bool ok = r.feed.error == K::None && next.parse(r.feed.title);
        if (ok) {
          bool changed = account.signedIn() != next.signedIn();
          account = next; accountKnown = true; accountFailed = false;
          if (changed) {
            rebuildCats(); scroll = 0;
            if (accountKnown && !account.signedIn()) { nav.pages.clear(); nav.queue.clear(); if (screen == NAV) screen = HOME; }
          }
          if (account.signedIn()) {
            if (changed && loginPanel) { toast = tr(K::SignedInAs) + account.name; toastUntil = now + 4000; }
            loginPanel = false; loginError = K::None;
          } else if (loginPanel && account.state == Account::SIGNED_OUT) loginError = r.key == "/api/account/login" ? K::AccountError : K::LoginExpired;
          if (r.key == "/api/account/logout") { logoutPanel = false; toast = tr(K::SignedOutToast); toastUntil = now + 3000; }
          accountAt = now + (loginPanel && account.state == Account::PENDING ? 3000 : 60000);
          if (loginPanel && r.key == "/api/account/login" && account.state == Account::PENDING) accountAt = now;
        } else {
          accountFailed = true;
          if (loginPanel) loginError = K::AccountError;
          if (logoutPanel) { toast = tr(K::AccountError); toastUntil = now + 4000; }
          accountAt = now + (loginPanel && account.state == Account::PENDING ? 3000 : 60000);
        }
      }
    }
    syncMusicToken();
    while (navWorker.pop(r)) {
      if (r.accountRoute && r.backend != consoleAccount->revision()) continue;
      if (r.catalogRoute && (r.catalogBackend != navWorker.musicRevision.load() || r.backend != consoleAccount->revision())) continue;
      if (r.stamp == nav.actionStamp && nav.busy) {
        nav.busy = false;
        bool ok = r.feed.error == K::None && r.feed.title.rfind("ok", 0) == 0;
        if (r.key.rfind("/api/nav/play?", 0) == 0 && ok) {
          NavPage queue; Navigation::parse(r.feed.title, queue);
          if (nav.cancelQueue) {
            nav.musicSession = nav.musicNow = false;
            if (r.catalog) { nav.clearLocalQueue(); nav.cancelQueue = false; }
            else nav.action(navWorker, "/api/nav/transport?action=stop");
          }
          else {
            nav.queue = std::move(queue.items); nav.nowId.clear(); nav.ownsQueue = true;
            nav.localQueue = r.catalog || r.localAccount; nav.queueIndex = 0;
            if (nav.localQueue && !nav.queue.empty()) {
              for (size_t i = 0; i < nav.queue.size(); ++i) if (nav.queue[i].id == nav.queueStart) nav.queueIndex = i;
              startVideo(nav.queue[nav.queueIndex].video(),0,false);
            }
          }
          nav.nowSel = 0; nav.lyrics.clear(); nav.lyricsId.clear(); nav.lyricLines.clear(); nav.lyricScroll = 0;
        } else if (r.key.rfind("/api/nav/lyrics?", 0) == 0) {
          if (screen == PLAYER && r.key == "/api/nav/lyrics?id=" + playing.id) {
            auto fields = net::split(r.feed.title.substr(0, r.feed.title.find('\n')), '\t');
            nav.lyrics = fields.size() > 1 ? fields[1] : "";
            nav.lyricLines = nav.lyrics.empty() ? std::vector<std::string>{} : gfx::wrap(g, ui::F_KEY, nav.lyrics, 714, 200); nav.lyricScroll = 0;
          }
        } else if (r.key.find("action=subscribe") != std::string::npos && ok && !nav.pages.empty()) {
          for (auto& p : nav.pages) if (r.key.find("id=" + p.channelId + "&") != std::string::npos && !p.channelId.empty()) p.subscribed = r.key.find("value=1") != std::string::npos;
        } else if (r.key.find("action=later") != std::string::npos && ok) { toast = tr(K::AddedLater); toastUntil = now + 3500; nav.menu = false; }
        if (r.key == "/api/nav/transport?action=stop") { nav.cancelQueue = false; nav.ownsQueue = false; }
        if (!ok) {
          if (r.key.rfind("/api/nav/play?", 0) == 0) nav.musicSession = nav.musicNow = false;
          auto fields = net::split(r.feed.title.substr(0, r.feed.title.find('\n')), '\t');
          toast = fields.size() > 1 ? fields[1] : tr(K::ActionFailed); toastUntil = now + 4500;
        }
      } else {
        nav.arrived(r, g, settings.lang);
        if (screen == NAV && !nav.pages.empty() && nav.page().stamp == r.stamp && !nav.page().items.empty() && !nav.page().error.empty()) {
          toast = nav.page().error; toastUntil = now + 4500;
        }
      }
    }
    while (worker.pop(r)) {
      if (r.accountRoute && r.backend != consoleAccount->revision()) continue;
      Shelf* target = inResults && r.stamp == results.stamp ? &results : nullptr;
      for (auto& c : cats) if (r.stamp == c.stamp) target = &c;
      if (!target) continue;  // idioma, conta ou busca já mudou
      if (r.offset > 0) {
        target->feed.loadingMore = false;
        if (r.offset == (int)target->feed.items.size()) {
          if (r.feed.error == K::None) {
            target->feed.items.insert(target->feed.items.end(), r.feed.items.begin(), r.feed.items.end());
            target->feed.more = r.feed.items.size() >= 24;
          } else {
            target->feed.more = false; toast = tr(K::PageError); toastUntil = now + 4000;
          }
        }
      } else {
        if (!r.localAccount) r.feed.more = !r.feed.direct && r.feed.items.size() >= 24;
        target->feed = std::move(r.feed);
      }
    }
    thumbs.upload(g, frame, 2);

    // ---------------------------------------------------------------- comandos do celular
    SDL_LockMutex(remote.mu);
    std::deque<std::vector<std::string>> cmds; cmds.swap(remote.cmds);
    bool remoteOnline = remote.online;
    SDL_UnlockMutex(remote.mu);
    {
      phone.drain(cmds, phoneStatus);
      remoteOnline = remoteOnline || phoneStatus.online;
      if (castWho != phoneStatus.who) {
        toast = phoneStatus.who.empty() ? tr(K::PhoneDisconnected) : tr(K::PhoneConnected) + phoneStatus.who;
        toastUntil = now + 5000;
      }
      castCode = phoneStatus.code; castWho = phoneStatus.who;
    }
    bool shutdownCommand = std::any_of(cmds.begin(),cmds.end(),[](const auto& c) { return !c.empty() && c[0] == "desligar"; });
    bool phonePlay = std::any_of(cmds.begin(),cmds.end(),[](const auto& c) { return c.size() >= 2 && c[0] == "play" && !(c.size() > 4 && c[4] == "navigation"); });
    if (shutdownCommand && !shutdownAt) {
      LOG("timer: desligar recebido"); stopVideo();
      shutdownAt = SDL_GetTicks()+2000; sleepPanel = false;
    }
    for (auto& c : cmds) {
      if (c.empty()) continue;
      if (c[0] == "desligar" && !shutdownAt) {
        LOG("timer: desligar recebido"); stopVideo();
        shutdownAt = SDL_GetTicks() + 2000; sleepPanel = false;
      }
      if (shutdownAt) continue;
      if (c[0] == "aviso" && c.size() >= 2) {
        const std::string pre = i18n::get(i18n::Lang::PT, K::PhoneConnected), en = i18n::get(i18n::Lang::EN, K::PhoneConnected);
        if (c[1].rfind(pre, 0) == 0 || c[1].rfind(en, 0) == 0) {
          castWho = c[1].substr(c[1].rfind(pre, 0) == 0 ? pre.size() : en.size()); toast = tr(K::PhoneConnected) + castWho;
        } else if (c[1].find("desconectou") != std::string::npos || c[1].find("disconnected") != std::string::npos) {
          castWho.clear(); toast = tr(K::PhoneDisconnected);
        } else toast = tr(K::PhoneUpdated);
        toastUntil = now + 5000;
        continue;
      }
      if (c[0] == "volume" && c.size() >= 3) {
        double level = std::atof(c[1].c_str())/100;
        if (!std::isfinite(level)) continue;
        level = std::clamp(level,0.0,1.0);
        bool muted = c[2] == "1";
        phoneGain = muted ? 0 : level;
        pl.sleepGain((float)gain * phoneGain);
        pl.directSpeaker.volume(level,muted);
        if (nestOut && net::serverEnabled && !pl.directSpeaker.configured())
          control.add({5,"/api/nest/volume?v="+std::to_string(muted ? 0 : level*100),0});
        LOG("cast: volume %.0f, mudo %s", std::atof(c[1].c_str()), c[2].c_str());
        continue;
      }
      bool queueStart = c[0] == "play" && c.size() > 4 && c[4] == "navigation";
      if (queueStart && (nav.cancelQueue || phonePlay || phonePlaybackSerial)) continue;
      if (c[0] == "play" && c.size() >= 2) {
        if (endTimerPending) continue;
        Video v; v.id = c[1]; double t = c.size() > 2 ? std::atof(c[2].c_str()) : 0; v.title = c.size() > 3 ? c[3] : "";
        for (auto& c : cats) for (auto& it : c.feed.items) if (it.id == v.id) v = it;
        for (auto& it : results.feed.items) if (it.id == v.id) v = it;
        if (!queueStart) {
          bool serverQueue = (nav.ownsQueue && !nav.localQueue) || (nav.busy && nav.actionPath.rfind("/api/nav/play?",0) == 0);
          nav.clearLocalQueue(); nav.musicSession = nav.musicNow = false; nav.cancelQueue = true;
          nav.busy = false; nav.actionStamp = ++nav.serial;
          if (serverQueue) navWorker.add({6,"/api/nav/transport?action=clear",++nav.serial},true);
        }
        phonePlaybackSerial = c.size() > 5 && c[4] == "cast" ? std::strtoull(c[5].c_str(), nullptr, 10) : 0;
        startVideo(v, t, true);
        LOG("cast: play %s em %.2f, servidor %s", v.id.c_str(), t, net::serverEnabled ? "auto" : "off");
        if (!queueStart) { toast = tr(K::PhoneRequest); toastUntil = now + 2500; }
      } else if (screen == PLAYER) {
        if (c[0] == "pause") pl.setPaused(true);
        else if (c[0] == "resume") { if (pl.live && pl.paused) resumeLive(); else pl.setPaused(false); }
        else if (c[0] == "stop") stopVideo();
        else if (c[0] == "seek" && c.size() >= 2 && !pl.live && pl.state != player::CONNECTING) startVideo(playing, std::max(0.0, pl.position() + std::atof(c[1].c_str())), playerFromRemote);
        overlayUntil = now + 4000;
      }
    }

    if (screen == PLAYER && nav.musicSession) {
      nav.prepareNow(g, playing, settings.lang);
    }
    // ---------------------------------------------------------------- lógica por tela
    // código de pareamento e quem está conectado pelo Transmitir (o código troca a cada 5 min)
    if (net::serverEnabled && !pl.directSpeaker.configured() && now >= nestInfoAt) { control.add({5, "/api/nest/info", 0}); nestInfoAt = now + 60000; }
    if (!accountBusy && now >= accountAt) requestAccount("/api/account");
    const char* keys[] = {tr(K::Keyboard1), tr(K::Keyboard2), tr(K::Keyboard3), tr(K::Keyboard4)};
    bool behindLive = screen == PLAYER && pl.live && (pl.paused || (pl.shownFrames &&
        (pl.liveDebt >= 1.0 || (pl.lastFrameMs && now > pl.lastFrameMs + 1000))));
    int optionCount = behindLive ? PlayerUI::COUNT : PlayerUI::EDGE;
    if (playerUI.selected >= optionCount) playerUI.selected = PlayerUI::CHANNEL;
    if (shutdownAt) {
      if (now >= shutdownAt) {
        if (powerDownXbox()) quit = true;
        else { shutdownAt = 0; toast = tr(K::SleepError); toastUntil = now + 6000; }
      }
    } else if (sleepWarning) {
      if (in.hit(Input::A) && !sleepPending) requestSleep("/api/sleep/add?min=15");
      if (in.hit(Input::B) && !sleepPending) requestSleep("/api/sleep/cancel");
    } else if (sleepPanel) {
      if (in.hit(Input::UP)) playerUI.sleepRow = 0;
      if (in.hit(Input::DOWN)) playerUI.sleepRow = 1;
      int direction = in.hit(Input::LEFT) ? -1 : in.hit(Input::RIGHT) ? 1 : 0;
      if (!playerUI.sleepRow) sleepChoice = (sleepChoice + direction + 8) % 8;
      else if (direction || in.hit(Input::A)) {
        settings.sleepNotebook = !settings.sleepNotebook; saveSettings();
        if (sleep.active) requestSleep(std::string("/api/sleep/set?target_only=1&what=") + (settings.sleepNotebook ? "xbox_notebook" : "xbox"));
      }
      if (in.hit(Input::A) && !playerUI.sleepRow && !sleepPending) { applySleep(); sleepPanel = false; }
      if (in.hit(Input::B)) sleepPanel = false;
      playerUI.touch(now);
    } else if (ratePanel && (screen != PLAYER || pl.state == player::FAILED)) {
      ratePanel = false;  // o vídeo parou ou falhou com o painel aberto: o A não pode avaliar por trás da grade
    } else if (ratePanel) {
      if (in.hit(Input::LEFT) || in.hit(Input::RIGHT)) rateSel = 1 - rateSel;
      if (in.hit(Input::A)) {
        // apertar de novo o que já está marcado desfaz, como no app do YouTube
        bool same = likeState == (rateSel == 0 ? "LIKE" : "DISLIKE");
        control.add({5, "/api/video/" + playing.id + "/rate?r=" + (same ? "none" : rateSel == 0 ? "like" : "dislike"), 0});
        ratePanel = false;
      }
      if (in.hit(Input::B) || in.hit(Input::DOWN)) ratePanel = false;
      overlayUntil = now + 4000;
    } else if (screen == PLAYER && playerUI.panel != PlayerUI::NONE) {
      if (in.hit(Input::B)) playerUI.panel = PlayerUI::NONE;
      else if (playerUI.panel == PlayerUI::CAPTIONS) {
        if (in.hit(Input::UP)) playerUI.panelSel = 0;
        if (in.hit(Input::DOWN)) playerUI.panelSel = 1;
        int direction = in.hit(Input::LEFT) ? -1 : in.hit(Input::RIGHT) ? 1 : 0;
        if (captions.failed && in.hit(Input::A) && !captionLoader.loading)
          captionLoader.request(srv, playing.id, i18n::code(settings.lang), settings.captionLang, nullptr, pl.direct);
        else if (!playerUI.panelSel && !captions.cues.empty() && (direction || in.hit(Input::A))) {
          settings.captions = !settings.captions; saveSettings();
        } else if (playerUI.panelSel && !captions.tracks.empty() && !captionLoader.loading && (direction || in.hit(Input::A))) {
          int index = 0;
          for (size_t i = 0; i < captions.tracks.size(); i++) if (captions.tracks[i].code == captions.selected) index = (int)i;
          index = (index + (direction ? direction : 1) + (int)captions.tracks.size()) % (int)captions.tracks.size();
          settings.captionLang = captions.tracks[(size_t)index].code; settings.captions = true; saveSettings();
          captionLoader.request(srv, playing.id, i18n::code(settings.lang), settings.captionLang, nullptr, pl.direct);
          captions.cues.clear(); captionIndex = -1; captionLines.clear();
        }
      } else {
        int count = playerUI.panel == PlayerUI::QUALITY ? 3 : playerUI.panel==PlayerUI::TRACKS ? (int)audioTracks.size() : 2;
        if (in.hit(Input::UP) || in.hit(Input::LEFT)) playerUI.panelSel = (playerUI.panelSel + count - 1) % count;
        if (in.hit(Input::DOWN) || in.hit(Input::RIGHT)) playerUI.panelSel = (playerUI.panelSel + 1) % count;
        if (in.hit(Input::A)) {
          bool change = false;
          if (playerUI.panel == PlayerUI::QUALITY) {
            change = playerUI.panelSel != settings.videoMode; settings.videoMode=playerUI.panelSel;
          } else if (playerUI.panel==PlayerUI::TRACKS && playerUI.panelSel < (int)audioTracks.size()) {
            bool wasPaused=pl.paused;
            std::string track=audioTracks[playerUI.panelSel].code;
            double position=pl.position();
            startVideo(playing,pl.live ? 0 : position,playerFromRemote,track);
            if (wasPaused) pl.setPaused(true);
            LOG("audio: faixa %s em %.3f s",track.c_str(),position);
          } else if (!playerUI.panelSel || nestAvailable) {
            bool speaker = playerUI.panelSel == 1;
            change = speaker != pl.nest; nestOut = speaker; nestSkip = false;
          } else { toast = tr(K::NoSpeaker); toastUntil = now + 3000; }
          if (change) { startVideo(playing, pl.live ? 0 : pl.position(), playerFromRemote); saveSettings(); }
          playerUI.options = true; playerUI.panel = PlayerUI::NONE;
        }
      }
      playerUI.touch(now);
    } else if (screen == PLAYER && nextAt) {
      if (in.hit(Input::A) || in.hit(Input::START)) { Video v = upNext; startVideo(v, 0, false); }
      else if (in.hit(Input::B) || in.hit(Input::BACK)) { nextAt = 0; stopVideo(); }
    } else if (loginPanel) {
      if (in.hit(Input::B)) { loginPanel = false; accountAt = now + 60000; }
      if (in.hit(Input::A) && !accountBusy && loginError != K::None) {
        loginError = K::None;
        requestAccount(account.state == Account::PENDING ? "/api/account" : "/api/account/login");
      }
    } else if (logoutPanel) {
      if (in.hit(Input::B) && !accountBusy) logoutPanel = false;
      if (in.hit(Input::A) && !accountBusy) requestAccount("/api/account/logout");
    } else if (phonePanel) {
      if (in.hit(Input::A) || in.hit(Input::B)) phonePanel = false;
    } else if (nav.menu) {
      if (in.hit(Input::UP) && nav.menuSel > 0) nav.menuSel--;
      if (in.hit(Input::DOWN) && nav.menuSel < 2) nav.menuSel++;
      if (in.hit(Input::B)) nav.menu = false;
      if (in.hit(Input::A) && !nav.busy) {
        const auto item = nav.menuItem;
        if (nav.menuSel == 0) {
          nav.menu = false;
          if (!item.channelId.empty()) openNav("channel", item.channelId);
          else openChannelForVideo(item.id);
        } else if (nav.menuSel == 1) {
          if (!account.signedIn()) { toast = tr(K::AccountRequired); toastUntil = now + 4000; }
          else nav.action(navWorker, "/api/nav/action?action=later&id=" + item.id + "&lang=" + i18n::code(settings.lang));
        } else { nav.menu = false; openNav("radio", item.id, "", true); }
      }
    } else if (nav.unsubscribe) {
      if (in.hit(Input::B)) nav.unsubscribe = false;
      if (in.hit(Input::A) && !nav.busy) {
        nav.unsubscribe = false;
        nav.action(navWorker, "/api/nav/action?action=subscribe&id=" + nav.page().channelId + "&value=0&lang=" + i18n::code(settings.lang));
      }
    } else if (screen == NAV && !railFocus && !nav.pages.empty()) {
      auto& p = nav.page(); uint64_t currentPage = p.stamp;
      bool channel = p.kind == "channel";
      bool playlist = p.kind == "playlist" || p.kind == "music_playlist" || p.kind == "album" || p.kind == "radio" || p.kind == "later";
      bool tabs = channel || p.kind == "music" || p.kind == "search" || p.kind == "music_search";
      int count = (int)p.items.size();
      if (count) p.sel = std::clamp(p.sel, 0, count - 1);
      bool left = in.hit(Input::LEFT), right = in.hit(Input::RIGHT), up = in.hit(Input::UP), down = in.hit(Input::DOWN), a = in.hit(Input::A);
      bool firstColumn = p.musicHome() && !p.shelves.empty() ? p.sel == p.shelves[p.shelf].first : p.sel % 4 == 0;
      if (left && (p.zone == 0 || (p.zone == 1 && p.tabSel == 0) || (p.zone == 2 && p.channelSel == 0) || (p.zone == 3 && firstColumn))) {
        railFocus = true; railSel = railPage();
      } else if (p.zone == 0) {
        if (down) p.zone = tabs ? 1 : !p.channels.empty() ? 2 : 3;
        if (a && !p.loading && !nav.busy) {
          if (channel) {
            if (!account.signedIn()) { toast = tr(K::AccountRequired); toastUntil = now + 4000; }
            else if (p.subscribed) nav.unsubscribe = true;
            else nav.action(navWorker, "/api/nav/action?action=subscribe&id=" + p.channelId + "&value=1&lang=" + i18n::code(settings.lang));
          } else if (playlist) {
            phonePlaybackSerial = 0;
            nav.queueStart.clear();
            nav.cancelQueue = false; nav.musicSession = p.music; nav.musicNow = false;
            nav.action(navWorker, "/api/nav/play?kind=" + p.kind + "&id=" + net::urlEncode(p.id) + "&lang=" + i18n::code(settings.lang));
          }
        }
      } else if (p.zone == 1) {
        int maxTab = p.kind == "music_search" ? 3 : 2, before = p.tabSel;
        if (left && p.tabSel > 0) p.tabSel--;
        if (right && p.tabSel < maxTab) p.tabSel++;
        if (up && (channel || playlist)) p.zone = 0;
        if (down || a) p.zone = 3;
        if (p.tabSel != before) {
          p.tab = p.tabSel == 1 ? "live" : p.tabSel == 2 ? "playlists" : "videos";
          if (p.kind == "music") p.query = p.tabSel == 1 ? "charts" : p.tabSel == 2 ? "mixes" : "";
          p.sel = 0; p.scroll = 0; nav.load(navWorker, settings.lang);
        }
      } else if (p.zone == 2) {
        if (left && p.channelSel > 0) p.channelSel--;
        if (right && p.channelSel + 1 < (int)p.channels.size()) p.channelSel++;
        if (down) p.zone = 3;
        if (a && !p.channels.empty()) { const auto id = p.channels[(size_t)p.channelSel].id; openNav("channel", id); }
      } else {
        if (p.musicHome() && !p.shelves.empty()) {
          auto& shelf = p.shelves[p.shelf];
          if (left && p.sel > shelf.first) shelf.selected--;
          if (right && p.sel + 1 < shelf.end) shelf.selected++;
          if (up && p.shelf == 0) p.zone = 1;
          else if (up) p.shelf--;
          if (down && p.shelf + 1 < (int)p.shelves.size()) p.shelf++;
          p.sel = p.shelves[p.shelf].selected;
        } else {
          if (left && p.sel % 4 > 0) p.sel--;
          if (right && p.sel % 4 < 3 && p.sel + 1 < count) p.sel++;
          if (up) { if (p.sel >= 4) p.sel -= 4; else p.zone = !p.channels.empty() ? 2 : tabs ? 1 : channel || playlist ? 0 : 3; }
          if (down) { if (p.sel + 4 < count) p.sel += 4; else if (count) p.sel = count - 1; }
        }
        if (a && count && !p.loading) {
          const auto item = p.items[(size_t)p.sel];
          if (item.type == "video") {
            if (playlist && nav.nativePage()) {
              phonePlaybackSerial = 0;
              nav.queueStart = item.id; nav.cancelQueue = false; nav.musicSession = p.music; nav.musicNow = false;
              nav.action(navWorker,"/api/nav/play?kind="+p.kind+"&id="+net::urlEncode(p.id)+"&lang="+i18n::code(settings.lang));
            } else if (p.music) { openNav("radio", item.id, "", true); nav.page().zone = 0; }
            else startVideo(item.video(), 0, false);
          } else if (item.type == "mixes") openNav("music", "", "mixes", true);
          else if (item.type == "history" || item.type == "later") openNav(item.type, "", "", p.music);
          else openNav(item.type == "playlist" && p.music ? "music_playlist" : item.type, item.id, "", p.music);
        }
        if (screen == NAV && nav.page().stamp == currentPage && in.hit(Input::START) && count && p.items[(size_t)p.sel].type == "video") { nav.menuItem = p.items[(size_t)p.sel]; nav.menuSel = 0; nav.menu = true; }
      }
      if (screen == NAV && nav.page().stamp == currentPage && p.more && !p.loading) {
        bool needPage = p.sel >= count - 8 || (p.zone == 2 && p.channelSel >= (int)p.channels.size() - 4);
        if (p.musicHome()) needPage |= p.shelf + 3 >= (int)p.shelves.size();
        if (needPage) nav.load(navWorker, settings.lang, false, true);
      }
      if (screen == NAV && nav.page().stamp == currentPage && in.hit(Input::X)) { nav.page().sel = 0; nav.page().scroll = 0; nav.load(navWorker, settings.lang, true); }
      if (in.hit(Input::Y)) openSearch();
      if (in.hit(Input::B)) {
        if (nav.busy && nav.actionPath.rfind("/api/nav/play?", 0) == 0) nav.cancelQueue = true;
        navBack();
      }
    } else if (screen == SETTINGS) {
      if (in.hit(Input::UP) && settingsRow > 0) settingsRow--;
      if (in.hit(Input::DOWN) && settingsRow < 10) settingsRow++;
      int direction = 0;
      if (in.hit(Input::LEFT)) direction = -1;
      if (in.hit(Input::RIGHT)) direction = 1;
      if (in.hit(Input::A) && settingsRow == 0 && !accountBusy) {
        if (account.signedIn()) logoutPanel = true;
        else beginLogin();
      }
      if (direction && settingsRow > 0) {
        bool changed = true;
        if (settingsRow == 1) {
          settings.cols = settings.cols == 4 ? 3 : 4;
          thumbs.configure(g, thumbsWorker, settings.cols); scroll = 0;
        } else if (settingsRow == 2) {
          settings.videoMode=(settings.videoMode+direction+playback::COUNT)%playback::COUNT;
        } else if (settingsRow == 3) {
          if (nestAvailable) { nestOut = !nestOut; if (nestOut && !pl.directSpeaker.configured()) control.add({5, "/api/nest/prepare", 0}); }
          else changed = false;
        } else if (settingsRow == 4) nestOffsetMs = std::clamp(nestOffsetMs + direction * 40, -2000, 2000);
        else if (settingsRow == 5) {
          settings.lang = settings.lang == i18n::Lang::EN ? i18n::Lang::PT : i18n::Lang::EN;
          rebuildCats(); scroll = 0; toast.clear();
        } else if (settingsRow == 6) { sleepChoice = (sleepChoice + direction + 8) % 8; applySleep(); changed = false; }
        else if (settingsRow == 7) {
          settings.sleepNotebook = !settings.sleepNotebook;
          if (sleep.active) {
            requestSleep(std::string("/api/sleep/set?what=") + (settings.sleepNotebook ? "xbox_notebook" : "xbox") + "&target_only=1");
          }
        }
        else if (settingsRow == 8) {
          settings.server = !settings.server; net::serverEnabled = settings.server;
          consoleAccount->select(!settings.server); account = {}; accountKnown = false;
          if (!settings.server) {
            account = {}; accountKnown = false; nestAvailable = pl.directSpeaker.configured(); sleep = {};
            SDL_LockMutex(remote.mu); remote.online = false; remote.cmds.clear(); remote.ended.clear(); SDL_UnlockMutex(remote.mu);
          }
          accountAt = nestInfoAt = 0; rebuildCats();
        }
        else if (settingsRow == 9) {
          settings.outputHeight=settings.outputHeight==1080 ? 720 : 1080;
          gfx::output(g,settings.outputHeight);
          if (!gfx::loadFonts(g,assets,fontSpecs)) { LOG("fontes não carregaram"); quit=true; }
          overlayTitle = {}; overlayMeta = {}; overlayChapter = {}; tipLabel = {}; tipValue = {};
          metaChannel.clear(); clockSecond = -1;
          if ((int)g.extent.height != settings.outputHeight) { toast=tr(K::Display1080Help); toastUntil=now+6000; }
        }
        else if (settingsRow == 10) settings.originalAudio=!settings.originalAudio;
        if (changed) saveSettings();
      }
      if (in.hit(Input::B)) { screen = HOME; railFocus = true; railSel = 6; }
    } else if ((screen == HOME || screen == NAV || screen == SEARCH) && railFocus) {
      if (in.hit(Input::UP) && railSel > 0) railSel--;
      if (in.hit(Input::DOWN) && railSel < 7) railSel++;
      if (in.hit(Input::RIGHT) || in.hit(Input::B)) railFocus = false;
      if (in.hit(Input::A)) {
        if (screen != HOME && railSel >= 2 && railSel <= 4) { goHome(); railFocus = true; }
        if (railSel == 0) goHome();
        else if (railSel == 1) { if (screen == SEARCH) railFocus = false; else openSearch(); }
        else if (railSel == 2) openNav("subscriptions");
        else if (railSel == 3) openNav("library");
        else if (railSel == 4) openNav("music", "", "", true);
        else if (railSel == 5) { phonePanel = true; }
        else if (railSel == 6) { screen = SETTINGS; railFocus = false; nestInfoAt = 0; accountAt = 0; }
        else quit = true;
      }
    } else if (screen == HOME) {
      Shelf& cur = inResults ? results : cats[(size_t)cat];
      if (!cur.feed.items.empty()) cur.sel = std::clamp(cur.sel, 0, (int)cur.feed.items.size() - 1);
      int n = (int)cur.feed.items.size(), previousSel = cur.sel;
      if (zone == 1 && !inResults) {
        if (in.hit(Input::LEFT)) { if (cat > 0) { cat--; scroll = 0; } else { railFocus = true; railSel = 0; } }
        if (in.hit(Input::RIGHT) && cat + 1 < (int)cats.size()) { cat++; scroll = 0; }
        if (!cats[cat].stamp) requestFeed(cats[cat], cat == 0 ? 0 : 3);
        if (cur.kind == "subs" && (in.hit(Input::DOWN) || in.hit(Input::A))) openNav("subscriptions");
        else if ((in.hit(Input::DOWN) || in.hit(Input::A)) && n) zone = 0;
      } else {
        int cols = settings.cols;
        if (in.hit(Input::LEFT)) { if (cur.sel % cols > 0) cur.sel--; else { railFocus = true; railSel = inResults ? 1 : 0; } }
        if (in.hit(Input::RIGHT) && cur.sel % cols < cols - 1 && cur.sel + 1 < n) cur.sel++;
        if (in.hit(Input::DOWN)) { if (cur.sel + cols < n) cur.sel += cols; else if (n > 0 && cur.sel / cols < (n - 1) / cols) cur.sel = n - 1; }
        // rolagem sem fim: perto do fim da grade, pede a próxima página
        if (n > 0 && cur.sel >= n - 8 && cur.feed.more && !cur.feed.loadingMore && !cur.feed.loading) {
          cur.feed.loadingMore = true;
          requestFeed(cur, inResults ? 1 : cat == 0 ? 0 : 3, false, n, true);
        }
        if (in.hit(Input::UP)) { if (cur.sel >= cols) cur.sel -= cols; else if (!inResults) zone = 1; }
        if (in.hit(Input::A) && n) {
          const Video& v = cur.feed.items[(size_t)cur.sel];
          // Um item já concluído recomeça; pedir o fim exato devolve HTTP 416.
          startVideo(v, v.dur > 0 && v.pos >= v.dur ? 0 : v.pos, false);
        }
      }
      if (cur.sel != previousSel) LOG("home: categoria %d, foco %d/%d", cat, cur.sel + 1, n);
      if (in.hit(Input::START) && n && zone == 0) { const auto& v = cur.feed.items[(size_t)cur.sel]; nav.menuItem = {"video",v.id,v.title,v.channel,v.id,"","",v.dur}; nav.menuSel = 0; nav.menu = true; }
      if (in.hit(Input::Y)) openSearch();
      if (in.hit(Input::X)) {
        cur.feed = Feed{}; requestFeed(cur, inResults ? 1 : cat == 0 ? 0 : 3, true, 0, true);
        cur.sel = 0; scroll = 0; zone = 0; toast = tr(K::Refreshing); toastUntil = now + 2500;
      }
      if (in.hit(Input::B)) { if (inResults) goHome(); else if (zone == 0) zone = 1; }
      if (in.hit(Input::BACK)) quit = true;
    } else if (screen == SEARCH) {
      if (keyInRecent) {
        if (in.hit(Input::UP) && recentSel > 0) recentSel--;
        if (in.hit(Input::DOWN) && recentSel + 1 < (int)recent.size()) recentSel++;
        if (in.hit(Input::LEFT)) keyInRecent = false;
        if (in.hit(Input::A) && recentSel < (int)recent.size()) doSearch(recent[(size_t)recentSel]);
      } else {
        int cols = keyRow < 4 ? 10 : 3;
        if (in.hit(Input::UP) && keyRow > 0) { if (keyRow == 4) keyCol = keyCol * 3 + 1; keyRow--; }
        if (in.hit(Input::DOWN) && keyRow < 4) { keyRow++; if (keyRow == 4) keyCol = std::min(2, keyCol / 4); }
        if (in.hit(Input::LEFT)) { if (keyCol > 0) keyCol--; else { railFocus = true; railSel = 1; } }
        if (in.hit(Input::RIGHT)) { if (keyCol + 1 < cols) keyCol++; else if (!recent.empty()) { keyInRecent = true; recentSel = 0; } }
        if (in.hit(Input::A)) {
          if (keyRow < 4) {
            const char* row = keys[keyRow]; int ci = 0; size_t i = 0;
            while (row[i]) { size_t st = i; gfx::utf8(row, i); if (ci == keyCol) { query.append(row + st, i - st); break; } ci++; }
          } else if (keyCol == 0) query += ' ';
          else if (keyCol == 1) backspace();
          else doSearch(query);
        }
      }
      if (in.hit(Input::X)) backspace();
      if (in.hit(Input::Y)) query += ' ';
      if (in.hit(Input::START)) doSearch(query);
      if (in.hit(Input::B)) screen = searchBack;
    } else if (screen == PLAYER && nav.musicNow) {
      if (pl.state == player::FAILED && pl.nestFailed && !nestSkip) {
        nestSkip = true; toast = tr(K::MusicSpeakerFallback); toastUntil = now + 5000;
        startVideo(playing, pl.position(), playerFromRemote);
      }
      if (in.hit(Input::UP) && nav.lyricsOpen && nav.lyricScroll > 0) nav.lyricScroll--;
      if (in.hit(Input::DOWN) && nav.lyricsOpen && nav.lyricScroll + 9 < (int)nav.lyricLines.size()) nav.lyricScroll++;
      if (in.hit(Input::LEFT) && nav.nowSel > 0) nav.nowSel--;
      if (in.hit(Input::RIGHT) && nav.nowSel < 3) nav.nowSel++;
      if (in.hit(Input::A)) {
        if (nav.nowSel == 0 && (!pl.waiting() || pl.paused)) pl.setPaused(!pl.paused);
        else if (nav.nowSel == 1 && !nav.busy) {
          if (nav.localQueue) {
            if (nav.queueIndex+1 < nav.queue.size()) { ++nav.queueIndex; startVideo(nav.queue[nav.queueIndex].video(),0,false); }
            else stopVideo();
          } else nav.action(navWorker, "/api/nav/transport?action=next");
        }
        else if (nav.nowSel == 2) {
          nav.lyricsOpen = !nav.lyricsOpen;
          if (nav.lyricsOpen && nav.lyricsId != playing.id && !nav.busy) { nav.lyricsId = playing.id; nav.lyrics.clear(); nav.lyricLines.clear(); nav.action(navWorker, "/api/nav/lyrics?id=" + playing.id); }
        } else if (nav.nowSel == 3) {
          double pos = pl.position(); bool paused = pl.paused;
          nav.musicNow = false; startVideo(playing,pos,playerFromRemote); pl.setPaused(paused);
        }
      }
      if (in.hit(Input::B)) {
        if (nav.localQueue) stopVideo();
        else {
          nav.ownsQueue = nav.musicSession = nav.musicNow = false; nav.busy = false; nav.cancelQueue = true;
          nav.action(navWorker, "/api/nav/transport?action=stop"); stopVideo();
        }
      }
    } else if (screen == PLAYER) {
      bool any = false;
      int horizontal = in.hit(Input::LEFT) ? -1 : in.hit(Input::RIGHT) ? 1 : 0;
      // Na opção 1 aprovada os ícones ficam ABAIXO da barra: ↓ leva aos ícones, ↑ volta para a trilha.
      int vertical = in.hit(Input::DOWN) ? 1 : in.hit(Input::UP) ? -1 : 0;
      double jump = 0;
      if (horizontal || vertical) { jump = playerUI.direction(now, horizontal, vertical, optionCount) * 10; any = true; }
      if (in.hit(Input::LB)) jump = -60;
      if (in.hit(Input::RB)) jump = 60;
      if (jump != 0) {
        if (pl.state == player::CONNECTING) {
          LOG("player: seek ignored: live status unknown (CONNECTING)");
          toast = tr(K::PlayerSeekWait); toastUntil = now + 2500;
        } else if (pl.live) {
          LOG("player: seek ignored: live");
          toast = tr(K::PlayerLiveSeek); toastUntil = now + 2500;
        } else if (pl.state != player::FAILED) {
          double to = std::clamp(pl.position() + jump, 0.0, pl.duration > 0 ? std::max(0.0, pl.duration - 3) : 1e9);
          startVideo(playing, to, playerFromRemote);
        }
        any = true;
      }
      bool pressA = in.hit(Input::A), open = pressA && playerUI.options;
      if (open && playerUI.selected != PlayerUI::TRANSPORT) {
        playerUI.panelSel = 0;
        if (playerUI.selected == PlayerUI::TIMER) { sleepPanel = true; playerUI.sleepRow = 0; }
        else if (playerUI.selected == PlayerUI::RATE || playerUI.selected == PlayerUI::DISLIKE) {
          if (account.signedIn()) {
            const char* want = playerUI.selected == PlayerUI::RATE ? "LIKE" : "DISLIKE";
            bool same = likeState == want;
            control.add({5, "/api/video/" + playing.id + "/rate?r=" + (same ? "none" : playerUI.selected == PlayerUI::RATE ? "like" : "dislike"), 0});
          } else { toast = tr(K::RateSignIn); toastUntil = now + 3500; }
        } else if (playerUI.selected == PlayerUI::CC) {
          if (captions.failed || captionLoader.loading || (!captions.live && !captions.tracks.empty())) playerUI.panel = PlayerUI::CAPTIONS;
        } else if (playerUI.selected == PlayerUI::VIDEO) {
          playerUI.panel = PlayerUI::QUALITY; playerUI.panelSel = settings.videoMode;
        } else if (playerUI.selected == PlayerUI::AUDIO) {
          playerUI.panel = PlayerUI::SOUND; playerUI.panelSel = pl.nest ? 1 : 0;
        } else if (playerUI.selected == PlayerUI::TRACK && audioTracks.size()>1) {
          playerUI.panel=PlayerUI::TRACKS;
          for (int i=0;i<(int)audioTracks.size();++i) if (audioTracks[i].code==selectedAudio) playerUI.panelSel=i;
        } else if (playerUI.selected == PlayerUI::EDGE) {
          LOG("live: retorno manual à borda"); resumeLive();
        } else openPlayerChannel(captions.channelId, playing.id);
        any = true;
      }
      // Durante a abertura, A não cria uma pausa invisível atrás do carregamento.
      if (((pressA && (!open || playerUI.selected == PlayerUI::TRANSPORT)) || in.hit(Input::START)) && (!pl.waiting() || pl.paused)) {
        if (pl.live && pl.paused) resumeLive(); else pl.setPaused(!pl.paused);
        LOG("player: pause=%d pos=%.2f", pl.paused ? 1 : 0, pl.position());
        any = true;
      }
      if (in.hit(Input::Y) && pl.state != player::FAILED) {
        bool row = playerUI.options;
        settings.videoMode=(settings.videoMode+1)%playback::COUNT;
        startVideo(playing, pl.position(), playerFromRemote); playerUI.options = row;
        toast = std::string(tr(K::QualityPrefix)) + tr(videoModeLabel(settings.videoMode)); toastUntil = now + 2500; saveSettings(); any = true;
      }
      if (in.hit(Input::X)) {
        if (!nestAvailable) toast = tr(K::NoSpeaker);
        else {
          bool row = playerUI.options;
          if (nestOut && (nestSkip || (pl.speakerRequested && !pl.nest))) nestSkip = false;
          else nestOut = !nestOut;
          startVideo(playing, pl.position(), playerFromRemote); playerUI.options = row;
          toast = tr(K::SoundPrefix) + (nestOut ? nestName : std::string(tr(K::TV)));
          saveSettings();
        }
        toastUntil = now + 3000; any = true;
      }
      bool lt = in.hit(Input::LT), rt = in.hit(Input::RT);
      if (nestOut && (lt || rt)) {
        nestOffsetMs = std::clamp(nestOffsetMs + (rt ? 40 : -40), -2000, 2000); pl.nestOffset = nestOffsetMs / 1000.0;
        toast = tr(K::SyncPrefix) + std::to_string(nestOffsetMs) + tr(K::Milliseconds);
        toastUntil = now + 2500; saveSettings(); any = true;
      }
      if (any) playerUI.touch(now);
      if (in.hit(Input::BACK)) stopVideo();
      else if (in.hit(Input::B) && !playerUI.back(now)) {
        if (nav.musicSession) {
          double pos = pl.position(); bool paused = pl.paused;
          nav.musicNow = true; startVideo(playing,pos,playerFromRemote); pl.setPaused(paused);
        } else stopVideo();
      }
      if (pl.state == player::FAILED && pl.nestFailed && !nestSkip) {
        nestSkip = true;
        toast = tr(K::SpeakerFallback); toastUntil = now + 5000;
        startVideo(playing, pl.position(), playerFromRemote);
      }

    }
    if (screen == PLAYER) {
      if (pl.speakerFallback.exchange(false)) {
        toast = tr(nav.musicNow ? K::MusicSpeakerFallback : K::SpeakerFallback); toastUntil = now + 5000;
      }
      bool newTracks=pl.tracksReady.exchange(false);
      if (newTracks) { audioTracks=pl.audioTracks; selectedAudio=pl.selectedAudio; playerUI.tracks=audioTracks.size()>1; }
      if (newTracks || trackLang!=settings.lang) {
        trackLabels.clear(); selectedTrackLabel.clear(); trackLang=settings.lang;
        for (const auto& track : audioTracks) { trackLabels.push_back(audioTrackLabel(track,settings.lang)); if (track.code==selectedAudio) selectedTrackLabel=trackLabels.back(); }
      }
      if (pl.direct && pl.mediaReady.exchange(false)) {
        if (pl.title.empty()) pl.title = pl.media.title;
        pl.duration = pl.media.duration;
        if (!pl.playingAudio && (captionVideo != playing.id || captionInterface != i18n::code(settings.lang) || !captionLoader.native)) {
          captionVideo = playing.id; captionInterface = i18n::code(settings.lang);
          captions = {}; captionIndex = -1; captionLines.clear();
          captionLoader.request(srv, playing.id, i18n::code(settings.lang), settings.captionLang, &pl.media);
        }
      } else if (!pl.playingAudio && !pl.direct && (captionVideo != playing.id || captionInterface != i18n::code(settings.lang) || captionLoader.native)) {
        captionVideo = playing.id; captionInterface = i18n::code(settings.lang);
        captions = {}; captionIndex = -1; captionLines.clear();
        captionLoader.request(srv, playing.id, i18n::code(settings.lang), settings.captionLang);
      }
      if (captionLoader.pop(captions)) {
        captionIndex = -1; captionLines.clear();
        LOG("legendas: %s %s, %u frases, %u idiomas", captionVideo.c_str(), captions.selected.c_str(), (unsigned)captions.cues.size(), (unsigned)captions.tracks.size());
      }

    }
    // Live retomada pelo próprio Nest (toque em cima, Google Home) também volta para o ao vivo.
    if (screen == PLAYER && pl.live) { if (pl.paused) livePaused = true; else if (livePaused) resumeLive(); }
    else livePaused = false;
    if (screen == PLAYER && account.signedIn() && consoleAccount->active() &&
        (pl.shownFrames > 0 || (pl.playingAudio && pl.firstPts >= 0)) && (!historyStarted || now >= historyAt)) {
      Job j{7, "/api/account/watched?id=" + playing.id + "&pos=" + std::to_string(pl.position()) +
          "&progress=" + (historyStarted ? "1" : "0"), 0};
      control.add(j); historyStarted = true; historyAt = now + 30000;
    }
    // Fim natural continua sendo informado mesmo com um cartão aberto.
    if (screen == PLAYER && nav.localQueue && pl.state == player::ENDED) {
      SDL_LockMutex(remote.mu); remote.ended = playing.id; SDL_UnlockMutex(remote.mu);
      if (nav.queueIndex+1 < nav.queue.size() && !(sleep.active && sleep.end)) {
        ++nav.queueIndex; startVideo(nav.queue[nav.queueIndex].video(),0,false);
      } else { endTimerPending = sleep.active && sleep.end; stopVideo(); }
    }
    if (screen == PLAYER && pl.state == player::ENDED && !nextDeadline) nextDeadline = now + 8000;
    if (screen == PLAYER && pl.state == player::ENDED && !shutdownAt && (playerFromRemote || !nextPending || now >= nextDeadline)) {
      if (!endedSent) {
        SDL_LockMutex(remote.mu); remote.ended = playing.id; SDL_UnlockMutex(remote.mu);
        endTimerPending = sleep.active && sleep.end;
        if (endTimerPending) LOG("timer: fim de video, aguardando desligar");
        if (phonePlaybackSerial && playerFromRemote && !endTimerPending) phone.publish({playing.id, pl.position(), pl.duration, 4, phonePlaybackSerial}, playing.id);
        endedSent = true; ratePanel = sleepPanel = false; playerUI.panel = PlayerUI::NONE;
        // Pedido pelo celular: o app do celular escolhe o próximo. Timer "no fim do vídeo": desliga, não emenda.
        if (!playerFromRemote && !upNext.id.empty() && !(sleep.active && sleep.end)) nextAt = now + 8000;
        else stopVideo();
      } else if (nextAt && now >= nextAt) { Video v = upNext; startVideo(v, 0, false); }
    }
    if (screen != loggedScreen) {
      LOG("navegacao: %s -> %s", screenNames[loggedScreen], screenNames[screen]);
      loggedScreen = screen;
    }
    in.clear();
    Shelf& cur = inResults ? results : cats[(size_t)cat];
    const Video* focusVid = nullptr;
    if (!cur.feed.items.empty()) { cur.sel = std::clamp(cur.sel, 0, (int)cur.feed.items.size() - 1); focusVid = &cur.feed.items[(size_t)cur.sel]; }

    SDL_LockMutex(remote.mu);
    remote.playing = screen == PLAYER ? playing.id : ""; remote.pos = pl.position(); remote.dur = pl.duration; remote.paused = pl.paused;
    SDL_UnlockMutex(remote.mu);
    if (now >= phoneStateAt) {
      int state = screen != PLAYER ? -1 : (pl.state == player::ENDED || pl.state == player::FAILED) ? 4 : pl.paused ? 2 : pl.state == player::PLAYING ? 1 : 3;
      auto speaker = pl.directSpeaker.sample();
      bool speakerVolume = nestOut && pl.directSpeaker.configured() && speaker.volume >= 0;
      phone.publish({screen == PLAYER ? playing.id : "", pl.position(), pl.duration, state, phonePlaybackSerial,
                     speakerVolume ? speaker.volume*100 : 100, speakerVolume && speaker.muted});
      phoneStateAt = now + 250;
    }

    if (focusVid && screen == HOME) { if (const Thumbs::Slot* s = thumbs.get(thumbsWorker, focusVid->id, frame, cur.feed.direct)) ambientWant = s->avg; }
    ambient = ui::mix(ambient, ambientWant, 0.06f);
    railOpen += ((railFocus ? 1.f : 0.f) - railOpen) * 0.25f;

    // ---------------------------------------------------------------- quadro do vídeo
    thumbs.configure(g, thumbsWorker, settings.cols, screen == PLAYER && nav.musicNow,
                     screen == NAV && !nav.pages.empty() && !nav.page().banner.empty());
    gfx::begin(g);
    gfx::rect(g, 0, 0, ui::W, ui::H, ui::BG);
    if (screen == PLAYER) {
      if (player::Frame* f = pl.take()) {
        if (f->w != videoTex.w || f->h != videoTex.h) { if (videoTex.set) gfx::videoDestroy(g, videoTex); gfx::videoCreate(g, videoTex, f->w, f->h); LOG("vídeo %dx%d", f->w, f->h); }
        videoTex.bt709 = f->bt709; videoTex.fullRange = f->fullRange;
        gfx::videoUpload(g, videoTex, f->yuv.data(), f->yuv.size(), f->pts);
        pl.recycle(f);
      }
    }

    // ---------------------------------------------------------------- desenho
    double t = now / 1000.0;
    // ambiente: brilho grande e suave na cor do vídeo em foco, por cima do fundo e do destaque (assim não há emenda)
    auto glow = [&]() {
      gfx::rect(g, -160, -220, 860, 600, ui::alpha(ambient, 0.26f), 300, 360);
      gfx::rect(g, 820, 380, 560, 420, ui::alpha(ambient, 0.10f), 210, 300);
    };
    if (screen == SEARCH) glow();

    if (screen == HOME) {
      const int cols = settings.cols;
      const float gap = 24, cw = std::floor((ui::W - ui::X0 - 64 - (cols - 1) * gap) / cols), th = std::round(cw * 9 / 16), rowH = th + 106, top = 150;
      int n = (int)cur.feed.items.size(), row = cur.sel / cols;
      float want = zone == 1 || row == 0 ? 0 : row * rowH - 118;
      scroll += (want - scroll) * 0.2f;
      for (int i = 0; i < n; i++) {
        float x = ui::X0 + (i % cols) * (cw + gap), y = top + (i / cols) * rowH - scroll;
        if (y > ui::H || y + rowH < 90) continue;
        const Video& v = cur.feed.items[(size_t)i];
        bool focused = zone == 0 && !railFocus && i == cur.sel;
        float& an = selAnim[i % 600]; an += ((focused ? 1.f : 0.f) - an) * 0.25f;
        float s = 1 + 0.045f * an, w = cw * s, h = th * s, ix = x - (w - cw) / 2, iy = y - (h - th) / 2;
        if (an > 0.02f) {
          gfx::shadow(g, ix, iy + 10, w, h, 14, 22, 0.6f * an);
          gfx::rect(g, ix - 4, iy - 4, w + 8, h + 8, ui::alpha(rgb(0xffffff), an), 16);
        }
        const Thumbs::Slot* ts = thumbs.get(thumbsWorker, v.id, frame, cur.feed.direct);
        if (ts) gfx::image(g, ts->tex, ix, iy, w, h, 12);
        else gfx::rect(g, ix, iy, w, h, ui::SURFACE, 12);
        if (v.pos > 0 && v.dur > 0) {
          gfx::rect(g, ix + 4, iy + h - 5, w - 8, 4, {0, 0, 0, 0.6f}, 2);
          gfx::rect(g, ix + 4, iy + h - 5, std::max(4.f, (w - 8) * (float)std::clamp(v.pos / v.dur, 0.0, 1.0)), 4, ui::A1, 2);
        }
        if (v.dur > 0) {
          std::string d = fmtTime(v.dur); float dw = gfx::textWidth(g, ui::F_TINY, d) + 12;
          gfx::rect(g, ix + w - dw - 7, iy + h - 27, dw, 20, {0, 0, 0, 0.8f}, 5);
          gfx::text(g, ui::F_TINY, d, ix + w - dw / 2 - 7, iy + h - 24, ui::TEXT, 1);
        } else if (v.dur == 0) {
          gfx::rect(g, ix + w - 70, iy + h - 27, 63, 20, ui::A1, 5);
          gfx::text(g, ui::F_TINY, tr(K::Live), ix + w - 38, iy + h - 24, rgb(0xffffff), 1);
        }
        float ty = y + th + 14;
        for (auto& l : gfx::wrap(g, ui::F_CARD, v.title, cw, 2)) { gfx::text(g, ui::F_CARD, l, x, ty, focused ? ui::TEXT : rgb(0xe4e4e7)); ty += 23; }
        std::string meta = v.channel + (v.views.empty() ? "" : "  \xC2\xB7  " + v.views);
        gfx::text(g, ui::F_SMALL, gfx::ellipsize(g, ui::F_SMALL, meta, cw), x, ty + 3, ui::MUTED);
      }
      if (n == 0) {
        if (cur.feed.loading) { spinner(g, ui::X0 + (ui::W - ui::X0) / 2 - 32, 380, 22, t); gfx::text(g, ui::F_BODY, tr(K::LoadingVideos), ui::X0 + (ui::W - ui::X0) / 2 - 32, 420, ui::MUTED, 1); }
        else if (cur.feed.error != K::None) {
          gfx::text(g, ui::F_BIG, tr(settings.server ? K::ServerUnavailable : K::BadResponse), ui::X0, 190, ui::TEXT);
          gfx::text(g, ui::F_BODY, settings.server ? std::string(tr(cur.feed.error)) + " (" + srv.ip + ":" + std::to_string(srv.port) + ")" : tr(K::Refresh), ui::X0, 240, ui::MUTED);
          if (settings.server) gfx::text(g, ui::F_BODY, tr(K::RetryServer), ui::X0, 270, ui::MUTED);
        } else gfx::text(g, ui::F_BODY, tr(K::Empty), ui::X0, 190, ui::MUTED);
      }
      // topo fixo: o nome e as categorias (a grade passa por baixo)
      gfx::rect(g, 0, 0, ui::W, 118, ui::BG, 0);
      gfx::vgrad(g, 0, 118, ui::W, 36, ui::BG, ui::alpha(ui::BG, 0));
      wordmark(g, ui::X0, 22, 1, ui::BG);
      {
        float hw = 24 + 8 + gfx::textWidth(g, ui::F_SMALL, tr(K::Refresh)) + 26 + 24 + 8 + gfx::textWidth(g, ui::F_SMALL, tr(K::Search)) + 26 + 24 + 8 + gfx::textWidth(g, ui::F_SMALL, tr(K::MoreOptions));
        float lx = ui::W - 88 - hw, pillRight = lx - 18;
        auto pill = [&](const std::string& label, Color color) {
          std::string txt = gfx::ellipsize(g, ui::F_SMALL, label, 218);
          float w = gfx::textWidth(g, ui::F_SMALL, txt) + 32;
          gfx::rect(g, pillRight - w, 30, w, 28, ui::alpha(color, 0.16f), 14);
          gfx::rect(g, pillRight - w + 10, 41, 6, 6, color, 3);
          gfx::text(g, ui::F_SMALL, txt, pillRight - w + 22, 34, ui::TEXT);
          pillRight -= w + 10;
        };
        // a contagem do timer entra na fileira de pílulas (sozinha à direita, cobria a legenda do X)
        if (sleep.active) {
          std::string value = sleep.end ? tr(K::SleepEnd) : fmtTime(std::ceil(sleepLeft));
          if (sleep.end && sleepLeft >= 0) value += " · " + fmtTime(std::ceil(sleepLeft));
          pill(std::string(tr(K::SleepTimer)) + " · " + value, ui::A1);
        }
        if (!castWho.empty()) pill(tr(K::PhonePrefix) + castWho, rgb(0x34c759));
        accountPhoto();
        float hx3 = hint(g, lx, 32, "X", XX, tr(K::Refresh));
        float hx4 = hint(g, hx3, 32, "Y", XY, tr(K::Search));
        hint(g, hx4, 32, "Menu", XG, tr(K::MoreOptions));
      }
      if (inResults) {
        gfx::text(g, ui::F_H2, gfx::ellipsize(g, ui::F_H2, cur.feed.title, 760), ui::X0, 78, ui::TEXT);
        hint(g, ui::W - 64 - 120, 80, "B", XB, tr(K::Home));
      } else {
        // Chips inteiros: a janela acompanha o selecionado sem sair da tela.
        int first = cat; float used = gfx::textWidth(g, ui::F_BODY, cats[(size_t)cat].title) + 34;
        while (first > 0) {
          float prev = gfx::textWidth(g, ui::F_BODY, cats[(size_t)first - 1].title) + 44;
          if (used + prev > ui::W - ui::X0 - 64) break;
          used += prev; first--;
        }
        float cx = ui::X0;
        int last = first - 1;
        for (int c = first; c < (int)cats.size(); c++) {
          float w = gfx::textWidth(g, ui::F_BODY, cats[(size_t)c].title) + 34;
          if (cx + w > ui::W - 64) break;
          bool sel = c == cat, f = sel && zone == 1 && !railFocus;
          if (f) gfx::shadow(g, cx, 76 + 5, w, 40, 20, 14, 0.5f);
          gfx::rect(g, cx, 76, w, 40, f ? rgb(0xffffff) : sel ? ui::alpha(rgb(0xffffff), 0.9f) : ui::alpha(rgb(0xffffff), 0.09f), 20);
          gfx::text(g, ui::F_BODY, cats[(size_t)c].title, cx + w / 2, 85, sel ? ui::INK : ui::TEXT, 1);
          if (cats[(size_t)c].feed.loading && cats[(size_t)c].feed.items.empty()) gfx::rect(g, cx + w - 12, 82, 6, 6, ui::A1, 3);
          cx += w + 10;
          last = c;
        }
        if (first > 0) gfx::text(g, ui::F_SMALL, tr(K::BtnHorizontal), ui::X0 - 34, 88, ui::FAINT);
        if (last + 1 < (int)cats.size()) gfx::triangle(g, ui::W - 36, 89, ui::W - 29, 96, ui::W - 36, 103, ui::MUTED);
      }
    } else if (screen == SEARCH) {
      gfx::rect(g, 0, 0, ui::W, ui::H, ui::alpha(ui::BG, 0.55f), 0);
      gfx::text(g, ui::F_BIG, tr(K::Search), ui::X0, 52, ui::TEXT);
      float fx = ui::X0, fy = 112, fw = 652, fh = 60;
      gfx::rect(g, fx, fy, fw, fh, ui::alpha(rgb(0xffffff), 0.08f), 30);
      gfx::rect(g, fx + 1, fy + 1, fw - 2, fh - 2, ui::SURFACE, 29);
      icon(g, 1, fx + 34, fy + 30, ui::MUTED, ui::SURFACE);
      float qx = fx + 62;
      if (query.empty()) gfx::text(g, ui::F_KEY, gfx::ellipsize(g, ui::F_KEY, tr(searchMusic ? K::MusicSearch : K::SearchPrompt), 540), qx, fy + 17, ui::FAINT);
      else qx += gfx::text(g, ui::F_KEY, gfx::ellipsize(g, ui::F_KEY, query, 540), qx, fy + 17, ui::TEXT);
      if (std::fmod(t, 1.0) < 0.55) gfx::vgrad(g, qx + 3, fy + 15, 3, 30, ui::A1, ui::A2, 1.5f);
      const float kx = ui::X0, ky = 200, kw = 58, kh = 54, kg = 8;
      for (int rr = 0; rr < 4; rr++) {
        const char* row = keys[rr]; size_t i = 0; int c = 0;
        while (row[i]) {
          size_t st = i; gfx::utf8(row, i); std::string chs(row + st, i - st);
          bool f = !keyInRecent && keyRow == rr && keyCol == c;
          float x = kx + c * (kw + kg), y = ky + rr * (kh + kg);
          if (f) gfx::shadow(g, x, y + 5, kw, kh, 14, 14, 0.5f);
          gfx::rect(g, x, y, kw, kh, f ? rgb(0xffffff) : ui::alpha(rgb(0xffffff), 0.07f), 14);
          gfx::text(g, ui::F_KEY, chs, x + kw / 2, y + 15, f ? ui::INK : ui::TEXT, 1);
          c++;
        }
      }
      const char* acts[3] = {tr(K::Space), tr(K::Delete), tr(K::Search)};
      float aw = (10 * (kw + kg) - kg - 2 * kg) / 3;
      for (int c = 0; c < 3; c++) {
        bool f = !keyInRecent && keyRow == 4 && keyCol == c;
        float x = kx + c * (aw + kg), y = ky + 4 * (kh + kg);
        if (f) gfx::shadow(g, x, y + 5, aw, kh, 14, 14, 0.5f);
        if (c == 2) gfx::hgrad(g, x, y, aw, kh, f ? ui::mix(ui::A1, rgb(0xffffff), 0.25f) : ui::A1, f ? ui::mix(ui::A2, rgb(0xffffff), 0.25f) : ui::A2, 14);
        else gfx::rect(g, x, y, aw, kh, f ? rgb(0xffffff) : ui::alpha(rgb(0xffffff), 0.07f), 14);
        gfx::text(g, ui::F_BODY, acts[c], x + aw / 2, y + 16, c == 2 ? rgb(0xffffff) : f ? ui::INK : ui::TEXT, 1);
      }
      float rx = 846;
      gfx::text(g, ui::F_H2, tr(K::RecentSearches), rx, 122, ui::TEXT);
      if (recent.empty()) gfx::text(g, ui::F_SMALL, tr(K::RecentEmpty), rx, 162, ui::FAINT);
      for (size_t i = 0; i < recent.size(); i++) {
        bool f = keyInRecent && (int)i == recentSel; float y = 160 + i * 58;
        gfx::rect(g, rx, y, 370, 48, f ? rgb(0xffffff) : ui::alpha(rgb(0xffffff), 0.06f), 24);
        icon(g, 1, rx + 26, y + 24, f ? ui::INK : ui::FAINT, f ? rgb(0xffffff) : ui::SURFACE);
        gfx::text(g, ui::F_BODY, gfx::ellipsize(g, ui::F_BODY, recent[i], 300), rx + 50, y + 13, f ? ui::INK : ui::TEXT);
      }
      gfx::vgrad(g, 0, ui::H - 70, ui::W, 70, ui::alpha(ui::BG, 0), ui::alpha(ui::BG, 0.96f));
      float hx2 = ui::X0;
      hx2 = hint(g, hx2, ui::H - 40, "A", XA, tr(K::Type));
      hx2 = hint(g, hx2, ui::H - 40, "X", XX, tr(K::Delete));
      hx2 = hint(g, hx2, ui::H - 40, "Y", XY, tr(K::Space));
      hx2 = hint(g, hx2, ui::H - 40, "Menu", XG, tr(K::Search));
      hint(g, hx2, ui::H - 40, "B", XB, tr(K::Back));
    }

    if (screen == SETTINGS) {
      glow();
      gfx::text(g, ui::F_BIG, tr(K::Settings), ui::X0, 42, ui::TEXT);
      gfx::text(g, ui::F_BODY, tr(K::SettingsIntro), ui::X0, 89, ui::MUTED);
      gfx::text(g, ui::F_SMALL, tr(K::SavedAutomatically), ui::W - 64, 94, ui::FAINT, 2);
      const K labels[] = {K::YouTubeAccount, K::VideosPerRow, K::DefaultQuality, K::Sound, K::SpeakerSync, K::Language, K::SleepTimer, K::SleepTogether, K::ServerMode,K::DisplayOutput,K::AudioPreference};
      int first=std::clamp(settingsRow-6,0,3);
      for (int i = first; i < std::min(11,first+8); i++) {
        float x = ui::X0, y = 122 + (i-first) * 59, w = ui::W - ui::X0 - 64;
        bool selected = i == settingsRow, disabled = i == 3 && !nestAvailable;
        if (selected) {
          gfx::shadow(g, x, y + 6, w, 53, 18, 20, 0.45f);
          gfx::rect(g, x - 2, y - 2, w + 4, 57, ui::alpha(ui::A1, 0.65f), 20);
        }
        gfx::rect(g, x, y, w, 53, selected ? ui::SURFACE2 : ui::SURFACE, 18);
        if (selected) gfx::vgrad(g, x + 1, y + 13, 4, 27, ui::A1, ui::A2, 2);
        gfx::text(g, ui::F_CHAN, tr(labels[i]), x + 28, y + (i == 0 || (i == 3 && disabled) ? 6 : 17), disabled ? ui::MUTED : ui::TEXT);
        std::string value;
        if (i == 0) {
          value = !accountKnown && accountBusy ? tr(K::Loading) : account.signedIn() ? tr(K::SignedInAs) + account.name
              : account.state == Account::PENDING ? tr(K::Pending) : tr(K::SignedOut);
          gfx::text(g, ui::F_SMALL, tr(accountFailed ? K::AccountUnavailable : account.signedIn() ? K::SignOutAction : K::SignInAction), x + 28, y + 29, ui::MUTED);
        } else if (i == 1) value = tr(settings.cols == 3 ? K::Three : K::Four);
        else if (i == 2) value = tr(videoModeLabel(settings.videoMode));
        else if (i == 3) {
          value = nestAvailable && nestOut ? nestName : std::string(tr(K::TV));
          if (disabled) gfx::text(g, ui::F_SMALL, tr(nestChecked ? K::NoSpeaker : K::SpeakerChecking), x + 28, y + 29, ui::FAINT);
        } else if (i == 4) value = (nestOffsetMs > 0 ? "+" : "") + std::to_string(nestOffsetMs) + tr(K::Milliseconds);
        else if (i == 5) value = tr(settings.lang == i18n::Lang::PT ? K::Portuguese : K::English);
        else if (i == 6) value = sleepLabel(sleepChoice);
        else if (i == 7) value = tr(settings.sleepNotebook ? K::SleepNotebook : K::SleepXbox);
        else if (i==8) value = tr(settings.server ? K::Automatic : K::SleepOff);
        else if (i==9) value=std::to_string(g.extent.height)+"p";
        else value=tr(settings.originalAudio ? K::AudioAlwaysOriginal : K::AudioInterface);
        float right = x + w - 54;
        gfx::text(g, ui::F_KEY, gfx::ellipsize(g, ui::F_KEY, value, i == 0 || i==10 ? 520 : 320), right, y + 15, disabled ? ui::FAINT : selected ? ui::A2 : ui::TEXT, 2);
        if (selected && i > 0 && !disabled) {
          float ax = right - std::min(340.f, gfx::textWidth(g, ui::F_KEY, value)) - 26;
          gfx::triangle(g, ax, y + 27, ax + 6, y + 22, ax + 6, y + 32, ui::MUTED);
          gfx::triangle(g, x + w - 24, y + 27, x + w - 30, y + 22, x + w - 30, y + 32, ui::MUTED);
        }
      }
      gfx::text(g, ui::F_SMALL, tr(K::SleepTVHelp), ui::X0, 630, ui::MUTED);
      if (settingsRow==9) gfx::text(g,ui::F_SMALL,tr(K::Display1080Help),ui::X0,652,ui::FAINT);
      if (settingsRow == 4) gfx::text(g, ui::F_SMALL, tr(K::SyncHelp), ui::X0, 652, ui::FAINT);
      float hx = hint(g, ui::X0, 678, tr(K::BtnVertical), XG, tr(K::ChooseRow));
      hx = hint(g, hx, 678, tr(K::BtnHorizontal), XG, tr(K::ChangeValue));
      if (settingsRow == 0) hx = hint(g, hx, 678, "A", XA, tr(K::Open));
      hint(g, hx, 678, "B", XB, tr(K::Back));
    }

    if (screen == NAV) { nav.draw(g, thumbs, thumbsWorker, frame, now, settings.lang, !railFocus); accountPhoto(); }

    if (screen == HOME || screen == SEARCH || screen == SETTINGS || screen == NAV) {
      // barra lateral: ícones; com foco, abre com os nomes
      float rw = ui::RAIL + 200 * railOpen;
      if (railOpen > 0.01f) {
        gfx::rect(g, 0, 0, rw, ui::H, ui::alpha(ui::BG, 0.96f * railOpen), 0);
        gfx::hgrad(g, rw - 1, 0, 200, ui::H, ui::alpha(ui::BG, 0.96f * railOpen), ui::alpha(ui::BG, 0));
      }
      const char* names[] = {tr(K::Home), tr(K::Search), tr(K::Subscriptions), tr(K::Library), tr(K::Music), tr(K::Phone), tr(K::Settings), tr(K::Exit)};
      int page = railPage();
      for (int i = 0; i < 8; i++) {
        float y = 142 + i * 68;
        bool f = railFocus && railSel == i, act = i == page;
        if (f) gfx::rect(g, 30, y - 24, 52 + 168 * railOpen, 48, rgb(0xffffff), 24);
        else if (act) gfx::rect(g, 30, y - 24, 52, 48, ui::alpha(rgb(0xffffff), 0.1f), 24);
        if (act && !f) gfx::vgrad(g, 16, y - 12, 4, 24, ui::A1, ui::A2, 2);
        Color ic = f ? ui::INK : act ? ui::TEXT : ui::MUTED;
        const int symbols[] = {0,1,7,6,5,2,3,4};
        icon(g, symbols[i], 56, y, ic, f ? rgb(0xffffff) : act ? ui::mix(ui::BG, rgb(0xffffff), 0.1f) : ui::BG);
        if (railOpen > 0.05f) gfx::text(g, ui::F_BODY, names[i], 94, y - 12, ui::alpha(f ? ui::INK : ui::TEXT, railOpen));
        if (i == 5) gfx::rect(g, 66, y - 18, 9, 9, !castWho.empty() ? rgb(0x34c759) : remoteOnline ? rgb(0xd4d4d8) : ui::FAINT, 4.5f);
      }
    }

    if (screen == PLAYER && nav.musicNow) nav.drawNow(g, thumbs, thumbsWorker, playing, pl.position(), pl.duration, pl.paused, pl.waiting(),
        pl.state == player::FAILED ? i18n::playerError(pl.error) : K::None, frame, settings.lang);
    else {
    if (screen == PLAYER) {
      gfx::rect(g, 0, 0, ui::W, ui::H, rgb(0x000000), 0);
      gfx::videoQuad(g, 0, 0, ui::W, ui::H);
      bool panelOpen = ratePanel || sleepPanel || playerUI.panel != PlayerUI::NONE || sleepWarning || shutdownAt || nextAt;
      bool show = playerUI.visible(now) || panelOpen;
      if (show) {
        const Color red = rgb(0xff0033), island = {0,0,0,0.63f};
        gfx::vgrad(g, 0, 420, ui::W, 300, {0,0,0,0}, {0,0,0,0.87f});
        bool options = playerUI.options && !panelOpen;
        bool transportFocus = options && playerUI.selected == PlayerUI::TRANSPORT;
        const std::string& title = !captions.title.empty() ? captions.title : pl.title.empty() ? playing.title : pl.title;
        const std::string& channel = captions.channel.empty() ? playing.channel : captions.channel;
        if (metaChannel != channel || metaDate != captions.date || metaLang != settings.lang) {
          metaChannel = channel; metaDate = captions.date; metaLang = settings.lang;
          std::string meta = channel;
          if (!metaDate.empty()) {
            if (!meta.empty()) meta += " · ";
            meta += i18n::shortDate(metaDate, metaLang);
          }
          overlayMeta.update(g, ui::F_META, meta.c_str(), 690);
        }
        overlayTitle.update(g, ui::F_VIDEO_TITLE, title.c_str(), options && !transportFocus ? 760 : 1200);
        gfx::text(g, ui::F_VIDEO_TITLE, overlayTitle.text, 40, 563, ui::TEXT);
        gfx::text(g, ui::F_META, overlayMeta.text, 40, 591, rgb(0xd4d4d8));

        const bool live = pl.live;
        double pos = pl.position(), dur = pl.duration;
        constexpr float bx = 40, by = 624, bw = 1200;
        float frac = live ? 1.f : dur > 0 ? (float)std::clamp(pos / dur, 0.0, 1.0) : 0;
        gfx::rect(g, bx, by, bw, 4, {1,1,1,0.25f}, 2);
        if (frac > 0) gfx::rect(g, bx, by, bw * frac, 4, red, 2);
        const cc::Chapter* currentChapter = nullptr;
        if (!live && dur > 0) {
          for (const auto& chapter : captions.chapters) {
            float x = bx + bw * (float)(chapter.start / dur);
            if (x > bx && x < bx + bw) gfx::rect(g, x - 1, by, 2, 4, island);
            if (chapter.start <= pos) currentChapter = &chapter;
          }
        }
        gfx::rect(g, bx + bw * frac - 7, by - 5, 14, 14, red, 7);

        if (!live && (clockSecond != (int)pos || clockDuration != (int)dur)) {
          clockSecond = (int)pos; clockDuration = (int)dur;
          clockLabel = fmtTime(pos);
          if (dur > 0) clockLabel += " / " + fmtTime(dur);
          clockWidth = gfx::textWidth(g, ui::F_CLOCK, clockLabel);
        }
        const char* liveLabel = tr(K::Live);
        float leftW = 78 + (live ? 14 + gfx::textWidth(g, ui::F_CLOCK, liveLabel) : clockWidth);
        gfx::playerIsland(g, 32, leftW);
        playerIcon(g, pl.paused ? icons::PLAY : icons::PAUSE, 60, transportFocus);
        if (live) {
          Color status = behindLive ? ui::MUTED : red;
          gfx::rect(g, 92, 662, 8, 8, status, 4);
          gfx::text(g, ui::F_CLOCK, liveLabel, 106, 657, status);
        } else gfx::text(g, ui::F_CLOCK, clockLabel, 92, 657, ui::TEXT);
        if (currentChapter && !options && !panelOpen) {
          overlayChapter.update(g, ui::F_META, currentChapter->title.c_str(), 430);
          gfx::text(g, ui::F_META, overlayChapter.text, 46 + leftW, 658, ui::TEXT);
        }

        constexpr icons::PlayerIcon symbols[] = {icons::CLOSED_CAPTIONING, icons::GEAR, icons::SPEAKER_HIGH, icons::TRANSLATE_PLAYER,
                                                 icons::MOON, icons::THUMBS_UP, icons::THUMBS_DOWN, icons::USER_CIRCLE, icons::BROADCAST};
        float rightW = 8 + (optionCount - 1 - (playerUI.tracks ? 0 : 1)) * 48, rightX = 1248 - rightW;
        gfx::playerIsland(g, rightX, rightW);
        float focusX = 60;
        int slot = 0;
        for (int i = PlayerUI::CC; i < optionCount; i++) {
          if (i == PlayerUI::TRACK && !playerUI.tracks) continue;
          float cx = rightX + 28 + slot++ * 48;
          bool focused = options && playerUI.selected == i;
          // Avaliação já dada fica com o joinha cheio, como no YouTube.
          bool marked = (i == PlayerUI::RATE && likeState == "LIKE") || (i == PlayerUI::DISLIKE && likeState == "DISLIKE");
          playerIcon(g, symbols[i - PlayerUI::CC], cx, focused, marked);
          if (focused) focusX = cx;
          if (i == PlayerUI::VIDEO) {
            bool full = pl.actualHeight > 720;
            float badgeWidth = full ? 29 : 23;
            gfx::rect(g, cx - 3, 648, badgeWidth, 11, red, 3);
            gfx::text(g, ui::F_BADGE, full ? "1080" : "720", cx - 3 + badgeWidth / 2, 648, rgb(0xffffff), 1);
          } else if (i == PlayerUI::AUDIO && pl.nest) {
            gfx::rect(g, cx + 6, 652, 8, 8, rgb(0x22c55e), 4);
          }
        }
        if (options) {
          const K labels[] = {pl.paused ? K::Resume : K::Pause, K::Captions, K::PlayerQuality, K::Sound,
                              K::AudioTrack, K::SleepTimer, K::Like, K::Dislike, K::Channel, K::ChannelLive};
          const char* value = "";
          std::string timerValue;
          switch (playerUI.selected) {
            case PlayerUI::CC:
              value = tr(captionLoader.loading ? K::Loading : captions.failed ? K::CaptionsFailed :
                         captions.reason == "live_clock" ? K::CaptionsLiveClock : captions.tracks.empty() ?
                         (live ? K::CaptionsLiveNone : K::CaptionsNone) : settings.captions ? K::CaptionsOn : K::CaptionsOff);
              break;
            case PlayerUI::VIDEO: {
              int resolution = pl.actualHeight.load(), sourceFps = pl.actualFps.load();
              int targetFps = std::min(sourceFps, pl.presentFps.load());
              const char* mode = pl.waiting() ? nullptr : tr(pl.direct ? K::Console : pl.original ? K::ServerOriginal : K::ServerConverted);
              if (resolution != lastQuality || sourceFps != lastSourceFps || targetFps != lastTargetFps || lastOutput != (int)g.extent.height || mode != lastMode) {
                resolutionLabel = resolution ? std::to_string(resolution) + "p" : "720p";
                if (sourceFps) resolutionLabel += std::to_string(sourceFps);
                if (sourceFps > targetFps) resolutionLabel += " -> " + std::to_string(targetFps) + " fps";
                qualityLabel = resolutionLabel + " · " + std::to_string(g.extent.height) + "p";
                if (mode) { qualityLabel += " · "; qualityLabel += mode; }
                lastQuality = resolution; lastSourceFps = sourceFps; lastTargetFps = targetFps;
                lastOutput = (int)g.extent.height; lastMode = mode;
              }
              value = qualityLabel.c_str(); break;
            }
            case PlayerUI::AUDIO: value = pl.nest ? nestName.c_str() : tr(K::TV); break;
            case PlayerUI::TRACK: value = selectedTrackLabel.c_str(); break;
            case PlayerUI::TIMER:
              if (sleep.active && !sleep.end) { timerValue = fmtTime(std::ceil(sleepLeft)); value = timerValue.c_str(); }
              else value = tr(sleep.active ? K::SleepEnd : K::SleepOff);
              break;
            case PlayerUI::RATE: if (likeState == "LIKE") value = tr(K::Liked); break;
            case PlayerUI::DISLIKE: if (likeState == "DISLIKE") value = tr(K::Disliked); break;
            case PlayerUI::CHANNEL: value = channel.c_str(); break;
            case PlayerUI::EDGE: value = tr(K::GoLive); break;
          }
          tipLabel.update(g, ui::F_CARD, tr(labels[playerUI.selected]), 310);
          tipValue.update(g, ui::F_DETAIL, value, 360);
          playerTip(g, focusX, tipLabel, tipValue, transportFocus);
        }
      }
      if (settings.captions && !captions.failed && !panelOpen && videoTex.ready) {
        int index = captions.at(pl.position());
        if (index != captionIndex) {
          captionIndex = index; captionLines.clear(); captionWidth = 0;
          if (index >= 0) {
            captionLines = gfx::wrap(g, ui::F_H2, captions.cues[(size_t)index].text, 1060, 2);
            for (size_t i = 0; i < captionLines.size(); i++) {
              captionLineWidths[i] = gfx::textWidth(g, ui::F_H2, captionLines[i]);
              captionWidth = std::max(captionWidth, captionLineWidths[i]);
            }
          }
        }
        if (!captionLines.empty()) {
          float height = (float)captionLines.size() * 31 + 16, bottom = show ? 482 : 674;
          gfx::rect(g, (ui::W - captionWidth) / 2 - 18, bottom - height, captionWidth + 36, height, {0, 0, 0, 0.78f}, 8);
          float cy = bottom - height + 8;
          for (size_t i = 0; i < captionLines.size(); i++) {
            gfx::text(g, ui::F_H2, captionLines[i], (ui::W - captionLineWidths[i]) / 2, cy, ui::TEXT); cy += 31;
          }
        }
      }
      if (playerUI.panel != PlayerUI::NONE) {
        float w = 720, h = playerUI.panel==PlayerUI::TRACKS ? 430 : 330, x = (ui::W - w) / 2, y = playerUI.panel==PlayerUI::TRACKS ? 140 : 190;
        gfx::rect(g, 0, 0, ui::W, ui::H, {0, 0, 0, 0.55f});
        gfx::shadow(g, x, y + 10, w, h, 24, 32, 0.6f);
        gfx::rect(g, x, y, w, h, ui::SURFACE, 24);
        bool ccPanel = playerUI.panel == PlayerUI::CAPTIONS;
        gfx::text(g, ui::F_BIG, tr(ccPanel ? K::Captions : playerUI.panel == PlayerUI::QUALITY ? K::PlayerQuality : playerUI.panel==PlayerUI::TRACKS ? K::AudioTrack : K::Sound), x + 36, y + 28, ui::TEXT);
        if (ccPanel && (captionLoader.loading || captions.failed || captions.tracks.empty())) {
          gfx::text(g, ui::F_BODY, tr(captionLoader.loading ? K::CaptionsLoading : captions.failed ? K::CaptionsFailed : captions.reason == "live_clock" ? K::CaptionsLiveClock : pl.live ? K::CaptionsLiveNone : K::CaptionsNone), x + 36, y + 110, ui::MUTED);
        } else {
          int count = ccPanel ? 2 : playerUI.panel == PlayerUI::QUALITY ? 3 : playerUI.panel==PlayerUI::TRACKS ? (int)audioTracks.size() : 2;
          int first=playerUI.panel==PlayerUI::TRACKS ? std::max(0,playerUI.panelSel-3) : 0;
          for (int i = first; i < std::min(count,first+4); i++) {
            float ry = y + 90 + (i-first) * 58;
            bool focused = playerUI.panelSel == i;
            bool disabled = playerUI.panel == PlayerUI::SOUND && i == 1 && !nestAvailable;
            gfx::rect(g, x + 30, ry, w - 60, 50, focused ? ui::TEXT : ui::SURFACE2, 10);
            std::string label;
            if (ccPanel && !i) label = tr(settings.captions ? K::CaptionsOn : K::CaptionsOff);
            else if (ccPanel) {
              label = tr(K::CaptionsLanguage);
              for (const auto& track : captions.tracks) if (track.code == captions.selected)
                label = track.name + " · " + tr(track.automatic ? K::CaptionsAuto : K::CaptionsManual);
            } else if (playerUI.panel == PlayerUI::QUALITY) label = tr(videoModeLabel(i));
            else if (playerUI.panel==PlayerUI::TRACKS) label=trackLabels[i];
            else label = i ? nestName : tr(K::TV);
            gfx::text(g, ui::F_BODY, gfx::ellipsize(g, ui::F_BODY, label, w - 132), x + 50, ry + 14, disabled ? ui::FAINT : focused ? ui::INK : ui::TEXT);
          }
        }
        float hx = hint(g, x + 36, y + h - 42, tr(K::BtnVertical), XG, tr(K::ChooseRow));
        hx = hint(g, hx, y + h - 42, "A", XA, tr(K::SleepApply));
        hint(g, hx, y + h - 42, "B", XB, tr(K::Back));
      }
      if (ratePanel) {
        // dois botões redondos com o joinha, como no YouTube: aceso (cheio, na cor de destaque) o que está marcado
        float w = 560, h = 252, x = (ui::W - w) / 2, y = ui::H - 456;
        gfx::shadow(g, x, y + 10, w, h, 24, 32, 0.6f);
        gfx::rect(g, x, y, w, h, ui::SURFACE, 24);
        gfx::text(g, ui::F_H2, tr(K::RateTitle), x + 32, y + 26, ui::TEXT);
        const float d = 96, gap = 56;
        for (int i = 0; i < 2; i++) {
          float bx = x + w / 2 + (i == 0 ? -gap / 2 - d : gap / 2), bt = y + 78;
          bool sel = rateSel == i, on = likeState == (i == 0 ? "LIKE" : "DISLIKE");
          if (sel) gfx::rect(g, bx - 4, bt - 4, d + 8, d + 8, ui::alpha(rgb(0xffffff), 0.9f), d / 2 + 4);
          gfx::rect(g, bx, bt, d, d, on ? ui::A1 : sel ? ui::SURFACE2 : ui::alpha(rgb(0xffffff), 0.08f), d / 2);
          int which = i == 0 ? (on ? icons::THUMB_UP_FILL : icons::THUMB_UP) : (on ? icons::THUMB_DOWN_FILL : icons::THUMB_DOWN);
          glyph(g, which, true, bx + (d - 48) / 2, bt + (d - 48) / 2, on || sel ? rgb(0xffffff) : ui::MUTED);
        }
        float hx = hint(g, x + 32, y + 206, tr(K::BtnHorizontal), XG, tr(K::ChangeValue));
        hx = hint(g, hx, y + 206, "A", XA, tr(K::SleepApply));
        hint(g, hx, y + 206, "B", XB, tr(K::Close));
      }
      if (nextAt) {
        gfx::rect(g, 0, 0, ui::W, ui::H, {0, 0, 0, 0.6f});
        float w = 800, h = 300, x = (ui::W - w) / 2, y = (ui::H - h) / 2;
        gfx::shadow(g, x, y + 12, w, h, 24, 32, 0.6f);
        gfx::rect(g, x, y, w, h, ui::SURFACE, 24);
        double left = nextAt > now ? (nextAt - now) / 1000.0 : 0;
        gfx::text(g, ui::F_SMALL, tr(K::UpNextIn) + std::to_string((int)std::ceil(left)) + " s", x + 32, y + 26, ui::MUTED);
        gfx::rect(g, x + 32, y + 52, w - 64, 4, {1, 1, 1, 0.12f}, 2);
        gfx::hgrad(g, x + 32, y + 52, std::max(4.f, (w - 64) * (float)(1 - left / 8.0)), 4, ui::A1, ui::A2, 2);
        float tw = 256, th = 144, tx = x + 32, ty = y + 74;
        gfx::rect(g, tx, ty, tw, th, ui::SURFACE2, 12);
        if (const Thumbs::Slot* s = thumbs.get(thumbsWorker, upNext.id, frame, pl.direct)) gfx::image(g, s->tex, tx, ty, tw, th, 12);
        float cx = tx + tw + 28, cw = x + w - 32 - cx, cy = ty + 2;
        for (auto& line : gfx::wrap(g, ui::F_H2, upNext.title, cw, 2)) { gfx::text(g, ui::F_H2, line, cx, cy, ui::TEXT); cy += 30; }
        if (!upNext.channel.empty()) gfx::text(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, upNext.channel, cw), cx, cy + 8, ui::MUTED);
        float hx = hint(g, x + 32, y + h - 52, "A", XA, tr(K::WatchNow));
        hint(g, hx, y + h - 52, "B", XB, tr(K::Cancel));
      }
      if (pl.waiting() && !panelOpen) {
        spinner(g, ui::W / 2, ui::H / 2, 26, t);
        gfx::text(g, ui::F_BODY, pl.state == player::CONNECTING ? tr(pl.direct ? K::Loading : K::RequestingVideo) : tr(K::Loading), ui::W / 2, ui::H / 2 + 46, ui::TEXT, 1);
      }
      if (pl.state == player::FAILED && !panelOpen) {
        gfx::rect(g, ui::W / 2 - 340, ui::H / 2 - 74, 680, 148, ui::SURFACE, 22);
        gfx::text(g, ui::F_H2, tr(K::VideoFailed), ui::W / 2, ui::H / 2 - 46, ui::TEXT, 1);
        gfx::text(g, ui::F_BODY, tr(i18n::playerError(pl.error)), ui::W / 2, ui::H / 2, ui::MUTED, 1);
      }
    }

    }

    if (sleep.active && screen == PLAYER && nav.musicNow) {
      std::string value = sleep.end ? tr(K::SleepEnd) : fmtTime(std::ceil(sleepLeft));
      if (sleep.end && sleepLeft >= 0) value += " · " + fmtTime(std::ceil(sleepLeft));
      std::string label = std::string(tr(K::SleepTimer)) + " · " + value;
      float w = gfx::textWidth(g, ui::F_SMALL, label) + 30, x = ui::W - 64 - w;
      float y = screen == PLAYER ? 124 : 24;
      gfx::rect(g, x, y, w, 32, ui::SURFACE2, 16);
      gfx::text(g, ui::F_SMALL, label, x + 15, y + 7, ui::MUTED);
    }
    if (nav.menu) nav.drawMenu(g, settings.lang);
    if (nav.unsubscribe) {
      gfx::rect(g, 0, 0, 1280, 720, {0,0,0,0.8f}); gfx::rect(g, 250, 250, 780, 210, ui::SURFACE, 24);
      gfx::text(g, ui::F_H2, tr(K::SubscribeConfirm), 290, 290, ui::TEXT);
      float hx = hint(g, 290, 398, "A", XA, tr(K::Confirm)); hint(g, hx, 398, "B", XB, tr(K::Cancel));
    }
    if (nav.busy && screen != PLAYER) {
      gfx::rect(g, 410, 615, 460, 48, ui::SURFACE2, 24);
      gfx::text(g, ui::F_CHAN, tr(nav.actionPath.rfind("/api/nav/play?",0) == 0 ? K::StartingQueue : K::Saving), 640, 630, ui::TEXT, 1);
    }
    if (phonePanel) {
      gfx::rect(g, 0, 0, ui::W, ui::H, {0, 0, 0, 0.72f});
      float pw = 820, ph = 418, px = (ui::W - pw) / 2, py = (ui::H - ph) / 2;
      gfx::shadow(g, px, py + 10, pw, ph, 28, 40, 0.6f);
      gfx::rect(g, px, py, pw, ph, ui::SURFACE, 28);
      icon(g, 2, px + 56, py + 60, ui::TEXT, ui::SURFACE);
      gfx::text(g, ui::F_BIG, tr(K::CastTitle), px + 92, py + 40, ui::TEXT);
      float ty = py + 104;
      for (auto& line : gfx::wrap(g, ui::F_BODY, std::string(tr(K::CastChoose)) + "YouTube 360.", pw - 96, 2)) {
        gfx::text(g, ui::F_BODY, line, px + 48, ty, ui::TEXT); ty += 25;
      }
      for (auto& line : gfx::wrap(g, ui::F_SMALL, tr(K::CastPath), pw - 96, 2)) {
        gfx::text(g, ui::F_SMALL, line, px + 48, ty + 8, ui::MUTED); ty += 22;
      }
      if (!castCode.empty()) gfx::textGrad(g, ui::F_CODE, castCode, px + 48, py + 186, ui::A1, ui::A2);
      else gfx::text(g, ui::F_BODY, tr(K::CastLoading), px + 48, py + 208, ui::FAINT);
      gfx::text(g, ui::F_SMALL, tr(K::CastRemember), px + 48, py + 264, ui::MUTED);
      gfx::rect(g, px + 48, py + 300, pw - 96, 1, ui::alpha(rgb(0xffffff), 0.08f));
      gfx::text(g, ui::F_SMALL, !castWho.empty() ? tr(K::PhoneConnected) + castWho : std::string(tr(phoneStatus.online ? K::NoPhone : K::CastOffline)),
                px + 48, py + 320, !castWho.empty() ? rgb(0x34c759) : ui::FAINT);
      float hx = hint(g, px + 48, py + 366, "A", XA, tr(K::Close));
      hint(g, hx, py + 366, "B", XB, tr(K::Back));
    }
    if (loginPanel || logoutPanel) {
      gfx::rect(g, 0, 0, ui::W, ui::H, {0, 0, 0, 0.78f});
      float pw = 880, ph = logoutPanel ? 330 : 488, px = (ui::W - pw) / 2, py = (ui::H - ph) / 2;
      gfx::shadow(g, px, py + 12, pw, ph, 28, 40, 0.7f);
      gfx::rect(g, px, py, pw, ph, ui::SURFACE, 28);
      gfx::hgrad(g, px + 48, py + 24, 56, 4, ui::A1, ui::A2, 2);
      gfx::text(g, ui::F_BIG, tr(logoutPanel ? K::SignOutTitle : K::SignInTitle), px + 48, py + 48, ui::TEXT);
      if (logoutPanel) {
        gfx::text(g, ui::F_KEY, gfx::ellipsize(g, ui::F_KEY, account.name, pw - 96), px + 48, py + 106, ui::A2);
        gfx::text(g, ui::F_BODY, tr(K::SignOutBody), px + 48, py + 152, ui::MUTED);
        if (accountBusy) spinner(g, ui::W / 2, py + 218, 14, t);
        float hx = hint(g, px + 48, py + ph - 58, "A", XA, tr(K::Confirm));
        hint(g, hx, py + ph - 58, "B", XB, tr(K::Cancel));
      } else {
        gfx::text(g, ui::F_BODY, tr(K::SignInStep), ui::W / 2, py + 114, ui::MUTED, 1);
        gfx::text(g, ui::F_BIG, tr(K::DeviceURL), ui::W / 2, py + 148, ui::TEXT, 1);
        gfx::text(g, ui::F_SMALL, tr(K::EnterCode), ui::W / 2, py + 214, ui::MUTED, 1);
        if (!account.code.empty() && loginError != K::LoginExpired) {
          float w = gfx::textWidth(g, ui::F_CODE, account.code);
          gfx::textGrad(g, ui::F_CODE, account.code, (ui::W - w) / 2, py + 252, ui::A1, ui::A2);
        } else if (accountBusy) spinner(g, ui::W / 2, py + 286, 22, t);
        gfx::text(g, ui::F_BODY, tr(loginError != K::None ? loginError : K::LoginWaiting), ui::W / 2, py + 352, ui::MUTED, 1);
        float hx = px + 48;
        if (loginError != K::None) hx = hint(g, hx, py + ph - 58, "A", XA, tr(K::Refresh));
        hint(g, hx, py + ph - 58, "B", XB, tr(K::Back));
      }
    }
    if (!toast.empty() && now < toastUntil) {
      std::string shown = gfx::ellipsize(g, ui::F_BODY, toast, ui::W - 100);
      float w = gfx::textWidth(g, ui::F_BODY, shown) + 52;
      gfx::rect(g, ui::W / 2 - w / 2, 24, w, 46, ui::SURFACE2, 23);
      gfx::text(g, ui::F_BODY, shown, ui::W / 2, 36, ui::TEXT, 1);
    }

    if (sleepPanel || sleepWarning || shutdownAt) {
      gfx::rect(g, 0, 0, ui::W, ui::H, {0, 0, 0, 0.65f});
      float x = 280, y = sleepPanel && !sleepWarning && !shutdownAt ? 190 : 242, w = 720;
      float h = sleepPanel && !sleepWarning && !shutdownAt ? 330 : 226;
      gfx::shadow(g, x, y + 12, w, h, 24, 32, 0.6f);
      gfx::rect(g, x, y, w, h, ui::SURFACE, 24);
      std::string title = shutdownAt ? tr(K::SleepShuttingDown) : sleepWarning ? std::string(tr(K::SleepWarning)) +
          std::to_string((int)std::ceil(sleepLeft)) + tr(K::SleepSeconds) : tr(K::SleepTimer);
      gfx::text(g, ui::F_BIG, title, x + 36, y + 28, ui::TEXT);
      if (!shutdownAt) {
        if (!sleepWarning) {
          bool timeFocus = playerUI.sleepRow == 0;
          gfx::rect(g, x + 30, y + 90, w - 60, 62, timeFocus ? ui::TEXT : ui::SURFACE2, 12);
          gfx::text(g, ui::F_KEY, sleepLabel(sleepChoice), x + 50, y + 108, timeFocus ? ui::INK : ui::A2);
          gfx::text(g, ui::F_KEY, "<   >", x + w - 50, y + 108, timeFocus ? ui::INK : ui::MUTED, 2);
          gfx::rect(g, x + 30, y + 174, w - 60, 62, !timeFocus ? ui::TEXT : ui::SURFACE2, 12);
          gfx::text(g, ui::F_SMALL, tr(K::PlayerAlsoOff), x + 50, y + 184, !timeFocus ? ui::INK : ui::MUTED);
          gfx::text(g, ui::F_BODY, tr(settings.sleepNotebook ? K::SleepNotebook : K::SleepXbox), x + 50, y + 206, !timeFocus ? ui::INK : ui::TEXT);
          gfx::text(g, ui::F_BODY, "<   >", x + w - 50, y + 198, !timeFocus ? ui::INK : ui::MUTED, 2);
        } else gfx::text(g, ui::F_KEY, tr(sleep.notebook ? K::SleepNotebook : K::SleepXbox), x + 36, y + 92, ui::A2);
        float hy = y + h - 48, hx = x + 36;
        if (!sleepWarning) hx = hint(g, hx, hy, tr(K::BtnVertical), XG, tr(K::ChooseRow));
        hx = hint(g, hx, hy, "A", XA, tr(sleepWarning ? K::SleepAdd : playerUI.sleepRow ? K::ChangeValue : K::SleepApply));
        hint(g, hx, hy, "B", XB, tr(K::Back));
      }
    }

    bool shoot = shotPath && (int)frame == shotFrame;
#ifndef __XSDK__
    if (shotPath && std::getenv("XT_SHOT_MS")) shoot = now - runStart >= (uint64_t)std::atoi(std::getenv("XT_SHOT_MS"));
#endif
    gfx::end(g, shoot ? shotPath : nullptr);
    if (shoot) {
      LOG("captura: frame %llu, tempo %llu ms, tela %d, vídeo %s, posição %.2f", (unsigned long long)frame, (unsigned long long)(now - runStart), (int)screen, playing.id.c_str(), pl.position());
      LOG("captura conta: conectada=%d foto=%d", account.signedIn(), !account.avatar.empty());
      if (screen == NAV && !nav.pages.empty()) LOG("captura navegação: %s sel=%d canal=%d zona=%d pilha=%u menu=%d prateleira=%d",
          nav.page().kind.c_str(), nav.page().sel, nav.page().channelSel, nav.page().zone, (unsigned)nav.pages.size(), railFocus, nav.page().shelf);
      quit = true;
    }
    frame++;
    if (frame % 600 == 0 && screen == PLAYER) LOG("player: %s pos %.1f quadros %lld perdidos %lld", playing.id.c_str(), pl.position(), (long long)pl.shownFrames, (long long)pl.droppedFrames);
  }
  recordHistory();
  phone.stop();
  captionLoader.stop();
  pl.halt();
  pl.directSpeaker.shutdown();
  SDL_LockMutex(worker.mu); worker.quit = true; SDL_SignalCondition(worker.cv); SDL_UnlockMutex(worker.mu);
  remote.quit = true;
  SDL_LockMutex(sleepWorker.mu); sleepWorker.quit = true; SDL_SignalCondition(sleepWorker.cv); SDL_UnlockMutex(sleepWorker.mu);
  SDL_LockMutex(navWorker.mu); navWorker.quit = true; SDL_SignalCondition(navWorker.cv); SDL_UnlockMutex(navWorker.mu);
  SDL_WaitThread(nt, nullptr);
  SDL_WaitThread(st, nullptr);
  SDL_LockMutex(control.mu); control.quit = true; SDL_SignalCondition(control.cv); SDL_UnlockMutex(control.mu);
  SDL_LockMutex(thumbsWorker.mu); thumbsWorker.quit = true; SDL_BroadcastCondition(thumbsWorker.cv); SDL_UnlockMutex(thumbsWorker.mu);
  SDL_WaitThread(wt, nullptr);
  SDL_WaitThread(ct, nullptr);
  for (auto* th : tw) SDL_WaitThread(th, nullptr);
  SDL_WaitThread(rt, nullptr);
  vkDeviceWaitIdle(g.dev);
  LOG("miniaturas: %u bytes GPU, pico de RGBA aguardando upload %u bytes", (unsigned)(thumbs.slots.size() * thumbs.slotBytes + thumbs.bannerBytes), (unsigned)thumbs.peakPending);
  LOG("%s saindo", brandName.c_str());
  SDL_Quit();
  return 0;
}
