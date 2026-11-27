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
#include "thumb_scale.hpp"
#include "startup.hpp"
#include "boot_log.h"
#include "connection_error.hpp"
#include "seek_hold.hpp"
#include "gfx_video_memory.hpp"
#include "net.hpp"
#include "player.hpp"
#include "music_player.hpp"
#include "player_ui.hpp"
#include "player_watch.hpp"
#include "player_extras.hpp"
#include "sponsorblock_worker.hpp"
#include "player_text.hpp"
#include "captions.hpp"
#include "settings.hpp"
#include "audio_track_label.hpp"
#include "account.hpp"
#include "local_library.hpp"
#include "yt/console_account.hpp"
#include "sleep.hpp"
#include "icons.hpp"
#include "cast/receiver.hpp"
#ifdef TUBE360_F33_BENCH
#include "../../bench/1080p/playback_probe.hpp"
#endif
#ifdef TUBE360_F40_BENCH
#include "../../bench/1080p-app/playback_probe.hpp"
#endif

#ifdef __XSDK__
#include "xsdk/net.h"
extern "C" void install_player_fault_handler();
// XAM começa com recepção de 16 KB por socket; 63 KB segura o fluxo de vídeo com folga (xsdk/net.h).
extern "C" const XsdkNetworkLimits xsdk_network_limits = {32, 8, 63, 16};
#endif

using gfx::Color;
using gfx::rgb;
using K = i18n::Key;

#include "ui_theme.hpp"

struct ItemGrid {
  struct GridRow { std::vector<int> items; bool shorts = false; std::string heading; bool carousel = false; int column = 0; };  // column: posição própria de cada prateleira rolável  // heading: título do grupo (Histórico por dia), só na 1ª fileira do grupo
  std::vector<GridRow> rows;
  std::vector<int> itemRows;
  int gridSize = -1, gridCols = 0, gridColumn = 0, shortColumn = 0;
  bool gridHide = false, gridOnlyShorts = false; int gridGrouped = 0;
  // grouped: 0 sem grupos; 1 grupos que quebram em fileiras (Histórico por dia); 2 uma fileira rolável por prateleira (Início da conta).
  template<class Item> void build(const std::vector<Item>& items, int& sel, int cols, bool hide, int grouped = 0, bool onlyShorts = false) {
    std::map<std::string,int> kept;
    if (gridSize != (int)items.size() || gridCols != cols || gridHide != hide || gridGrouped != grouped || gridOnlyShorts != onlyShorts) {
      gridSize = (int)items.size(); gridCols = cols; gridHide = hide; gridGrouped = grouped; gridOnlyShorts = onlyShorts;
      for (const auto& r : rows) if (r.carousel) kept[r.heading] = r.column;  // a posição de cada prateleira sobrevive à continuação
      rows.clear(); itemRows.assign(items.size(), -1);
      int shortRow = -1;
      std::string lastSection;
      for (int i = 0; i < gridSize; ++i) {
        if (items[i].shortForm) {
          if (hide) continue;
          if (shortRow < 0) { shortRow = (int)rows.size(); rows.push_back({{},true}); }
          rows[shortRow].items.push_back(i); itemRows[i] = shortRow;
        } else {
          if (onlyShorts) continue;  // chip "Shorts" da busca: só os Shorts
          bool newGroup = false;
          if constexpr (requires { items[i].section; }) {
            newGroup = grouped && !items[i].section.empty() && items[i].section != lastSection;
            lastSection = items[i].section;  // só vídeos comuns: um Short no meio não esconde o título do dia
          }
          bool full = !rows.empty() && (int)rows.back().items.size() == cols;
          if (!rows.empty() && rows.back().carousel && !rows.back().shorts && !newGroup) full = false;
          if (rows.empty() || rows.back().shorts || full || newGroup) {
            rows.push_back({});
            if constexpr (requires { items[i].section; }) if (grouped && (newGroup || rows.size() == 1)) { rows.back().heading = items[i].section; rows.back().carousel = grouped == 2 && !items[i].section.empty(); }
          }
          rows.back().items.push_back(i); itemRows[i] = (int)rows.size()-1;
        }
      }
    }
    for (auto& r : rows) if (r.carousel) { auto k = kept.find(r.heading); if (k != kept.end()) r.column = k->second; }
    if (rows.empty()) { sel = 0; return; }
    if (sel < 0 || sel >= gridSize || itemRows[sel] < 0) {
      int next = std::clamp(sel, 0, std::max(0, gridSize-1));
      while (next < gridSize && itemRows[next] < 0) ++next;
      if (next == gridSize) { next = gridSize-1; while (next >= 0 && itemRows[next] < 0) --next; }
      sel = next;
    }
  }
  int gridRow(int sel) const { return sel >= 0 && sel < (int)itemRows.size() ? itemRows[sel] : -1; }
  int gridPosition(int sel) const {
    int row = gridRow(sel); if (row < 0) return 0;
    const auto& indices = rows[row].items;
    return (int)(std::find(indices.begin(),indices.end(),sel)-indices.begin());
  }
  bool moveGrid(int& sel, int horizontal, int vertical) {
    int row = gridRow(sel); if (row < 0) return false;
    int col = gridPosition(sel);
    if (horizontal) {
      col = std::clamp(col+horizontal,0,(int)rows[row].items.size()-1);
      sel = rows[row].items[col];
      if (rows[row].shorts) shortColumn = col; else gridColumn = col;
      if (rows[row].carousel) rows[row].column = col;
    }
    if (vertical) {
      if (!rows[row].shorts) gridColumn = col;
      if (rows[row].carousel) rows[row].column = col;
      int next = row+vertical;
      if (next < 0 || next >= (int)rows.size()) return false;
      int column = rows[next].shorts ? shortColumn : rows[next].carousel ? rows[next].column : gridColumn;
      sel = rows[next].items[std::clamp(column,0,(int)rows[next].items.size()-1)];
    }
    return true;
  }
};

struct Video { std::string id, channel, views, title, date, section; int dur = -1; double pos = 0; yt::Membership members = yt::Membership::None; bool shortForm = false, live = false; };
struct Feed { std::string title; std::vector<Video> items; bool loading = false; K error = K::None; std::string errorDetail; bool more = true, loadingMore = false, direct = false; ItemGrid layout; };
struct Shelf { std::string title, query; Feed feed; int sel = 0; std::string kind = "search"; uint64_t stamp = 0; };

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
  feed.more = head.size() > 3 && head[3] == "1";
  for (size_t i = 1; i < lines.size(); i++) {
    auto f = net::split(lines[i], '\t');
    if (f.size() < 5 || f[0].empty()) continue;
    Video v; v.id = f[0]; v.dur = std::atoi(f[1].c_str()); v.channel = f[2]; v.views = f[3]; v.title = f[4];
    v.live = v.dur == 0; if (v.dur <= 0) v.dur = v.live ? 0 : -1;  // fio: 0 = ao vivo, -1 = desconhecida, > 0 = segundos
    if (f.size() > 5) { double pos = std::atof(f[5].c_str()); if (std::isfinite(pos)) v.pos = std::max(0.0, pos); }
    if (f.size() > 6) v.members = yt::membershipWire(f[6]);
    if (f.size() > 7) v.shortForm = f[7] == "short";
    if (f.size() > 8) v.section = f[8];
    feed.items.push_back(v);
  }
}

struct Worker {
  net::Server srv; std::shared_ptr<yt::ConsoleAccount> consoleAccount; SDL_Mutex* mu = SDL_CreateMutex(); SDL_Condition* cv = SDL_CreateCondition();
  std::deque<Job> jobs; std::deque<Result> results; bool quit = false, finishJobs = false; int hw = 5;
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
    catalog.setLogger([](const std::string& message) { LOG("%s",message.c_str()); });
    for (;;) {
      SDL_LockMutex(mu);
      while (jobs.empty() && !quit) SDL_WaitCondition(cv, mu);
      if (quit && (!finishJobs || jobs.empty())) { SDL_UnlockMutex(mu); return; }
      Job j = jobs.front(); jobs.pop_front();
      if (j.a.rfind("/api/",0)==0) {
        if (j.a.find("lang=ar") != std::string::npos) j.lang=i18n::Lang::AR;
        else if (j.a.find("lang=es") != std::string::npos) j.lang=i18n::Lang::ES;
        else if (j.a.find("lang=pt") != std::string::npos) j.lang=i18n::Lang::PT;
      }
      youtube.setLanguage(i18n::code(j.lang));
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
        auto route=yt::catalog::query(j.a);
        if (route["kind"]=="search") {
          if (!r.catalog) r.feed.title=std::string("error\t")+i18n::get(j.lang,K::BadResponse)+"\n";
          r.catalog=true;
        }
        bool accountFallback = !r.catalog && consoleAccount && consoleAccount->active() && yt::ConsoleAccount::supports(j.a);
        // "signed_out" (Biblioteca sem conta) chega ao cliente como pedido de login, não como falta de resposta
        if (!r.catalog && !net::serverEnabled && !accountFallback && r.feed.title != "error\tsigned_out\n") { r.feed.error = K::NoResponse; r.feed.title.clear(); }
        if (r.catalog || (!net::serverEnabled && !accountFallback)) {
          LOG("catalogo console: %s, %u bytes, music=%s%s%s",j.a.substr(0,j.a.find('?')).c_str(),(unsigned)r.feed.title.size(),catalog.musicSource.c_str(),
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
          } else r.feed.title = std::move(reply.body);
        } else if (j.kind == 6) {
          r.feed.title = std::string("erro\t") + i18n::get(j.lang,
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
            Video v; v.id = std::move(card.id); v.title = std::move(card.title); v.members = card.members; v.shortForm = card.shortForm;
            v.channel = std::move(card.channel); v.views = std::move(card.views); v.date = std::move(card.date); v.dur = card.duration;
            v.live = v.dur == 0;  // Card: 0 = ao vivo (liveMark no collect), -1 = desconhecida
            r.feed.items.push_back(std::move(v));
          }
          r.feed.title = j.kind == 6 ? j.query : j.a;
        } else if (!net::serverEnabled) {
          r.feed.error = connection::message(page.failure);
          if(page.failure==yt::Failure::TLS_DATE)r.feed.errorDetail=std::string(i18n::get(j.lang,K::ConsoleClock))+connection::clock();
          LOG("catalogo: falha %s%s%s",yt::failureName(page.failure),r.feed.errorDetail.empty()?"":" ",r.feed.errorDetail.c_str());
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
        bool artwork = !yt::catalog::imageUrl(address).empty(), jpegTried=false;
        if (artwork) {
          auto img = https->request(yt::catalog::imageUrl(address));
          native = img.status == 200 && img.error.empty();
          if (native) resp.body.assign(img.body.begin(),img.body.end());
          else {
            auto fallback=yt::catalog::jpegThumbnail(address);
            if(!fallback.empty() && fallback!=yt::catalog::imageUrl(address)){
              jpegTried=true;auto jpg=https->request(fallback);native=jpg.status==200 && jpg.error.empty();
              if(native)resp.body.assign(jpg.body.begin(),jpg.body.end());
            }
          }
        }
        if (!native && (j.native || !net::serverEnabled || (consoleAccount && consoleAccount->active())) && j.a.size() == 11 && j.a.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos) {
          // #15: a capa HD (1280x720) reduzida ao cartão; sem ela, a de 320x180 de sempre. No console o JPEG HD só vale para a
          // capa grande (640x360, uma imagem): decodificar 1280x720 por cartão custa ~19x de CPU e ~11 MB de pico no Xenon, inclusive
          // com 3 colunas (384x216), texto grande e 480p; lá os cartões seguem em 320x180.
#ifdef __XSDK__
          const bool wantHd = j.w >= 640;
#else
          const bool wantHd = true;
#endif
          yt::Response img;
          if (wantHd) { img = https->request("https://i.ytimg.com/vi/" + j.a + "/hq720.jpg"); }
          if (!(img.status == 200 && img.error.empty())) img = https->request("https://i.ytimg.com/vi/" + j.a + "/mqdefault.jpg");
          native = img.status == 200 && img.error.empty();
          if (native) resp.body.assign(img.body.begin(), img.body.end());
        }
        if (native || (!artwork && net::get(srv, "/api/thumb/" + (j.a[0] == '~' ? "nav/" + j.a.substr(1) : j.a) + size, resp, 10000))) {
          int w, h, n;
          // Não decodifica dimensões arbitrárias recebidas pela rede.
          if (!stbi_info_from_memory(resp.body.data(), (int)resp.body.size(), &w, &h, &n) || (artwork || (native && j.a.rfind("~yt",0) == 0) ? (w < 1 || h < 1 || w > 2048 || h > 2048 || int64_t(w)*h > 2097152) : native ? !((w == 320 && h == 180) || (w == 1280 && h == 720)) : (w != j.w || h != j.h))) { w = h = 0; }
          if (!w && artwork && !jpegTried) {
            auto fallback=yt::catalog::jpegThumbnail(address);
            if(!fallback.empty() && fallback!=address){
              auto jpg=https->request(fallback);
              if(jpg.status==200 && jpg.error.empty()){
                resp.body.assign(jpg.body.begin(),jpg.body.end());
                if(!stbi_info_from_memory(resp.body.data(),int(resp.body.size()),&w,&h,&n) || w<1 || h<1 || w>2048 || h>2048 || int64_t(w)*h>2097152)w=h=0;
                LOG("miniatura: JPEG fallback ok=%d",w>0);
              }
            }
          }
          unsigned char* px = w ? stbi_load_from_memory(resp.body.data(), (int)resp.body.size(), &w, &h, &n, 4) : nullptr;
          if (px) {
            r.w = j.w; r.h = j.h;
            if (artwork || (native && j.a.rfind("~yt",0) == 0)) {
              r.rgba.assign((size_t)j.w*j.h*4,0);
              double scale = std::min(double(j.w)/w,double(j.h)/h);
              int dw = std::max(1,int(w*scale)), dh = std::max(1,int(h*scale));
              if (banner) { dw = j.w; dh = j.h; }
              int dx = (j.w-dw)/2, dy = (j.h-dh)/2;
              if (w > dw || h > dh) boxScale(px, w, h, 0, 0, w, h, r.rgba.data() + 4 * ((size_t)dy * j.w + dx), 4 * j.w, dw, dh);
              else for (int y = 0; y < dh; ++y) for (int x = 0; x < dw; ++x)
                std::memcpy(r.rgba.data()+4*((y+dy)*j.w+x+dx),px+4*((y*h/dh)*w+x*w/dw),4);
            } else if (w == j.w && h == j.h) r.rgba.assign(px, px + (size_t)w*h*4);
            else {
              r.rgba.resize((size_t)j.w*j.h*4);
              if (w > j.w || h > j.h) boxScale(px, w, h, 0, 0, w, h, r.rgba.data(), 4 * j.w, j.w, j.h);
              else for (int y=0; y<j.h; ++y) for (int x=0; x<j.w; ++x)
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
    gfx::quiesce(g,"thumbnail layout");
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

#include "music_navigation.hpp"
#include "ui_cards.hpp"
#include "local_library_ui.hpp"
#include "update/ui.hpp"
#include "nest_picker.hpp"
#include "sponsorblock_ui.hpp"
#include "search_ui.hpp"
#include "ui_context.hpp"
#include "ui_home.hpp"
#include "ui_settings.hpp"
#include "ui_shell.hpp"
#include "ui_search.hpp"
#include "ui_collection.hpp"
#include "ui_channel.hpp"
#include "ui_player.hpp"
#include "ui_shorts.hpp"
#include "ui_music.hpp"
#ifndef __XSDK__
#include "../../bench/search/frame_probe.hpp"
#endif

int run(int argc, char** argv) {
  (void)argc; (void)argv;
#ifdef __XSDK__
  boot_event("begin","environment.fault-handler","optional, opt-in only");
  install_player_fault_handler();
  boot_event("end","environment.fault-handler","probe returned");
  SDL_SetHint("SDL_AUDIO_XSDK_HARDWARE_THREAD","1");
  std::string assets = "";
#else
  std::string assets = std::getenv("XT_ASSETS") ? std::getenv("XT_ASSETS") : "assets/";
#endif
  if (!assets.empty() && assets.back() != '/') assets += '/';
  boot_event("begin","settings","reading interface configuration");
  startup::configure(assets);
  boot_event("end","settings","language=%s",i18n::code(startup::language));
  boot_event("begin","sdl.init","gamepad/audio; no Xbox Live requirement");
#ifdef __XSDK__
  bool initialized=SDL_Init(SDL_INIT_GAMEPAD|SDL_INIT_EVENTS|SDL_INIT_AUDIO);
#else
  bool initialized=SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD|SDL_INIT_AUDIO);
  const char* startupTest=std::getenv("XT_STARTUP_ERROR");
  if(startupTest && !std::strcmp(startupTest,"sdl"))initialized=false;
#endif
  boot_event(initialized?"end":"error","sdl.init","ok=%d error=%s",initialized,initialized?"none":SDL_GetError());
  if(!initialized)return startup::fail(K::StartupSDL,SDL_GetError());
  boot_event("begin","storage.writable","title directory");
  bool canWrite=startup::writable();
  boot_event(canWrite?"end":"error","storage.writable","ok=%d errno=%d",canWrite,canWrite?0:errno);
  if(!canWrite)return startup::fail(K::StartupFolder,assets,K::StartupStorage);
  boot_event("begin","fonts.preflight","four bundled TTF headers");
  auto missingFonts=startup::missingFonts(assets);
  boot_event(missingFonts.empty()?"end":"error","fonts.preflight","missing=%s",missingFonts.empty()?"none":missingFonts.c_str());
  if(!missingFonts.empty())return startup::fail(K::StartupFonts,missingFonts,K::StartupCopy);
  Settings settings; settings.load(assets);
  yt::placeThread("presentation",0,2);
  auto tr = [&](K key) { return i18n::get(settings.lang, key); };
  if (FILE* f = std::fopen((assets + "marca.txt").c_str(), "r")) {
    char line[64] = {0};
    if (std::fgets(line, sizeof(line), f)) { brandName = line; while (!brandName.empty() && (brandName.back() == '\n' || brandName.back() == '\r')) brandName.pop_back(); }
    if (std::fgets(line, sizeof(line), f) && !std::strncmp(line, "retangulo", 9)) brandRect = true;
    if (brandName.empty()) brandName = "Tube360";
    std::fclose(f);
  }
  LOG("%s iniciando", brandName.c_str());
#if defined(TUBE360_F40_BENCH) || defined(TUBE360_F33_BENCH)
  if (FILE* f = std::fopen((assets + "mudo.txt").c_str(), "r")) { player::benchMute = true; std::fclose(f); LOG("bancada: mudo.txt, volume zero"); }
#endif
  net::Server srv;
  if (FILE* f = std::fopen((assets + "server.txt").c_str(), "r")) {
    char ip[64] = {0}; int port = 0;
    if (std::fscanf(f, "%63s %d", ip, &port) == 2) { srv.ip = ip; srv.port = port; }
    std::fclose(f);
  }
  if (const char* e = std::getenv("XT_SERVER")) { char ip[64] = {0}; int port = 0; if (std::sscanf(e, "%63[^:]:%d", ip, &port) == 2) { srv.ip = ip; srv.port = port; } }
  // Sem server.txt (nem XT_SERVER) o app é só do console: nenhum pedido ao PC, nem para o Nest ou o timer.
  net::serverEnabled = net::effectiveServer(srv,settings.server);
  const std::string serverAddress=srv.ip+":"+std::to_string(srv.port);
  LOG("servidor %s:%d%s", srv.ip.c_str(), srv.port, net::serverEnabled ? "" : " (desligado: só console)");

  static gfx::Gfx g{};
  gfx::emergencyGraphics=&g;startup::releaseGraphics=gfx::closeForEmergency;
  gfx::failureScreen=[](const char* reason){startup::fail(K::StartupVideo,reason);};
  if (FILE* f = std::fopen((assets + "video-metrics.txt").c_str(),"r")) { std::fscanf(f,"%d",&g.videoMetrics); std::fclose(f); }
  const char* shotPath = std::getenv("XT_SHOT");
  g.readback = shotPath != nullptr;
  boot_event("begin","gpu.init","requested_output=%dp",settings.outputHeight);
  if (!gfx::init(g, (settings.outputHeight==480 ? 640 : settings.outputHeight*16/9), settings.outputHeight, "Tube360")) return startup::fail(K::StartupVideo,SDL_GetError());
#ifndef __XSDK__
  if(startupTest && !std::strcmp(startupTest,"video"))return startup::fail(K::StartupVideo,"GPU initialization (fixture)");
#endif
#ifndef __XSDK__
  if(startupTest && !std::strcmp(startupTest,"launch"))return startup::fail(K::StartupLaunch,"tube360.xex",K::StartupCopy);
  if(startupTest && !std::strcmp(startupTest,"timeout"))return startup::fail(K::StartupLaunch,tr(K::StartupTimeout));
#endif
  boot_event("end","gpu.init","swapchain=%ux%u",g.extent.width,g.extent.height);
  const std::vector<gfx::FontSpec> fontSpecs={{"inter-400.ttf", 19}, {"inter-400.ttf", 16}, {"inter-600.ttf", 17}, {"inter-600.ttf", 23},
                                  {"inter-700.ttf", 27}, {"inter-700.ttf", 34}, {"inter-600.ttf", 22}, {"inter-700.ttf", 13},
                                  {"inter-700.ttf", 42}, {"inter-600.ttf", 18}, {"inter-300.ttf", 27}, {"inter-700.ttf", 58},
                                  {"inter-600.ttf", 24.2f}, {"inter-400.ttf", 17}, {"inter-400.ttf", 15.1f},
                                  {"inter-400.ttf", 18.2f}, {"inter-700.ttf", 11}};
  auto loadDisplay = [&]() {
    // Reconfigurar também pode travar: o diagnóstico precisa sobreviver fora do primeiro frame.
    boot_event("begin","display.fonts","extent=%ux%u large_text=%d",g.extent.width,g.extent.height,settings.largeText);
    boot_event("begin","settings.layout","extent=%ux%u large=%d",g.extent.width,g.extent.height,settings.largeText);
    auto layout = display::layout(g.extent.width,g.extent.height,settings.largeText,gfx::outputAspect());
    ui::W = layout.width; ui::large = layout.large; ui::narrow = layout.narrow;
    g.rtlText = i18n::rtl(settings.lang);
    g.scale = layout.scale; g.scaleX = layout.scaleX; g.insetX = layout.insetX; g.insetY = layout.insetY;
    auto specs = fontSpecs;
    if (ui::large) {
      for (size_t i=0;i<specs.size();++i) {
        if (i==ui::F_LOGO || i==ui::F_LOGOL || i==ui::F_CODE || i==ui::F_HERO || i==ui::F_BIG || i==ui::F_BADGE) continue;
        specs[i].px = std::max(layout.sd ? 28.f : 22.f,specs[i].px*(layout.sd ? 1.6f : 1.3f));
      }
    }
    LOG("layout: %.0fx720, %s, texto %s, margem %.0fx%.0f",ui::W,ui::narrow ? "4:3" : "wide",ui::large ? "grande" : "normal",g.insetX,g.insetY);
    bool loaded=gfx::loadFonts(g,assets,specs);
    boot_event(loaded?"end":"error","settings.layout","font atlas loaded=%d",loaded);
    boot_event(loaded?"end":"error","display.fonts","extent=%ux%u large_text=%d loaded=%d",g.extent.width,g.extent.height,settings.largeText,loaded);
    return loaded;
  };
  boot_event("begin","fonts.atlas","load and upload Inter");
  if (!loadDisplay()) return startup::fail(K::StartupFonts,"inter-400.ttf, inter-600.ttf, inter-700.ttf, inter-300.ttf",K::StartupCopy);
  boot_event("end","fonts.atlas","ready");
  boot_event("begin","icons","embedded glyphs");
  loadIcons(g);
  boot_event("end","icons","ready");

  gfx::Video videoTex; g.video = &videoTex;
  boot_event("begin","video.memory","detect mapped R8 capability");
  gfx::DecodePool videoPool(g);
  boot_event("end","video.memory","mapped_capable=%d; allocation failure uses staging",videoPool.enabled);
  boot_event("begin","audio.init","PCM and controller workers");
  music::Player pl; pl.cacheBase = assets + "yt-base"; pl.init(); if(!pl.mu || !pl.cv || !pl.nmu || !pl.audio)return startup::fail(K::StartupSDL,SDL_GetError()); pl.videoMetrics = g.videoMetrics; pl.directSpeaker.init(assets);
  pl.videoBuffers = videoPool.enabled ? &videoPool : nullptr;
  pl.h264FullFilter=settings.h264FullFilter;
  if (FILE* f = std::fopen((assets + "decode-config.txt").c_str(),"r")) {
    int direct = 1, type = 1, threads = 4, loop = -1;
    if (std::fscanf(f,"%d %d %d %d",&direct,&threads,&type,&loop) == 4) {
      if (!direct) pl.videoBuffers = nullptr;
      pl.h264Threads = std::clamp(threads,1,6); pl.h264ThreadType = type == 2 ? FF_THREAD_SLICE : FF_THREAD_FRAME;
      pl.h264LoopFilter = std::clamp(loop,-1,1);
    }
    std::fclose(f);
  }
  boot_event("end","audio.init","ready; mapped_video_selected=%d; decode-config.txt can disable it",pl.videoBuffers!=nullptr);
  boot_network();
  boot_event("begin","network.workers","HTTP/account/update workers");
  updates::Service updater(assets);
  updates::UI updateUI;
  auto accountNet = yt::accountTransport();
  auto consoleAccount = std::make_shared<yt::ConsoleAccount>(*accountNet, assets + "yt-console-account.json");
  consoleAccount->select(!net::serverEnabled); pl.memberAccount = consoleAccount;
  Worker worker; worker.srv = srv; worker.consoleAccount = consoleAccount;
  SDL_Thread* wt = yt::worker(Worker::main, "trabalho", &worker);
  // Conta e pareamento não esperam as buscas do feed.
  Worker control; control.srv = srv; control.consoleAccount = consoleAccount; control.finishJobs = true; control.hw = 5;
  SDL_Thread* ct = yt::worker(Worker::main, "conta", &control);
  Worker navWorker; navWorker.srv = srv; navWorker.consoleAccount = consoleAccount; navWorker.hw = 5;
  SDL_Thread* nt = yt::worker(Worker::main, "navegação", &navWorker);
  MusicNavigation nav;
  LocalLibraryUI localUI(assets); nav.local=&localUI.library;
  Worker sleepWorker; sleepWorker.srv = srv; sleepWorker.hw = 5;
  SDL_Thread* st = yt::worker(Worker::main, "timer", &sleepWorker);
  // miniaturas numa fila própria, com 3 threads: não esperam atrás das buscas das categorias
  Worker thumbsWorker; thumbsWorker.srv = srv; thumbsWorker.consoleAccount = consoleAccount; thumbsWorker.hw = 5;
  SDL_Thread* tw[3];
  for (auto& th : tw) th = yt::worker(Worker::main, "miniaturas", &thumbsWorker);
  if(!wt || !ct || !nt || !st || !tw[0] || !tw[1] || !tw[2])return startup::fail(K::StartupSDL,"worker: "+std::string(SDL_GetError()));
  Remote remote; remote.srv = srv;
  SDL_Thread* rt = SDL_CreateThread(Remote::main, "controle", &remote);
  if(!rt)return startup::fail(K::StartupSDL,"remote worker: "+std::string(SDL_GetError()));
  boot_event("end","network.workers","threads created; failures remain local");
  boot_event("begin","phone.start","optional SSDP/lounge receiver");
  cast::Receiver phone(assets, brandName); phone.enable(true);
  boot_event("end","phone.start","enable returned; discovery failures do not block Home");
  cast::Status phoneStatus; uint64_t phoneStateAt = 0, phonePlaybackSerial = 0, playerPositionRevision = 0;
  float phoneGain = 1;
  bool endTimerPending = false;
  Thumbs thumbs;
  thumbs.configure(g, thumbsWorker, ui::columns(settings.cols));
  std::vector<Shelf> cats;
  Shelf results; bool inResults = false; int cat = 0;
  Account account; bool accountBusy = false, accountKnown = false, accountFailed = false;
  uint64_t accountAt = 0, nestInfoAt = 0, stamp = 0, accountBackend = consoleAccount->revision();
  auto requestFeed = [&](Shelf& shelf, int kind, bool fresh = false, int offset = 0, bool front = false) {
    if (!offset) { shelf.stamp = ++stamp; shelf.feed.loading = true; shelf.feed.loadingMore = false; shelf.feed.error = K::None; }
    if(shelf.kind=="local_later" || shelf.kind=="local_history") {
      shelf.stamp=++stamp;shelf.feed={};shelf.feed.direct=true;shelf.feed.more=false;
      for(const auto& e:shelf.kind=="local_later"?localUI.library.later:localUI.library.history){Video v;v.id=e.id;v.title=e.title;v.channel=e.channel;v.dur=e.duration;v.pos=localUI.library.position(e.id);shelf.feed.items.push_back(v);}return;
    }
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
    cats.push_back({tr(account.signedIn()?K::LocalHistory:K::ContinueWatching), "", {}, 0, "local_history"});
    cats.push_back({tr(account.signedIn()?K::LocalLater:K::WatchLater), "", {}, 0, "local_later"});
    nav.signedIn=account.signedIn();nav.localRevision=0;
    const K titles[] = {K::Music, K::Football, K::Games, K::Podcasts, K::News};
    const K queries[] = {K::MusicQuery, K::FootballQuery, K::GamesQuery, K::PodcastsQuery, K::NewsQuery};
    for (int i = 0; i < 5; i++) cats.push_back({tr(titles[i]), tr(queries[i]), {}});
    requestFeed(cats[0], 0);
    if (inResults) requestFeed(results, 1, false, 0, true);
  };
  rebuildCats();

  Screen screen = HOME;
  Screen navReturn = HOME, searchBack = HOME; bool searchMusic = false, navReturnRail = false;
  int zone = 0;  // 0 grade, 1 categorias
  float scroll = 0;
  bool railFocus = false; int railSel = 0; float railOpen = 0;
  PlayerExtras extras; extras.init();
  sponsorblock::Worker sponsorWorker; sponsorWorker.init();
  sponsorblock::Playback sponsors; sponsorblock::UI sponsorUI;
  std::vector<std::string> commentLines; int commentIndex=-1;
  PlayerText commentAuthor, commentMeta; std::string commentPosition;
  bool phonePanel = false;
  int settingsRow = 0; bool loginPanel = false, logoutPanel = false;
  K loginError = K::None;
  Color ambient{0.15f, 0.15f, 0.2f, 1}, ambientWant = ambient;
  std::string query; std::vector<std::string> recent;
  SearchUI searchUI; searchUI.worker.init();
  playback::SeekHold seekHold;
  PlayerUI playerUI; uint64_t& overlayUntil = playerUI.until;
  playback::QualityChoices offeredQualities(1),defaultQualities;
  bool qualityKnown=false;
  std::vector<std::string> qualityHelpLines,manualNoticeLines;
  uint64_t manualNoticeUntil=0,manualNoticeFont=0;
  i18n::Lang manualNoticeLang=settings.lang;
  float manualNoticeWidth=0;
  Screen backTo = HOME; std::string toast; uint64_t toastUntil = 0;
  Video playing; bool playerFromRemote = false, localRecording=false;
  cc::Loader captionLoader; cc::Data captions;
  int captionIndex = -1; std::vector<std::string> captionLines; float captionWidth = 0;
  float captionLineWidths[2]{};
  int lastQuality = -1, lastSourceFps = -1, lastTargetFps=-1, lastOutput=-1; const char* lastMode = nullptr;
  std::string qualityLabel, resolutionLabel, seekLabel;
  int seekSecond=-1;
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
  std::vector<Video> related, shortsQueue, playedBefore, shelfScratch; bool historyBack = false; NavPage shelfChannel; PlayerShelf shelf; std::string shortTitle; i18n::Lang shortLang = i18n::Lang::EN; std::vector<std::string> shortLines;  // F63: fileira "a seguir" do player (declarados aqui porque o startVideo os usa)
  bool livePaused = false;
  int& quality = settings.quality;
  quality=720;
  bool& nestOut = settings.nest; int& nestOffsetMs = settings.syncMs;
  auto saveSettings = [&]() {
    if (!settings.save(assets)) { toast = tr(K::SaveFailed); toastUntil = SDL_GetTicks() + 4000; }
  };
  std::string nestName = pl.directSpeaker.configured() ? pl.directSpeaker.settings().name : tr(K::Nest);
  bool nestAvailable = pl.directSpeaker.configured(), nestChecked = nestAvailable;
  SpeakerPicker speakerPicker;
  nest::Config speakerChoice = pl.directSpeaker.settings();
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
  // Sem PC o timer é do próprio console: mesmo formato da resposta do servidor, prazo contado aqui.
  auto localSleep = [&](const std::string& path) {
    uint64_t at = SDL_GetTicks();
    if (path.find("target_only=1") != std::string::npos || path == "/api/sleep") return;
    bool active = sleep.active, end = sleep.end; double seconds = sleep.remaining(at);
    if (path.rfind("/api/sleep/cancel", 0) == 0) { active = false; end = false; seconds = 0; }
    else if (path.rfind("/api/sleep/add", 0) == 0) {
      size_t m = path.find("min="); double add = m == std::string::npos ? 15 : std::atof(path.c_str() + m + 4);
      active = true; end = false; seconds = std::max(0.0, seconds) + add * 60;
    } else if (path.rfind("/api/sleep/set", 0) == 0) {
      active = true; end = path.find("end=1") != std::string::npos;
      size_t m = path.find("min="); seconds = end ? -1 : (m == std::string::npos ? 0 : std::atof(path.c_str() + m + 4) * 60);
    }
    char text[96];
    std::snprintf(text, sizeof(text), "ok\t%s\t%.0f\txbox\t%s", active ? "on" : "off", seconds, end ? "fim_do_video" : "minutos");
    sleep.parse(text, at);
    LOG("timer: %s %.0f s %s (console)", sleep.active ? "on" : "off", sleep.seconds, sleep.end ? "fim_do_video" : "minutos");
  };
  auto requestSleep = [&](const std::string& path) {
    if (!net::serverEnabled) { localSleep(path); return; }
    sleepWorker.add({5, path, ++sleepRevision}); sleepPending++;
  };
#ifndef __XSDK__
  if (const char* t = std::getenv("XT_SLEEP_PATH")) requestSleep(t);  // teste: ex. /api/sleep/set?what=xbox&min=0.05
#endif
  auto applySleep = [&]() {
    requestSleep(sleepChoice == 0 ? "/api/sleep/cancel" : std::string("/api/sleep/set?what=xbox") +
        (sleepChoice == 7 ? "&end=1" : "&min=" + std::to_string(sleepMinutes[sleepChoice])));
  };

  bool latinKeyboard=false;
  Input in; SDL_Gamepad* pad = nullptr;
  bool quit = false; uint64_t frame = 0, runStart = SDL_GetTicks(); int shotFrame = std::getenv("XT_SHOT_FRAME") ? std::atoi(std::getenv("XT_SHOT_FRAME")) : 90;
#ifndef __XSDK__
  if (const char* s = std::getenv("XT_SCREEN")) {
    if (!std::strcmp(s, "search")) { screen = SEARCH; query = "flamengo 0x3 palmeiras"; recent = {"palmeiras 4x1 corinthians", "reações dos corintianos", "flamengo 0x3 palmeiras"}; }
    if (!std::strcmp(s, "rail")) { railFocus = true; }
    if (!std::strcmp(s, "chips")) { zone = 1; }
    if (!std::strcmp(s, "settings")) { screen = SETTINGS; }
  }
  if (const char* text=std::getenv("XT_SEARCH_TEXT")) query=text;
  if (g.win) SDL_StartTextInput(g.win);
#endif
  uint64_t historyAt = 0; std::string historyVideo; bool historyStarted = false;
  auto recordLocal = [&]() {
    if(localRecording && !playing.id.empty() && !pl.live && !pl.playingAudio && pl.shownFrames>0 && pl.duration>0) {
      Video v=playing;if(v.title.empty())v.title=pl.title;if(v.channel.empty())v.channel=pl.media.channel;
      localUI.record(v,pl.position(),pl.duration,pl.state==player::ENDED);
    }
  };
  auto recordHistory = [&]() {
    recordLocal();
    if (!account.signedIn() || !consoleAccount->active() || historyVideo.empty() || !historyStarted) return;
    Job j{7, "/api/account/watched?id=" + historyVideo + "&pos=" + std::to_string(pl.position()) + "&progress=1", 0};
    control.add(j);
  };
  auto startVideo = [&](const Video& v, double t, bool fromRemote, std::string track = {}) {
    bool localOpening=v.id!=playing.id || screen!=PLAYER;
    if (screen != PLAYER) playedBefore.clear();
    if (!historyBack && !nav.localQueue && screen == PLAYER && !playing.id.empty() && v.id != playing.id && !nav.musicNow) {
      if (playedBefore.empty() || playedBefore.back().id != playing.id) playedBefore.push_back(playing);
      if (playedBefore.size() > 20) playedBefore.erase(playedBefore.begin());
    }
    if (v.shortForm && std::none_of(shortsQueue.begin(), shortsQueue.end(), [&](const Video& s) { return s.id == v.id; })) {
      // Fila do ↑ ↓ dos Shorts: os Shorts da lista de onde o Short foi aberto, na ordem dela.
      shortsQueue.clear();
      const Shelf* source = nullptr;
      for (const Shelf* s : {inResults ? &results : (const Shelf*)nullptr, cat >= 0 && cat < (int)cats.size() ? &cats[(size_t)cat] : (const Shelf*)nullptr})
        if (s && !source && std::any_of(s->feed.items.begin(), s->feed.items.end(), [&](const Video& i) { return i.id == v.id; })) source = s;
      if (source) for (const auto& i : source->feed.items) if (i.shortForm) shortsQueue.push_back(i);
      if (shortsQueue.empty()) shortsQueue.push_back(v);
    }
    if(localOpening){recordLocal();localUI.checkpoint=SDL_GetTicks()+30000;}
    ++playerPositionRevision;
    offeredQualities.assign(1);
    qualityKnown=false;
    manualNoticeUntil=0;
    LOG("player: pedido %s inicio=%.3f anterior=%.3f modo=%d remoto=%d",v.id.c_str(),t,pl.position(),settings.videoMode,fromRemote);
    if (v.id != playing.id || screen != PLAYER) {
      sponsors.reset(); sponsorWorker.select(settings.sponsorBlock==sponsorblock::OFF ? "" : v.id);
    }
    if (track.empty()) track=v.id==playing.id ? pl.audioSelection : settings.originalAudio ? "original" : std::string("lang:")+i18n::code(settings.lang);
    if (!fromRemote) phonePlaybackSerial = 0;
    if (historyVideo != v.id) { recordHistory(); historyVideo = v.id; historyStarted = false; historyAt = 0; }
    if (!fromRemote && nav.ownsQueue && !nav.localQueue) {
      nav.ownsQueue = nav.musicSession = nav.musicNow = false;
      navWorker.add({6, "/api/nav/transport?action=clear", ++nav.serial}, true);
    }
    if (v.id != playing.id || screen != PLAYER) {
      extras.select(v.id,i18n::code(settings.lang)); commentLines.clear(); commentIndex=-1;
    }
    if (v.id != playing.id) {
      nestSkip = false; likeState.clear(); upNext = Video{}; related.clear();
      if (!nav.localQueue) {
        Job next{5, "/api/video/" + v.id + "/next", 0}; next.lang=settings.lang;
        next.native = !fromRemote && !account.signedIn(); control.add(next, true); nextPending = true;
      } else {
        nextPending = false;
        if (account.signedIn()) {  // fila local: só o estado do joinha (a resposta atualiza `likeState`)
          Job rating{5, "/api/video/" + v.id + "/next", 0}; rating.lang=settings.lang; control.add(rating, true);
        }
      }
    }
    if (v.id != playing.id) { captions = {}; captionIndex = -1; captionLines.clear(); captionVideo.clear(); captionLoader.cancel(); }
    if (v.id != playing.id) { audioTracks.clear(); trackLabels.clear(); selectedTrackLabel.clear(); playerUI.tracks=false; }
    nextAt = nextDeadline = 0; endedSent = false; ratePanel = sleepPanel = false; livePaused = false;
    playing = v; playerFromRemote = fromRemote;
    if(localOpening)localRecording=!account.signedIn();
    videoTex.ready = false;
    phonePanel = loginPanel = logoutPanel = false;
    pl.nestOffset = nestOffsetMs / 1000.0;
    if (screen != PLAYER) nav.musicNow = nav.musicSession;
    pl.audioOnly = nav.musicSession && nav.musicNow && !nav.videoMode;
    if (pl.audioOnly) { pl.speed=1; pl.loop=false; }
    if (pl.requestLanguage != i18n::code(settings.lang)) { pl.halt(); pl.requestLanguage=i18n::code(settings.lang); }
    pl.start(srv, v.id, t, v.title, v.dur, quality, nestOut && nestAvailable && !nestSkip, !fromRemote || !net::serverEnabled || phonePlaybackSerial != 0,false,settings.videoMode,track,v.members != yt::Membership::None);
    if (screen != PLAYER) { backTo = screen; nav.musicNow = nav.musicSession; }
    seekHold.cancel();
    screen = PLAYER; playerUI.reset(SDL_GetTicks());
  };
  auto requestVideo = [&](const Video& v,double t=0) {
    bool localPage=(screen==NAV && !nav.pages.empty() && (nav.page().kind=="local_later" || nav.page().kind=="local_history")) ||
        (screen==HOME && !inResults && (cats[cat].kind=="local_later" || cats[cat].kind=="local_history"));
    double pos=(!account.signedIn() || localPage)?localUI.library.position(v.id):0;
    if(pos>0){localUI.pending=v;if(localUI.pending.title.empty())if(const auto* e=local::Library::find(localUI.library.history,v.id))localUI.pending.title=e->title;localUI.resumeAt=pos;localUI.selected=0;localUI.resume=true;}
    else startVideo(v,t,false);
  };
  if(localUI.corrupt){toast=tr(K::LocalCorrupt);toastUntil=SDL_GetTicks()+7000;LOG("local: unreadable file; original retained until explicit change");}
  auto openQuality = [&]() {
    playerUI.panel=PlayerUI::QUALITY;playerUI.panelSel=offeredQualities.index(settings.videoMode);playerUI.touch(SDL_GetTicks());
    qualityHelpLines=gfx::wrap(g,ui::F_SMALL,tr(K::ManualQualityHelp),648,2);
  };
  auto sponsorSeek = [&](sponsorblock::Action action) {
    bool paused=pl.paused;
    LOG("SponsorBlock: %s %.3f -> %.3f",action.kind==sponsorblock::Action::UNDO ? "undo" : "skip",pl.position(),action.target);
    startVideo(playing,action.target,playerFromRemote); pl.setPaused(paused);
  };
  auto stopVideo = [&]() {
    seekHold.cancel();
    recordHistory(); localRecording=false; historyVideo.clear(); historyStarted = false;
    extras.select("",i18n::code(settings.lang));
    sponsors.reset(); sponsorWorker.select("");
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

  auto railPage = [&]() {
    if (screen == NAV && !nav.pages.empty()) {
      const auto& root = nav.pages.front();
      return root.music ? 4 : root.kind == "subscriptions" ? 2 : root.kind == "library" ? 3 : root.kind == "search" ? 1 : 0;
    }
    if (screen == PLAYER && nav.musicNow) return 4;  // o trilho aparece no Tocando agora, com a Música marcada
    return screen == SETTINGS ? 6 : screen == SEARCH || inResults ? 1 : 0;
  };
  auto openNav = [&](const std::string& kind, const std::string& id = "", const std::string& q = "", bool music = false, yt::search::Filters filters = {}) {
    if (screen != NAV) { navReturn = screen == SEARCH ? searchBack : screen; navReturnRail = railFocus; nav.pages.clear(); }
    nav.signedIn=account.signedIn();
    nav.open(navWorker, settings.lang, kind, id, q, music, filters);
    nav.refreshLocal(g,settings.lang); screen = NAV; railFocus = false;
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
    else {screen = NAV;nav.localRevision=0;nav.refreshLocal(g,settings.lang);}
  };
  auto openSearch = [&]() {
    if (screen==NAV && nav.page().kind=="search") searchUI.filters=nav.page().filters;
    searchBack = screen; searchMusic = screen == NAV && nav.page().music;
    screen = SEARCH; searchUI.resetFocus(); railFocus = false;
  };
  auto doSearch = [&](std::string q) {
    if (q.empty()) return;
    query = q; recent.erase(std::remove(recent.begin(), recent.end(), q), recent.end()); recent.insert(recent.begin(), q);
    if (recent.size() > 6) recent.pop_back();
    if (screen == SEARCH) screen = searchBack;
    openNav(searchMusic ? "music_search" : "search", "", q, searchMusic, searchMusic ? yt::search::Filters{} : searchUI.filters);
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
      nav.open(navWorker, settings.lang, page.kind, page.id, page.query, page.music, page.filters);
    }
  };
  auto accountPhoto = [&]() {
    if (!account.signedIn() || account.avatar.empty()) return;
    if (const auto* slot = thumbs.get(thumbsWorker, account.avatar, frame)) {
      gfx::rect(g, ui::W-74, 24, 44, 44, {1,1,1,0.20f}, 22);
      gfx::image(g, slot->tex, ui::W-72, 26, 40, 40, 20, {1,1,1,1}, (float)thumbs.height / thumbs.width);
    }
  };
  if (const char* pv = std::getenv("XT_PLAY")) { Video v; v.id = pv; v.shortForm = std::getenv("XT_PLAY_SHORT") != nullptr; requestVideo(v); }
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
#ifdef TUBE360_F33_BENCH
  f33::PlaybackProbe f33Playback(assets,SDL_GetTicks());
#endif
#ifdef TUBE360_F40_BENCH
  f40::PlaybackProbe f40Playback(assets,SDL_GetTicks());
#endif
  const char* screenNames[] = {"Home", "Busca", "Player", "Ajustes", "Navegacao"};
#ifndef __XSDK__
  searchbench::Frames searchFrames;
#endif
  // ---- F61 Início e coleção: estado novo do run() só aqui

  // ---- F62 Busca e canal: estado novo do run() só aqui

  // ---- F63 Player e Shorts: estado novo do run() só aqui
  pl.setVolumePercent(settings.volume);  // volume salvo é só da TV; o app nunca manda SET_VOLUME ao Nest na abertura
  PlayerWatch playerWatch;

  // ---- F64 Configurações e controle: estado novo do run() só aqui

  // ---- F65 Música: estado novo do run() só aqui

  // ---- F66 controle pelo celular: estado novo do run() só aqui

  // ---- F67 membros tocando: estado novo do run() só aqui

  UiContext screens{
    // compartilhado
    .g=g,
    .settings=settings,
    .in=in,
    .thumbs=thumbs,
    .thumbsWorker=thumbsWorker,
    .nav=nav,
    .localUI=localUI,
    .account=account,
    .frame=frame,
    .quit=quit,
    .toast=toast,
    .toastUntil=toastUntil,
    .railFocus=railFocus,
    .railSel=railSel,
    .scroll=scroll,
    .sleep=sleep,
    .ambient=ambient,
    .openSearch=openSearch,
    .goHome=goHome,
    .accountPhoto=accountPhoto,
    .openNavFn=openNav,
    .requestVideoFn=[&](const Video& v, double t) { requestVideo(v, t); },
    .pl=pl,
    .control=control,
    .playing=playing,
    .playerFromRemote=playerFromRemote,
    .screen=screen,
    .startVideoFn=startVideo,
    .navWorker=navWorker,
    .railOpen=railOpen,
    .srv=srv,
    .consoleAccount=consoleAccount,
    .stopVideo=stopVideo,
    .openChannelForVideo=openChannelForVideo,
    // Início
    .zone=zone,
    .inResults=inResults,
    .cat=cat,
    .cats=cats,
    .results=results,
    .serverAddress=serverAddress,
    .castWho=castWho,
    .requestFeedFn=requestFeed,
    // Casca: painéis, conta e timer
    .sleepChoice=sleepChoice,
    .accountBusy=accountBusy,
    .accountKnown=accountKnown,
    .accountFailed=accountFailed,
    .logoutPanel=logoutPanel,
    .applySleep=applySleep,
    .beginLogin=beginLogin,
    .sleepLabel=sleepLabel,
    .accountAt=accountAt,
    .nestInfoAt=nestInfoAt,
    .phonePanel=phonePanel,
    .phonePlaybackSerial=phonePlaybackSerial,
    .railPage=railPage,
    .shutdownAt=shutdownAt,
    .sleepPanel=sleepPanel,
    .castCode=castCode,
    .phoneStatus=phoneStatus,
    .loginPanel=loginPanel,
    .loginError=loginError,
    .sleepPending=sleepPending,
    .requestAccount=requestAccount,
    .requestSleep=requestSleep,
    // Busca
    .latinKeyboard=latinKeyboard,
    .query=query,
    .searchBack=searchBack,
    .searchMusic=searchMusic,
    .searchUI=searchUI,
    .doSearch=doSearch,
    .navBack=navBack,
    // Configurações
    .sponsorUI=sponsorUI,
    .speakerPicker=speakerPicker,
    .saveSettings=saveSettings,
    .updateUI=updateUI,
    .updater=updater,
    .settingsRow=settingsRow,
    .speakerChoice=speakerChoice,
    .nestAvailable=nestAvailable,
    .nestChecked=nestChecked,
    .nestName=nestName,
    .nestOffsetMs=nestOffsetMs,
    .nestOut=nestOut,
    .nestSkip=nestSkip,
    .loadDisplay=loadDisplay,
    .rebuildCats=rebuildCats,
    // Player
    .offeredQualities=offeredQualities,
    .defaultQualities=defaultQualities,
    .qualityKnown=qualityKnown,
    .overlayTitle=overlayTitle,
    .overlayMeta=overlayMeta,
    .overlayChapter=overlayChapter,
    .tipLabel=tipLabel,
    .tipValue=tipValue,
    .metaChannel=metaChannel,
    .clockSecond=clockSecond,
    .audioTracks=audioTracks,
    .captionIndex=captionIndex,
    .captionInterface=captionInterface,
    .captionVideo=captionVideo,
    .captionLines=captionLines,
    .captionLineWidths=captionLineWidths,
    .captionLoader=captionLoader,
    .captions=captions,
    .captionWidth=captionWidth,
    .clockDuration=clockDuration,
    .clockLabel=clockLabel,
    .clockWidth=clockWidth,
    .commentAuthor=commentAuthor,
    .commentMeta=commentMeta,
    .commentIndex=commentIndex,
    .commentLines=commentLines,
    .commentPosition=commentPosition,
    .extras=extras,
    .lastMode=lastMode,
    .lastOutput=lastOutput,
    .lastQuality=lastQuality,
    .lastSourceFps=lastSourceFps,
    .lastTargetFps=lastTargetFps,
    .likeState=likeState,
    .manualNoticeLines=manualNoticeLines,
    .manualNoticeUntil=manualNoticeUntil,
    .metaDate=metaDate,
    .metaLang=metaLang,
    .nextAt=nextAt,
    .playerUI=playerUI,
    .overlayUntil=playerUI.until,
    .qualityHelpLines=qualityHelpLines,
    .qualityLabel=qualityLabel,
    .resolutionLabel=resolutionLabel,
    .seekLabel=seekLabel,
    .ratePanel=ratePanel,
    .rateSel=rateSel,
    .seekHold=seekHold,
    .seekSecond=seekSecond,
    .selectedAudio=selectedAudio,
    .selectedTrackLabel=selectedTrackLabel,
    .sponsors=sponsors,
    .trackLabels=trackLabels,
    .trackLang=trackLang,
    .upNext=upNext,
    .videoTex=videoTex,
    .nextDeadline=nextDeadline,
    .endedSent=endedSent,
    .livePaused=livePaused,
    .nextPending=nextPending,
    .openPlayerChannel=openPlayerChannel,
    .openQuality=openQuality,
    .resumeLive=resumeLive,
    .sponsorSeek=sponsorSeek,
    .manualNoticeFont=manualNoticeFont,
    .manualNoticeLang=manualNoticeLang,
    .manualNoticeWidth=manualNoticeWidth,

    // ---- F61 Início e coleção

    // ---- F62 Busca e canal

    // ---- F63 Player e Shorts
    .related=related,
    .shelf=shelf,
    .shortsQueue=shortsQueue,
    .playedBefore=playedBefore,
    .historyBack=historyBack,
    .shelfChannel=shelfChannel,
    .shelfScratch=shelfScratch,
    .shortTitle=shortTitle,
    .shortLang=shortLang,
    .shortLines=shortLines,

    // ---- F64 Configurações e controle

    // ---- F65 Música

    // ---- F66 controle pelo celular

    // ---- F67 membros tocando
  };
  boot_event("begin","app.first-frame","enter render loop; no disk logging each frame after ready");
  while (!quit) {
#ifndef __XSDK__
    searchFrames.tick(screen==SEARCH);
#endif
    uint64_t now = SDL_GetTicks();
#ifndef __XSDK__
    screens.errorShot={};
#endif
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
              : k == SDLK_Q ? Input::LT : k == SDLK_E ? Input::RT : k == SDLK_PAGEUP ? Input::LB : k == SDLK_PAGEDOWN ? Input::RB : k == SDLK_F10 ? Input::BACK : k == SDLK_F2 ? Input::START : -1;
        if (b >= 0) in.set(b, d, now);
      }
      if (e.type == SDL_EVENT_TEXT_INPUT && screen == SEARCH && !searchUI.filterOpen && query.size()+std::strlen(e.text.text)<=256) query += e.text.text;
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
    boot_session_heartbeat();
    if (shotPath) {
      in.clear();
      static const char* script = std::getenv("XT_KEYS");
      int step = (int)frame - 40;
#ifndef __XSDK__
      static int interval = std::getenv("XT_KEYS_MS") ? std::atoi(std::getenv("XT_KEYS_MS")) : 0;
      static int keyIndex = 0; static uint64_t keyAt = runStart + (uint64_t)std::max(0, interval);
      if (interval > 0) { step = -1; if (now >= keyAt) { step = keyIndex++ * 24; keyAt = now + interval; } }
#endif
      if (script && step >= 0 && step % 24 == 0 && step / 24 <= (int)std::strlen(script)) {
        char k = script[step / 24];
        int b = k == 'U' ? Input::UP : k == 'D' ? Input::DOWN : k == 'L' ? Input::LEFT : k == 'R' ? Input::RIGHT : k == 'A' ? Input::A
              : k == 'B' ? Input::B : k == 'X' ? Input::X : k == 'Y' ? Input::Y : k == 'S' ? Input::START : k == '[' ? Input::LB : k == ']' ? Input::RB : k == 'l' ? Input::LT : k == 'r' ? Input::RT : -1;
        in.set(Input::LT,k=='l',now);in.set(Input::RT,k=='r',now);
        if (b >= 0) in.fired[b] = true;
      }
    }

    updateUI.poll(updater,g,settings.lang);
    updater.tick(screen==HOME || screen==SETTINGS,frame>60 && now-runStart>=3000);

    // ---------------------------------------------------------------- resultados do trabalho em segundo plano
    if (accountBackend != consoleAccount->revision()) {
      accountBackend = consoleAccount->revision();
      accountBusy = false; accountKnown = false; accountAt = 0; account = {}; rebuildCats();
      nav.pages.clear(); nav.queue.clear(); nav.busy = false;
      if (screen == NAV) screen = HOME;
    }
    if (speakerPicker.poll(pl.directSpeaker.finder())) {
      auto choice = pl.directSpeaker.settings();
      speakerChoice = choice;
      if (!choice.host.empty() || !net::serverEnabled) {
        nestAvailable = !choice.host.empty(); nestChecked = true;
        nestName = nestAvailable ? choice.name : tr(K::Nest);
      }
      if (speakerPicker.result.state == nest::SearchState::SaveError) { toast=tr(K::SaveFailed); toastUntil=now+4000; }
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
    if (!net::serverEnabled && sleep.active && !shutdownAt &&
        ((!sleep.end && sleepLeft <= 0) || (sleep.end && endTimerPending))) {
      LOG("timer: prazo do console venceu, desligando");
      if (screen == PLAYER) stopVideo();
      sleep = {}; shutdownAt = now + 2000; sleepPanel = false;
    }
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
          related.clear();
          for (const auto& it : r.feed.items) if (it.id != playing.id && related.size() < 24) related.push_back(it);
        } else if (ok && id == playing.id && f.size() >= 5) {
          if (nav.localQueue) { likeState = f[1]; continue; }
          likeState = f[1];
          upNext = Video{};
          if (f[2].size() == 11) { upNext.id = f[2]; upNext.channel = f[3]; upNext.title = f[4]; related.assign(1, upNext); }
          size_t nl = r.feed.title.find('\n');
          while (nl != std::string::npos && related.size() < 24) {
            size_t end = r.feed.title.find('\n', nl + 1);
            auto row = net::split(r.feed.title.substr(nl + 1, end == std::string::npos ? std::string::npos : end - nl - 1), '\t');
            nl = end;
            if (row.size() < 5 || row[0] != "rel" || row[1].size() != 11 || row[1] == playing.id || row[1] == upNext.id) continue;
            Video v; v.id = row[1]; v.title = row[2]; v.channel = row[3]; v.dur = std::atoi(row[4].c_str());
            if (row.size() > 5) v.members = yt::membershipWire(row[5]);
            v.shortForm = row.size() > 6 && row[6] == "short";
            related.push_back(std::move(v));
          }
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
            // Fila nova zera o estado da tela ANTES de abrir a faixa (senão o modo Vídeo velho decodificaria vídeo na tela Música); a
            // continuação da reprodução automática segue no modo em que a pessoa estava.
            const bool continued = nav.continuing, keepVideo = continued && nav.videoMode;
            nav.continuing = false; nav.resetNow(); nav.videoMode = keepVideo;
            nav.queue = std::move(queue.items); nav.nowId.clear(); nav.ownsQueue = true;
            nav.localQueue = r.catalog || r.localAccount; nav.queueIndex = 0;
            if (nav.localQueue && !nav.queue.empty()) {
              for (size_t i = 0; i < nav.queue.size(); ++i) if (nav.queue[i].id == nav.queueStart) nav.queueIndex = i;
              if (continued && nav.queue.size() > 1 && nav.queue[0].id == playing.id) nav.queueIndex = 1;  // a rádio começa na faixa que acabou
              startVideo(nav.queue[nav.queueIndex].video(),0,false);
            }
          }
          nav.lyrics.clear(); nav.lyricsId.clear(); nav.lyricLines.clear(); nav.lyricScroll = 0;
        } else if (r.key.rfind("/api/nav/related?", 0) == 0) {
          NavPage page; Navigation::parse(r.feed.title, page);
          if (r.key == "/api/nav/related?id=" + playing.id + "&lang=" + i18n::code(settings.lang)) { nav.related = std::move(page.items); nav.relatedLoaded = true; nav.relatedSel = nav.relatedTop = 0; nav.queueChanged(); }
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
          if (r.key.rfind("/api/nav/related?", 0) == 0) { nav.relatedLoaded = true; continue; }
          if (nav.continuing && r.key.rfind("/api/nav/play?", 0) == 0) { nav.continuing = false; stopVideo(); continue; }  // a rádio não veio: acaba como acabaria sem a reprodução automática
          if (r.key.rfind("/api/nav/lyrics?", 0) == 0) { nav.lyricsFailed = true; continue; }  // erro de rede: a aba diz que falhou, sem aviso por cima  // sem letra: a aba mostra "Letra indisponível", sem aviso
          auto fields = net::split(r.feed.title.substr(0, r.feed.title.find('\n')), '\t');
          toast = fields.size() > 1 ? fields[1] : tr(K::ActionFailed); toastUntil = now + 4500;
        }
      } else {
        if (shelfChannel.stamp && r.stamp == shelfChannel.stamp) {
          // "Do canal" da fileira do player: mesma leitura da página de canal, numa página à parte.
          Navigation scratch; scratch.pages.push_back(shelfChannel); scratch.arrived(r, g, settings.lang);
          shelfChannel = scratch.pages.front(); shelfChannel.loading = false;
        } else nav.arrived(r, g, settings.lang);
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
            target->feed.more = r.localAccount ? r.feed.more : r.feed.items.size() >= 24;
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
    searchUI.update(g,query,recent,settings.lang,screen==SEARCH && !searchMusic && !searchUI.filterOpen);
    if (screen==NAV && !nav.pages.empty() && nav.page().kind=="search") {
      auto& p=nav.page();
      if (p.filterLabel.empty()) {
        p.filterLabel=SearchUI::summary(p.filters,settings.lang);
        p.filterLines=gfx::wrap(g,ui::F_CHAN,p.filterLabel,ui::W-440,2);
      }
    }
    if(screen==PLAYER && now>=localUI.checkpoint){recordLocal();localUI.checkpoint=now+30000;}
    auto localRevision=localUI.submitted;
    localUI.tick(toast,toastUntil,now,settings.lang);
    if(localRevision!=localUI.submitted) for(auto& shelf:cats)if(shelf.kind=="local_later" || shelf.kind=="local_history"){requestFeed(shelf,3);if(!shelf.feed.items.empty())shelf.sel=std::clamp(shelf.sel,0,int(shelf.feed.items.size())-1);else shelf.sel=0;}
    nav.refreshLocal(g,settings.lang);
    // ---------------------------------------------------------------- lógica por tela
    // código de pareamento e quem está conectado pelo Transmitir (o código troca a cada 5 min)
    if (net::serverEnabled && !pl.directSpeaker.configured() && now >= nestInfoAt) { control.add({5, "/api/nest/info", 0}); nestInfoAt = now + 60000; }
    if (!accountBusy && now >= accountAt) requestAccount("/api/account");
    bool alternateKeyboard=settings.lang==i18n::Lang::AR && latinKeyboard;
    auto keyboardText=[&](K k) { return i18n::get(alternateKeyboard ? i18n::Lang::EN : settings.lang,k); };
    const char* keys[] = {keyboardText(K::Keyboard1), keyboardText(K::Keyboard2), keyboardText(K::Keyboard3), keyboardText(K::Keyboard4)};
    bool behindLive = screen == PLAYER && pl.live && (pl.paused || (pl.shownFrames &&
        (pl.liveDebt >= 1.0 || (pl.lastFrameMs && now > pl.lastFrameMs + 1000))));
    if (extras.poll()) { commentIndex=-1; }
    if (sponsorWorker.poll(sponsors,screen==PLAYER && pl.state==player::PLAYING && !pl.live && !nav.musicNow,now))
      LOG("SponsorBlock: %u trechos pos=%.3f estado=%d %s",(unsigned)sponsors.segments.size(),pl.position(),(int)pl.state,sponsorWorker.diagnostic.line().c_str());
    if (screen==PLAYER && !pl.live && !nav.musicNow && !shutdownAt) {
      auto action=sponsors.update(pl.position(),pl.duration,now,settings.sponsorBlock,pl.state==player::PLAYING && !pl.paused);
      if (action) sponsorSeek(action);
    }
    auto commentPanel=display::commentsPanel(ui::W,g.fonts[ui::F_H2].lineH,g.fonts[ui::F_SMALL].lineH,g.fonts[ui::F_BODY].lineH);
    int optionCount = behindLive ? PlayerUI::COUNT : PlayerUI::EDGE;
    if (playerUI.selected >= optionCount) playerUI.selected = PlayerUI::CHANNEL;
    // entrada: o painel aberto mais por cima captura o controle; sem painel, vale a tela (métodos de ui_*.hpp)
    // ---- F64 Configurações e controle
    screens.sleepWarning = sleepWarning;
    screens.pageMenuIntercept();
    if (shutdownAt) {
      if (now >= shutdownAt) {
        if (powerDownXbox()) quit = true;
        else { shutdownAt = 0; toast = tr(K::SleepError); toastUntil = now + 6000; }
      }
    } else if (updateUI.input(updater,in,screen==HOME && !phonePanel && !loginPanel && !logoutPanel && !nav.menu && !localUI.resume && localUI.clear<0 && !sleepPanel && !sleepWarning && !shutdownAt,screen==SETTINGS && !localUI.resume && localUI.clear<0 && !speakerPicker.open && !loginPanel && !logoutPanel)) {
    } else if (sleepWarning) {
      screens.sleepWarningInput();
    } else if (sleepPanel) {
      screens.sleepPanelInput(now);
    } else if (ratePanel && (screen != PLAYER || pl.state == player::FAILED)) {
      ratePanel = false;  // o vídeo parou ou falhou com o painel aberto: o A não pode avaliar por trás da grade
    } else if (ratePanel) {
      screens.ratePanelInput(now);
    } else if (screen == PLAYER && playerUI.panel != PlayerUI::NONE) {
      screens.playerPanelInput(now,commentPanel);
    } else if (screen == PLAYER && nextAt) {
      screens.playerNextInput();
    } else if (loginPanel) {
      screens.loginInput(now);
    } else if (logoutPanel) {
      screens.logoutInput();
    } else if (phonePanel) {
      screens.phoneInput();
    } else if (searchUI.filterOpen) {
      screens.filtersInput();
    } else if (localUI.resume || localUI.clear>=0) {
      screens.localInput(now);
    } else if (nav.menu) {
      screens.videoMenuInput(now);
    } else if (nav.unsubscribe) {
      screens.unsubscribeInput();
    } else if (screen == NAV && !railFocus && !nav.pages.empty()) {
      screens.navInput(now);
    } else if (screen == SETTINGS) {
      if (auto code = screens.settingsInput(now)) return *code;
    } else if ((screen == HOME || screen == NAV || screen == SEARCH) && railFocus) {
      screens.railInput();
    } else if (screen == HOME) {
      screens.homeInput(now);
    } else if (screen == SEARCH) {
      screens.searchInput(keys);
    } else if (screen == PLAYER && nav.musicNow) {
      screens.musicNowInput(now);
    } else if (screen == PLAYER) {
      screens.playerInput(now,optionCount);
    }
    screens.playerUpdate(now);
    // ---- F63 Player e Shorts: vigia do player (marca a sessão para o relatório de erro)
    {
      using Phase = PlayerWatch::Phase;
      PlayerWatch::Sample w; w.now = now; w.inPlayer = screen == PLAYER;  // inclui o Tocando agora: música travada também gera relatório
      w.phase = pl.state == player::CONNECTING ? Phase::Connecting : pl.state == player::BUFFERING ? Phase::Buffering :
                pl.state == player::PLAYING ? Phase::Playing : Phase::Idle;
      w.paused = pl.paused; w.live = pl.live; w.nest = pl.nest || pl.speakerRequested; w.seeking = seekHold.active; w.position = pl.position();
      w.bytes = (uint64_t)SDL_GetAtomicInt(&pl.netBytes); w.frames = pl.framesIn; w.streamEnded = pl.decDone || (pl.playingAudio && pl.audioDone);  // música só com áudio termina por `audioDone`
      if (playerWatch.update(w)) {
        boot_session_player_stuck(playerWatch.stuck);
        LOG("vigia: player %s (estado=%d pos=%.1f bytes=%llu quadros=%llu fim=%d)", playerWatch.stuck ? "parado" : w.inPlayer ? "voltou" : "saiu da tela", (int)pl.state, w.position, (unsigned long long)w.bytes, (unsigned long long)w.frames, (int)w.streamEnded);
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
    if(screen==PLAYER && pl.state==player::ENDED && localUI.library.position(playing.id)>0)recordLocal();
    if (screen==PLAYER && pl.state==player::ENDED && !shutdownAt && audio::repeatAtEnd(pl.loop,pl.live,sleep.active && sleep.end)) {
      LOG("repetir: %s",playing.id.c_str()); startVideo(playing,0,playerFromRemote);
    }
    // Fim natural continua sendo informado mesmo com um cartão aberto.
    if (screen == PLAYER && nav.localQueue && pl.state == player::ENDED) {
      SDL_LockMutex(remote.mu); remote.ended = playing.id; SDL_UnlockMutex(remote.mu);
      size_t following = 0;
      if (nav.advance(following) && !(sleep.active && sleep.end)) {
        nav.queueIndex = following; if (nav.nowZone != MusicNavigation::ZoneQueue) nav.queueSel = (int)following; startVideo(nav.queue[following].video(),0,false);
      } else if (nav.autoplay && !nav.queue.empty() && !(sleep.active && sleep.end)) {
        // Reprodução automática: acabou a fila, segue com a rádio da última faixa (a resposta troca a fila e continua dali)
        if (!nav.continuing) {  // liga já: o pedido sai quando o worker estiver livre (letra ou relacionados podem estar em andamento)
          nav.continuing = true; nav.cancelQueue = false; nav.queueStart.clear(); nav.radioFor = playing.id;
          nav.queueTitle = std::string(tr(K::MusicMixOf)) + " " + playing.title;
        }
      } else { endTimerPending = sleep.active && sleep.end; stopVideo(); }
    }
    if (nav.continuing && !nav.radioFor.empty() && !nav.busy) {
      nav.action(navWorker,"/api/nav/play?kind=radio&id="+net::urlEncode(nav.radioFor)+"&lang="+i18n::code(settings.lang)); nav.radioFor.clear();
    }
    // ---- F63 Player e Shorts: fim de um Short emenda no próximo da fila (ou repete o último), sem o "a seguir em 8 s" de vídeo comum
    if (screen == PLAYER && pl.state == player::ENDED && playing.shortForm && !playerFromRemote && !shutdownAt && !nav.localQueue && !(sleep.active && sleep.end)) screens.shortsEnded();
    if (screen == PLAYER && pl.state == player::ENDED && !nextDeadline) nextDeadline = now + 8000;
    if (screen == PLAYER && pl.state == player::ENDED && !shutdownAt && !nav.continuing && (playerFromRemote || !nextPending || now >= nextDeadline)) {
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
                     speakerVolume ? speaker.volume*100 : 100, speakerVolume && speaker.muted, playerPositionRevision});
      phoneStateAt = now + 250;
    }

    if (focusVid && screen == HOME) { if (const Thumbs::Slot* s = thumbs.get(thumbsWorker, focusVid->id, frame, cur.feed.direct)) ambientWant = s->avg; }
    ambient = ui::mix(ambient, ambientWant, 0.06f);
    railOpen += ((railFocus ? 1.f : 0.f) - railOpen) * 0.25f;

    // ---------------------------------------------------------------- quadro do vídeo
    thumbs.configure(g, thumbsWorker, ui::columns(settings.cols), screen == PLAYER && nav.musicNow,
                     screen == NAV && !nav.pages.empty() && !nav.page().banner.empty());
    gfx::begin(g);
    gfx::rect(g, 0, 0, ui::W, ui::H, ui::BG);
    if (screen == PLAYER) {
      if (player::Frame* f = pl.take()) {
        if (f->surface) gfx::videoUse(g,videoTex,f->surface,f->pts);
        else {
          if (videoTex.surface || f->w != videoTex.w || f->h != videoTex.h) { if (videoTex.set) gfx::videoDestroy(g, videoTex); gfx::videoCreate(g, videoTex, f->w, f->h); LOG("vídeo %dx%d", f->w, f->h); }
          gfx::videoUpload(g, videoTex, f->yuv.data(), f->yuv.size(), f->pts);
        }
        videoTex.bt709 = f->bt709; videoTex.fullRange = f->fullRange;
        pl.recycle(f);
      }
    }

    // ---------------------------------------------------------------- desenho
    double t = now / 1000.0;
    // ambiente: brilho grande e suave na cor do vídeo em foco, por cima do fundo e do destaque (assim não há emenda)
    if (screen == SEARCH) screens.glow();

    if (screen == HOME) {
      screens.homeDraw(cur,t,sleepLeft);
    } else if (screen == SEARCH) {
      screens.searchDraw(keys,t);
    }

    if (screen == SETTINGS) screens.settingsDraw();

    if (screen == NAV) { nav.columnsSetting = settings.cols; nav.draw(g, thumbs, thumbsWorker, frame, now, settings.lang, !railFocus, settings.hideShorts); accountPhoto(); }

    if (screen == HOME || screen == SEARCH || screen == SETTINGS || screen == NAV) {
      screens.railDraw(remoteOnline);
    }

    if (screen == PLAYER && nav.musicNow) { screens.musicNowDraw(); screens.railDraw(remoteOnline); }  // o trilho por cima do degradê do fundo
    else if (screen == PLAYER) screens.playerDraw(now,t,optionCount,behindLive,commentPanel,sleepWarning,sleepLeft);

    if (sleep.active && screen == PLAYER && nav.musicNow) {
      screens.musicSleepDraw(sleepLeft);
    }
    screens.overlaysDraw(now,t,sleepWarning,sleepLeft);
    bool shoot = shotPath && (int)frame == shotFrame;
#ifndef __XSDK__
    if (shotPath && std::getenv("XT_SHOT_MS")) shoot = now - runStart >= (uint64_t)std::atoi(std::getenv("XT_SHOT_MS"));
#endif
    gfx::end(g, shoot ? shotPath : nullptr);
    if (shoot) {
      LOG("captura: frame %llu, tempo %llu ms, tela %d, vídeo %s, posição %.2f", (unsigned long long)frame, (unsigned long long)(now - runStart), (int)screen, playing.id.c_str(), pl.position());
      LOG("captura conta: conectada=%d foto=%d", account.signedIn(), !account.avatar.empty());
#ifndef __XSDK__
      if(screen==PLAYER){
        LOG("captura qualidade: modo=%s fixa=%d mascara=%x painel=%d selecao=%d fonte=%d pausa=%d aviso=%d estado=%d erro=%s",playback::modeName(settings.videoMode),playback::manual(settings.videoMode),pl.qualityAvailable.load(),playerUI.panel,playerUI.panelSel,pl.actualHeight.load(),pl.paused.load(),now<manualNoticeUntil,pl.state.load(),pl.state==player::FAILED?pl.error.c_str():"");
        if(playerUI.panel==PlayerUI::QUALITY)for(int i=0;i<offeredQualities.count;++i)LOG("captura opcao qualidade: %s | %s",playback::modeName(offeredQualities.modes[i]),tr(videoModeLabel(offeredQualities.modes[i])));
      }
      if(screens.errorShot.feed)LOG("captura erro Home: preferencia=%d efetivo=%d | %s | %s | %s",settings.server,int(net::serverEnabled.load()),
          tr(screens.errorShot.title),tr(screens.errorShot.help),screens.errorShot.server?serverAddress.c_str():screens.errorShot.feed->errorDetail.c_str());
#endif
      LOG("captura busca: painel=%d tipo=%d duracao=%d data=%d ordem=%d sugestoes=%u lista=%d selecao=%d recentes=%u",
          searchUI.filterOpen,searchUI.filterOpen ? searchUI.draft.type : searchUI.filters.type,
          searchUI.filterOpen ? searchUI.draft.duration : searchUI.filters.duration,
          searchUI.filterOpen ? searchUI.draft.date : searchUI.filters.date,
          searchUI.filterOpen ? searchUI.draft.order : searchUI.filters.order,
          (unsigned)searchUI.remote.size(),searchUI.listFocus,searchUI.selected,(unsigned)recent.size());
      if (screen == NAV && !nav.pages.empty()) LOG("captura navegação: %s sel=%d canal=%d zona=%d pilha=%u menu=%d prateleira=%d",
          nav.page().kind.c_str(), nav.page().sel, nav.page().channelSel, nav.page().zone, (unsigned)nav.pages.size(), railFocus, nav.page().shelf);
      quit = true;
    }
    frame++;
#ifdef TUBE360_F33_BENCH
    if (f33Playback.update(pl,now)) quit=true;
#endif
#ifdef TUBE360_F40_BENCH
    if (f40Playback.update(pl,now)) quit=true;
#endif
    if (frame % 600 == 0 && screen == PLAYER) LOG("player: %s pos %.1f quadros %lld perdidos %lld", playing.id.c_str(), pl.position(), (long long)pl.shownFrames, (long long)pl.droppedFrames);
  }
  recordHistory();
  if(localUI.submitted!=localUI.library.revision)localUI.writer.submit(localUI.library);
  phone.stop();
  captionLoader.stop();
  extras.close();
  pl.halt();
  sponsorWorker.close();
  searchUI.worker.close();
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
  gfx::videoDestroy(g,videoTex);
  LOG("miniaturas: %u bytes GPU, pico de RGBA aguardando upload %u bytes", (unsigned)(thumbs.slots.size() * thumbs.slotBytes + thumbs.bannerBytes), (unsigned)thumbs.peakPending);
  LOG("%s saindo", brandName.c_str());
  boot_event("end","app.exit","normal shutdown frames=%llu run_ms=%llu simulated_frame_delay_ms=%u",(unsigned long long)frame,(unsigned long long)(SDL_GetTicks()-runStart),g.simFrameDelay);
  localUI.shutdown();  // F61 (#42): o gravador da Biblioteca termina antes de SDL_Quit e da destruição de mutex/condição; antes do boot_close para a última gravação ficar no log
  boot_close();
  SDL_Quit();
  return 0;
}

int main(int argc,char** argv) {
#ifdef __XSDK__
  boot_open("","app");
  xsdk_on_terminate(boot_session_terminated);
  boot_event("info","session.terminate","system termination callback registered; raw kernel marker");
#else
  boot_open(std::getenv("XT_ASSETS")?std::getenv("XT_ASSETS"):"assets/","app");
#endif
  boot_session_version(unsigned(updates::buildId),updates::appVersion);
  boot_event("info","main","C runtime reached main; build=%u version=%s",unsigned(updates::buildId),updates::appVersion);
  boot_environment();
  try {
    int result=run(argc,argv);
    if(result==0)boot_session_clean();
    return result;
  }
  catch(const std::exception& error){return startup::fail(K::StartupVideo,error.what());}
}
