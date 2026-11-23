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
#include "settings.hpp"
#include "account.hpp"
#include "sleep.hpp"
#include "icons.hpp"

#ifdef __XSDK__
#include "xsdk/net.h"
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
enum { F_BODY, F_SMALL, F_CARD, F_H2, F_LOGO, F_BIG, F_KEY, F_TINY, F_HERO, F_CHAN, F_LOGOL, F_CODE };
constexpr float W = 1280, H = 720, RAIL = 96, X0 = 136;
inline Color alpha(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }
inline Color mix(Color a, Color b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t}; }
}  // namespace ui

struct Video { std::string id, channel, views, title; int dur = -1; double pos = 0; };
struct Feed { std::string title; std::vector<Video> items; bool loading = false; K error = K::None; bool more = true, loadingMore = false; };
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
struct Job { int kind; std::string a; uint64_t stamp; bool fresh = false; int offset = 0; i18n::Lang lang = i18n::Lang::EN; std::string feedKind; int w = 320, h = 180; };
struct Result { int kind = -1; std::string key; Feed feed; std::vector<uint8_t> rgba; int w = 0, h = 0; uint64_t stamp = 0; int offset = 0; };

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
  net::Server srv; SDL_Mutex* mu = SDL_CreateMutex(); SDL_Condition* cv = SDL_CreateCondition();
  std::deque<Job> jobs; std::deque<Result> results; bool quit = false; int hw = 4;
  static int main(void* p) { ((Worker*)p)->loop(); return 0; }
  void add(Job j, bool front = false) { SDL_LockMutex(mu); if (front) jobs.push_front(j); else jobs.push_back(j); SDL_SignalCondition(cv); SDL_UnlockMutex(mu); }
  void loop() {
    player::moveToHardwareThread(hw);
    for (;;) {
      SDL_LockMutex(mu);
      while (jobs.empty() && !quit) SDL_WaitCondition(cv, mu);
      if (quit) { SDL_UnlockMutex(mu); return; }
      Job j = jobs.front(); jobs.pop_front();
      SDL_UnlockMutex(mu);
      Result r; r.kind = j.kind; r.key = j.a; r.stamp = j.stamp; r.offset = j.offset;
      net::Response resp;
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
        if (net::get(srv, "/api/thumb/" + (j.a[0] == '~' ? "nav/" + j.a.substr(1) : j.a) + size, resp, 10000)) {
          int w, h, n;
          // Não decodifica dimensões arbitrárias recebidas pela rede.
          if (!stbi_info_from_memory(resp.body.data(), (int)resp.body.size(), &w, &h, &n) || w != j.w || h != j.h) { w = h = 0; }
          unsigned char* px = w ? stbi_load_from_memory(resp.body.data(), (int)resp.body.size(), &w, &h, &n, 4) : nullptr;
          if (px) { r.rgba.assign(px, px + (size_t)w * h * 4); r.w = w; r.h = h; stbi_image_free(px); }
        }
      }
      SDL_LockMutex(mu); results.push_back(std::move(r)); SDL_UnlockMutex(mu);
    }
  }
  bool pop(Result& r) { SDL_LockMutex(mu); bool ok = !results.empty(); if (ok) { r = std::move(results.front()); results.pop_front(); } SDL_UnlockMutex(mu); return ok; }
};

// ---------------------------------------------------------------- controle pelo celular (poll a cada ~500 ms)
struct Remote {
  net::Server srv; SDL_Mutex* mu = SDL_CreateMutex(); std::deque<std::vector<std::string>> cmds; bool quit = false;
  std::string playing, ended; double pos = 0, dur = 0; bool paused = false; bool online = false;
  static int main(void* p) { ((Remote*)p)->loop(); return 0; }
  void loop() {
    player::moveToHardwareThread(5);
    while (!quit) {
      SDL_LockMutex(mu);
      char q[256];
      std::snprintf(q, sizeof(q), "/api/remote/poll?playing=%s&pos=%.1f&dur=%.1f&paused=%d&ended=%s", playing.c_str(), pos, dur, paused ? 1 : 0, ended.c_str());
      ended.clear();
      SDL_UnlockMutex(mu);
      net::Response r;
      bool ok = net::get(srv, q, r, 3000, 4096);
      SDL_LockMutex(mu);
      online = ok;
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
  std::vector<Slot> slots; std::map<std::string, int> byId; std::map<std::string, Result> waiting;
  static constexpr size_t budgetBytes = 64u * 320 * 180 * 4;
  int maxSlots = 64, width = 320, height = 180; uint64_t generation = 1;
  size_t slotBytes = 0, peakPending = 0;
  void configure(gfx::Gfx& g, Worker& worker, int cols) {
    int w = cols == 3 ? 384 : 320, h = cols == 3 ? 216 : 180;
    if (w == width && h == height) return;
    vkDeviceWaitIdle(g.dev);
    for (auto& s : slots) {
      vkFreeDescriptorSets(g.dev, g.pool, 1, &s.tex.set);
      vkDestroyImageView(g.dev, s.tex.view, nullptr); vkDestroyImage(g.dev, s.tex.img, nullptr); vkFreeMemory(g.dev, s.tex.mem, nullptr);
    }
    slots.clear(); byId.clear(); waiting.clear(); generation++;
    width = w; height = h; maxSlots = 64; slotBytes = peakPending = 0;
    SDL_LockMutex(worker.mu); worker.jobs.clear(); worker.results.clear(); SDL_UnlockMutex(worker.mu);
  }
  const Slot* get(Worker& w, const std::string& id, uint64_t frame) {
    auto it = byId.find(id);
    if (it != byId.end()) { Slot& s = slots[(size_t)it->second]; s.used = frame; return &s; }
    if (!waiting.count(id) && waiting.size() < 24) {
      Job j{2, id, generation}; j.w = width; j.h = height; w.add(j); waiting[id] = Result{};
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
      if (r.rgba.empty() || r.w != width || r.h != height) { it = waiting.erase(it); continue; }
      int idx;
      if ((int)slots.size() < maxSlots) {
        slots.push_back({}); idx = (int)slots.size() - 1;
        slots[(size_t)idx].tex = gfx::makeImage(g, width, height, VK_FORMAT_R8G8B8A8_UNORM, true);
        if (!slotBytes) {
          VkMemoryRequirements req; vkGetImageMemoryRequirements(g.dev, slots[(size_t)idx].tex.img, &req);
          slotBytes = (size_t)req.size;
#ifdef __XSDK__
          // xsdk_gpu_alloc reserva páginas físicas de 64 KiB, além do requisito Vulkan.
          slotBytes = (slotBytes + 65535u) & ~size_t(65535u);
#endif
          maxSlots = std::min(64, (int)(budgetBytes / slotBytes));
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
  const float s = (float)icons::SIZES[big ? 0 : 1], u = (float)(which * icons::CELL), v = big ? 0.f : (float)icons::SIZES[0];
  gfx::imagePart(g, iconTex, std::round(x), std::round(y), s, s, u / icons::W, v / icons::H, (u + s) / icons::W, (v + s) / icons::H, c);
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

#include "navigation.hpp"

int main(int argc, char** argv) {
  (void)argc; (void)argv;
#ifdef __XSDK__
  if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) { LOG("SDL: %s", SDL_GetError()); return 1; }
  std::string assets = "";
#else
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) { LOG("SDL: %s", SDL_GetError()); return 1; }
  std::string assets = std::getenv("XT_ASSETS") ? std::getenv("XT_ASSETS") : "assets/";
#endif
  if (!assets.empty() && assets.back() != '/') assets += '/';
  Settings settings; settings.load(assets);
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
  const char* shotPath = std::getenv("XT_SHOT");
  g.readback = shotPath != nullptr;
  if (!gfx::init(g, 1280, 720, "Tube360")) return 1;
  if (!gfx::loadFonts(g, assets, {{"inter-400.ttf", 19}, {"inter-400.ttf", 16}, {"inter-600.ttf", 17}, {"inter-600.ttf", 23},
                                  {"inter-700.ttf", 27}, {"inter-700.ttf", 34}, {"inter-600.ttf", 22}, {"inter-700.ttf", 13},
                                  {"inter-700.ttf", 42}, {"inter-600.ttf", 18}, {"inter-300.ttf", 27}, {"inter-700.ttf", 58}})) { LOG("fontes não carregaram"); return 1; }
  loadIcons(g);

  gfx::Video videoTex; g.video = &videoTex;
  player::Player pl; pl.init();
  Worker worker; worker.srv = srv;
  SDL_Thread* wt = SDL_CreateThread(Worker::main, "trabalho", &worker);
  // Conta e pareamento não esperam as buscas do feed.
  Worker control; control.srv = srv; control.hw = 5;
  SDL_Thread* ct = SDL_CreateThread(Worker::main, "conta", &control);
  Worker navWorker; navWorker.srv = srv; navWorker.hw = 5;
  SDL_Thread* nt = SDL_CreateThread(Worker::main, "navegação", &navWorker);
  Navigation nav;
  Worker sleepWorker; sleepWorker.srv = srv; sleepWorker.hw = 5;
  SDL_Thread* st = SDL_CreateThread(Worker::main, "timer", &sleepWorker);
  // miniaturas numa fila própria, com 3 threads: não esperam atrás das buscas das categorias
  Worker thumbsWorker; thumbsWorker.srv = srv; thumbsWorker.hw = 5;
  SDL_Thread* tw[3];
  for (auto& th : tw) th = SDL_CreateThread(Worker::main, "miniaturas", &thumbsWorker);
  Remote remote; remote.srv = srv;
  SDL_Thread* rt = SDL_CreateThread(Remote::main, "controle", &remote);
  Thumbs thumbs;
  thumbs.configure(g, thumbsWorker, settings.cols);
  std::vector<Shelf> cats;
  Shelf results; bool inResults = false; int cat = 0;
  Account account; bool accountBusy = false, accountKnown = false, accountFailed = false;
  uint64_t accountAt = 0, nestInfoAt = 0, stamp = 0;
  auto requestFeed = [&](Shelf& shelf, int kind, bool fresh = false, int offset = 0, bool front = false) {
    if (!offset) { shelf.stamp = ++stamp; shelf.feed.loading = true; shelf.feed.loadingMore = false; shelf.feed.error = K::None; }
    Job j{kind, shelf.query, shelf.stamp, fresh, offset}; j.lang = settings.lang; j.feedKind = shelf.kind;
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
    for (size_t i = 0; i < cats.size(); i++) requestFeed(cats[i], i == 0 ? 0 : 3);
    if (inResults) requestFeed(results, 1, false, 0, true);
  };
  rebuildCats();

  enum Screen { HOME, SEARCH, PLAYER, SETTINGS, NAV } screen = HOME;
  Screen navReturn = HOME, searchBack = HOME; bool searchMusic = false;
  int zone = 0;  // 0 grade, 1 categorias
  float scroll = 0; static float selAnim[600] = {};
  bool railFocus = false; int railSel = 0; float railOpen = 0;
  bool phonePanel = false;
  int settingsRow = 0; bool loginPanel = false, logoutPanel = false;
  K loginError = K::None;
  Color ambient{0.15f, 0.15f, 0.2f, 1}, ambientWant = ambient;
  std::string query; std::vector<std::string> recent;
  int keyRow = 0, keyCol = 0; bool keyInRecent = false; int recentSel = 0;
  uint64_t overlayUntil = 0; Screen backTo = HOME; std::string toast; uint64_t toastUntil = 0;
  Video playing; bool playerFromRemote = false;
  // like da conta e o "a seguir" do vídeo aberto (/api/video/<id>/next); nextAt: contagem do próximo automático
  std::string likeState; Video upNext; bool ratePanel = false; int rateSel = 0; uint64_t nextAt = 0; bool endedSent = false;
  bool livePaused = false;
  int& quality = settings.quality;
  bool& nestOut = settings.nest; int& nestOffsetMs = settings.syncMs;
  auto saveSettings = [&]() {
    if (!settings.save(assets)) { toast = tr(K::SaveFailed); toastUntil = SDL_GetTicks() + 4000; }
  };
  std::string nestName = tr(K::Nest); bool nestAvailable = false, nestChecked = false;
  bool nestSkip = false;  // o Nest falhou neste vídeo: segue com o som da TV até trocar de vídeo ou apertar X
  std::string castCode, castWho; uint64_t castCodeAt = 0;  // castWho: quem está conectado pelo Transmitir  // código de "Assistir na TV" do receptor (cast/receptor.mjs)
  if (nestOut) control.add({5, "/api/nest/prepare", 0});
  auto requestAccount = [&](const char* path) { control.add({5, path, 0}); accountBusy = true; };
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
  auto startVideo = [&](const Video& v, double t, bool fromRemote) {
    if (!fromRemote && nav.ownsQueue) {
      nav.ownsQueue = nav.musicSession = nav.musicNow = false;
      navWorker.add({6, "/api/nav/transport?action=clear", ++nav.serial}, true);
    }
    if (v.id != playing.id) {
      nestSkip = false; likeState.clear(); upNext = Video{};
      control.add({5, "/api/video/" + v.id + "/next", 0});
    }
    nextAt = 0; endedSent = false; ratePanel = false; livePaused = false;
    playing = v; playerFromRemote = fromRemote;
    videoTex.ready = false;
    phonePanel = loginPanel = logoutPanel = false;
    pl.nest = nestOut && (!nestChecked || nestAvailable) && !nestSkip; pl.nestOffset = nestOffsetMs / 1000.0;
    pl.start(srv, v.id, t, v.title, v.dur, quality);
    if (screen != PLAYER) backTo = screen;
    screen = PLAYER; overlayUntil = SDL_GetTicks() + 5000;
  };
  auto stopVideo = [&]() {
    if (nav.ownsQueue && pl.state != player::ENDED) {
      nav.ownsQueue = nav.musicSession = nav.musicNow = false; nav.busy = false; nav.cancelQueue = true;
      nav.action(navWorker, "/api/nav/transport?action=stop");
    }
    pl.halt(); ratePanel = false; nextAt = 0; screen = backTo == PLAYER ? HOME : backTo; };
  // Live pausada não continua de onde parou: o ffmpeg do servidor para junto, e a janela de algumas lives no YouTube
  // tem só 30 s. Continuar abre de novo perto do ao vivo.
  auto resumeLive = [&]() { livePaused = false; startVideo(playing, 0, playerFromRemote); };
  auto backspace = [&]() {
    if (query.empty()) return;
    size_t at = query.size() - 1;
    while (at > 0 && ((unsigned char)query[at] & 0xC0) == 0x80) at--;
    query.resize(at);
  };
  auto openNav = [&](const std::string& kind, const std::string& id = "", const std::string& q = "", bool music = false) {
    if (screen != NAV) { navReturn = screen == SEARCH ? searchBack : screen; nav.pages.clear(); }
    nav.open(navWorker, settings.lang, kind, id, q, music); screen = NAV; railFocus = false;
  };
  auto openChannelForVideo = [&](const std::string& id) {
    if (screen != NAV) { navReturn = screen; nav.pages.clear(); }
    nav.channelFromVideo(navWorker, settings.lang, id); screen = NAV; railFocus = false;
  };
  auto navBack = [&]() {
    if (!nav.pages.empty()) nav.pages.pop_back();
    if (nav.pages.empty()) { screen = navReturn; if (screen == SEARCH || screen == PLAYER) screen = HOME; }
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
              : k == 'B' ? Input::B : k == 'X' ? Input::X : k == 'Y' ? Input::Y : k == 'S' ? Input::START : k == 'l' ? Input::LT : k == 'r' ? Input::RT : -1;
        if (b >= 0) in.fired[b] = true;
      }
    }

    // ---------------------------------------------------------------- resultados do trabalho em segundo plano
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
    if (!sleepPending && now >= sleepAt && !shutdownAt) requestSleep("/api/sleep");
    double sleepLeft = sleep.remaining(now, sleep.end && pl.paused);
    bool sleepWarning = sleep.active && sleepLeft >= 0 && sleepLeft <= 60 && !shutdownAt;
    double gain = sleepWarning ? std::clamp(sleepLeft / 60, 0.0, 1.0) : 1.0;
    pl.sleepGain((float)gain);
    while (control.pop(r)) {
      if (r.key == "/api/cast/code") {
        auto f = net::split(r.feed.title.substr(0, r.feed.title.find('\n')), '\t');
        castCode = f.size() > 1 && f[0] == "ok" ? f[1] : "";
        castWho = f.size() > 2 && f[0] == "ok" ? f[2] : "";
      } else if (r.key == "/api/nest/info") {
        auto f = net::split(r.feed.title.substr(0, r.feed.title.find('\n')), '\t');
        nestChecked = true; nestAvailable = f.size() > 1 && f[0] == "ok" && !f[1].empty();
        if (nestAvailable) nestName = f[1];
      } else if (r.key.rfind("/api/video/", 0) == 0) {
        auto f = net::split(r.feed.title.substr(0, r.feed.title.find('\n')), '\t');
        bool ok = r.feed.error == K::None && !f.empty() && f[0] == "ok";
        std::string id = r.key.substr(11, 11);
        if (r.key.find("/rate?") != std::string::npos) {
          if (ok && f.size() > 1 && id == playing.id) likeState = f[1];
          toast = tr(!ok ? K::RateFailed : likeState == "LIKE" ? K::Liked : likeState == "DISLIKE" ? K::Disliked : K::RatingRemoved);
          toastUntil = now + 3000;
        } else if (ok && id == playing.id && f.size() >= 5) {
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
            loginPanel = false; loginError = K::None;
            if (changed) { toast = tr(K::SignedInAs) + account.name; toastUntil = now + 4000; }
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
    while (navWorker.pop(r)) {
      if (r.stamp == nav.actionStamp && nav.busy) {
        nav.busy = false;
        bool ok = r.feed.error == K::None && r.feed.title.rfind("ok", 0) == 0;
        if (r.key.rfind("/api/nav/play?", 0) == 0 && ok) {
          NavPage queue; Navigation::parse(r.feed.title, queue);
          if (nav.cancelQueue) { nav.musicSession = nav.musicNow = false; nav.action(navWorker, "/api/nav/transport?action=stop"); }
          else { nav.queue = std::move(queue.items); nav.ownsQueue = true; }
          nav.nowSel = 0; nav.lyrics.clear(); nav.lyricsId.clear(); nav.lyricLines.clear(); nav.lyricScroll = 0;
        } else if (r.key.rfind("/api/nav/lyrics?", 0) == 0) {
          auto fields = net::split(r.feed.title.substr(0, r.feed.title.find('\n')), '\t');
          nav.lyrics = fields.size() > 1 ? fields[1] : "";
          nav.lyricLines = nav.lyrics.empty() ? std::vector<std::string>{} : gfx::wrap(g, ui::F_KEY, nav.lyrics, 714, 200); nav.lyricScroll = 0;
        } else if (r.key.find("action=subscribe") != std::string::npos && ok && !nav.pages.empty()) {
          for (auto& p : nav.pages) if (r.key.find("id=" + p.channelId + "&") != std::string::npos && !p.channelId.empty()) p.subscribed = r.key.find("value=1") != std::string::npos;
        } else if (r.key.find("action=later") != std::string::npos && ok) { toast = tr(K::AddedLater); toastUntil = now + 3500; nav.menu = false; }
        if (r.key == "/api/nav/transport?action=stop") { nav.cancelQueue = false; nav.ownsQueue = false; }
        if (!ok) {
          if (r.key.rfind("/api/nav/play?", 0) == 0) nav.musicSession = nav.musicNow = false;
          auto fields = net::split(r.feed.title.substr(0, r.feed.title.find('\n')), '\t');
          toast = fields.size() > 1 ? fields[1] : tr(K::ActionFailed); toastUntil = now + 4500;
        }
      } else nav.arrived(r, g, settings.lang);
    }
    while (worker.pop(r)) {
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
        r.feed.more = r.feed.items.size() >= 24;
        target->feed = std::move(r.feed);
      }
    }
    thumbs.upload(g, frame, 2);

    // ---------------------------------------------------------------- comandos do celular
    SDL_LockMutex(remote.mu);
    std::deque<std::vector<std::string>> cmds; cmds.swap(remote.cmds);
    bool remoteOnline = remote.online;
    SDL_UnlockMutex(remote.mu);
    for (auto& c : cmds) {
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
      if (c[0] == "play" && nav.cancelQueue) continue;
      if (c[0] == "play" && c.size() >= 2) {
        Video v; v.id = c[1]; double t = c.size() > 2 ? std::atof(c[2].c_str()) : 0; v.title = c.size() > 3 ? c[3] : "";
        for (auto& c : cats) for (auto& it : c.feed.items) if (it.id == v.id) v = it;
        for (auto& it : results.feed.items) if (it.id == v.id) v = it;
        startVideo(v, t, true); toast = tr(K::PhoneRequest); toastUntil = now + 2500;
      } else if (screen == PLAYER) {
        if (c[0] == "pause") pl.setPaused(true);
        else if (c[0] == "resume") { if (pl.live && pl.paused) resumeLive(); else pl.setPaused(false); }
        else if (c[0] == "stop") stopVideo();
        else if (c[0] == "seek" && c.size() >= 2 && !pl.live) startVideo(playing, std::max(0.0, pl.position() + std::atof(c[1].c_str())), playerFromRemote);
        overlayUntil = now + 4000;
      }
    }

    if (screen == PLAYER && nav.musicSession) {
      nav.musicNow = true;
      for (const auto& item : nav.queue) if (item.id == playing.id) { playing.title = item.title; playing.channel = item.channel; break; }
    }
    // ---------------------------------------------------------------- lógica por tela
    // código de pareamento e quem está conectado pelo Transmitir (o código troca a cada 5 min)
    if (now >= castCodeAt) { control.add({5, "/api/cast/code", 0}); castCodeAt = now + (phonePanel ? 15000 : 30000); }
    if (now >= nestInfoAt) { control.add({5, "/api/nest/info", 0}); nestInfoAt = now + 60000; }
    if (!accountBusy && now >= accountAt) requestAccount("/api/account");
    const char* keys[] = {tr(K::Keyboard1), tr(K::Keyboard2), tr(K::Keyboard3), tr(K::Keyboard4)};
    if (shutdownAt) {
      if (now >= shutdownAt) {
        if (powerDownXbox()) quit = true;
        else { shutdownAt = 0; toast = tr(K::SleepError); toastUntil = now + 6000; }
      }
    } else if (sleepWarning) {
      if (in.hit(Input::A) && !sleepPending) requestSleep("/api/sleep/add?min=15");
      if (in.hit(Input::B) && !sleepPending) requestSleep("/api/sleep/cancel");
    } else if (sleepPanel) {
      if (in.hit(Input::LEFT)) sleepChoice = (sleepChoice + 7) % 8;
      if (in.hit(Input::RIGHT)) sleepChoice = (sleepChoice + 1) % 8;
      if (in.hit(Input::A) && !sleepPending) { applySleep(); sleepPanel = false; }
      if (in.hit(Input::B)) sleepPanel = false;
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
    } else if (screen == NAV && !nav.pages.empty()) {
      auto& p = nav.page(); uint64_t currentPage = p.stamp;
      bool channel = p.kind == "channel";
      bool playlist = p.kind == "playlist" || p.kind == "music_playlist" || p.kind == "album" || p.kind == "radio" || p.kind == "later";
      bool tabs = channel || p.kind == "music" || p.kind == "search" || p.kind == "music_search";
      int count = (int)p.items.size();
      if (count) p.sel = std::clamp(p.sel, 0, count - 1);
      bool left = in.hit(Input::LEFT), right = in.hit(Input::RIGHT), up = in.hit(Input::UP), down = in.hit(Input::DOWN), a = in.hit(Input::A);
      if (p.zone == 0) {
        if (down) p.zone = tabs ? 1 : !p.channels.empty() ? 2 : 3;
        if (a && !p.loading && !nav.busy) {
          if (channel) {
            if (!account.signedIn()) { toast = tr(K::AccountRequired); toastUntil = now + 4000; }
            else if (p.subscribed) nav.unsubscribe = true;
            else nav.action(navWorker, "/api/nav/action?action=subscribe&id=" + p.channelId + "&value=1&lang=" + i18n::code(settings.lang));
          } else if (playlist) {
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
        if (left && p.sel % 4 > 0) p.sel--;
        if (right && p.sel % 4 < 3 && p.sel + 1 < count) p.sel++;
        if (up) { if (p.sel >= 4) p.sel -= 4; else p.zone = !p.channels.empty() ? 2 : tabs ? 1 : channel || playlist ? 0 : 3; }
        if (down) { if (p.sel + 4 < count) p.sel += 4; else if (count) p.sel = count - 1; }
        if (a && count) {
          const auto item = p.items[(size_t)p.sel];
          if (item.type == "video") {
            if (p.music) { openNav("radio", item.id, "", true); nav.page().zone = 0; }
            else startVideo(item.video(), 0, false);
          } else if (item.type == "mixes") openNav("music", "", "mixes", true);
          else if (item.type == "history" || item.type == "later") openNav(item.type, "", "", p.music);
          else openNav(item.type == "playlist" && p.music ? "music_playlist" : item.type, item.id, "", p.music);
        }
        if (screen == NAV && nav.page().stamp == currentPage && in.hit(Input::START) && count && p.items[(size_t)p.sel].type == "video") { nav.menuItem = p.items[(size_t)p.sel]; nav.menuSel = 0; nav.menu = true; }
        if (screen == NAV && nav.page().stamp == currentPage && count && p.sel >= count - 8 && p.more && !p.loading) nav.load(navWorker, settings.lang, false, true);
      }
      if (screen == NAV && nav.page().stamp == currentPage && in.hit(Input::X)) { nav.page().sel = 0; nav.page().scroll = 0; nav.load(navWorker, settings.lang, true); }
      if (in.hit(Input::Y)) openSearch();
      if (in.hit(Input::B)) {
        if (nav.busy && nav.actionPath.rfind("/api/nav/play?", 0) == 0) nav.cancelQueue = true;
        navBack();
      }
    } else if (screen == SETTINGS) {
      if (in.hit(Input::UP) && settingsRow > 0) settingsRow--;
      if (in.hit(Input::DOWN) && settingsRow < 7) settingsRow++;
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
          const int values[] = {720, 480, 360}; int i = quality == 720 ? 0 : quality == 480 ? 1 : 2;
          quality = values[(i + direction + 3) % 3];
        } else if (settingsRow == 3) {
          if (nestAvailable) { nestOut = !nestOut; if (nestOut) control.add({5, "/api/nest/prepare", 0}); }
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
        if (changed) saveSettings();
      }
      if (in.hit(Input::B)) { screen = HOME; railFocus = true; railSel = 6; }
    } else if (screen == HOME && railFocus) {
      if (in.hit(Input::UP) && railSel > 0) railSel--;
      if (in.hit(Input::DOWN) && railSel < 7) railSel++;
      if (in.hit(Input::RIGHT) || in.hit(Input::B)) railFocus = false;
      if (in.hit(Input::A)) {
        if (railSel == 0) goHome();
        else if (railSel == 1) openSearch();
        else if (railSel == 2) openNav("subscriptions");
        else if (railSel == 3) openNav("library");
        else if (railSel == 4) openNav("music", "", "", true);
        else if (railSel == 5) { phonePanel = true; castCodeAt = 0; }
        else if (railSel == 6) { screen = SETTINGS; railFocus = false; nestInfoAt = 0; accountAt = 0; }
        else quit = true;
      }
    } else if (screen == HOME) {
      Shelf& cur = inResults ? results : cats[(size_t)cat];
      if (!cur.feed.items.empty()) cur.sel = std::clamp(cur.sel, 0, (int)cur.feed.items.size() - 1);
      int n = (int)cur.feed.items.size();
      if (zone == 1 && !inResults) {
        if (in.hit(Input::LEFT)) { if (cat > 0) { cat--; scroll = 0; } else { railFocus = true; railSel = 0; } }
        if (in.hit(Input::RIGHT) && cat + 1 < (int)cats.size()) { cat++; scroll = 0; }
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
        if (in.hit(Input::LEFT)) { if (keyCol > 0) keyCol--; else { screen = searchBack; railFocus = screen == HOME; railSel = 1; } }
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
      if (in.hit(Input::UP) && nav.lyricsOpen && nav.lyricScroll > 0) nav.lyricScroll--;
      if (in.hit(Input::DOWN) && nav.lyricsOpen && nav.lyricScroll + 9 < (int)nav.lyricLines.size()) nav.lyricScroll++;
      if (in.hit(Input::LEFT) && nav.nowSel > 0) nav.nowSel--;
      if (in.hit(Input::RIGHT) && nav.nowSel < 3) nav.nowSel++;
      if (in.hit(Input::A)) {
        if (nav.nowSel == 0 && !pl.waiting()) pl.setPaused(!pl.paused);
        else if (nav.nowSel == 1 && !nav.busy) nav.action(navWorker, "/api/nav/transport?action=next");
        else if (nav.nowSel == 2) {
          nav.lyricsOpen = !nav.lyricsOpen;
          if (nav.lyricsOpen && nav.lyricsId != playing.id && !nav.busy) { nav.lyricsId = playing.id; nav.lyrics.clear(); nav.lyricLines.clear(); nav.action(navWorker, "/api/nav/lyrics?id=" + playing.id); }
        } else if (nav.nowSel == 3) { nav.musicNow = nav.musicSession = false; }
      }
      if (in.hit(Input::B)) {
        nav.ownsQueue = nav.musicSession = nav.musicNow = false; nav.busy = false; nav.cancelQueue = true;
        nav.action(navWorker, "/api/nav/transport?action=stop"); stopVideo();
      }
    } else if (screen == PLAYER) {
      bool any = false;
      // Carregando, o A não pausa: a pausa ficava invisível atrás do "Carregando" e o vídeo abria parado em 0:00.
      if ((in.hit(Input::A) || in.hit(Input::START)) && !pl.waiting()) {
        if (pl.live && pl.paused) resumeLive(); else pl.setPaused(!pl.paused);
        any = true;
      }
      double jump = 0;
      if (in.hit(Input::LEFT)) jump = -10;
      if (in.hit(Input::RIGHT)) jump = 10;
      if (in.hit(Input::LB)) jump = -60;
      if (in.hit(Input::RB)) jump = 60;
      if (jump != 0 && pl.state != player::FAILED && !pl.live) {
        double to = std::clamp(pl.position() + jump, 0.0, pl.duration > 0 ? std::max(0.0, pl.duration - 3) : 1e9);
        startVideo(playing, to, playerFromRemote); any = true;
      }
      if (in.hit(Input::Y) && pl.state != player::FAILED) {
        quality = quality == 720 ? 480 : quality == 480 ? 360 : 720;
        startVideo(playing, pl.position(), playerFromRemote);
        toast = std::string(tr(K::QualityPrefix)) + tr(i18n::quality(quality)); toastUntil = now + 2500; saveSettings(); any = true;
      }
      if (in.hit(Input::X)) {
        if (!nestAvailable) toast = tr(K::NoSpeaker);
        else {
          if (nestSkip && nestOut) nestSkip = false;
          else nestOut = !nestOut;
          if (!nestOut) control.add({5, "/api/nest/stop", 0});
          startVideo(playing, pl.position(), playerFromRemote);
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
      if (in.hit(Input::UP)) { sleepPanel = true; any = true; }
      if (in.hit(Input::DOWN)) {
        if (account.signedIn()) { ratePanel = true; rateSel = likeState == "DISLIKE" ? 1 : 0; }
        else { toast = tr(K::RateSignIn); toastUntil = now + 3500; }
        any = true;
      }
      if (any) overlayUntil = now + 4000;
      if (in.hit(Input::B) || in.hit(Input::BACK)) stopVideo();
      if (pl.state == player::FAILED && pl.nestFailed && !nestSkip) {
        nestSkip = true;
        toast = tr(K::SpeakerFallback); toastUntil = now + 5000;
        startVideo(playing, pl.position(), playerFromRemote);
      }

    }
    // Live retomada pelo próprio Nest (toque em cima, Google Home) também volta para o ao vivo.
    if (screen == PLAYER && pl.live) { if (pl.paused) livePaused = true; else if (livePaused) resumeLive(); }
    else livePaused = false;
    // Fim natural continua sendo informado mesmo com um cartão aberto.
    if (screen == PLAYER && pl.state == player::ENDED && !shutdownAt) {
      if (!endedSent) {
        SDL_LockMutex(remote.mu); remote.ended = playing.id; SDL_UnlockMutex(remote.mu);
        endedSent = true; ratePanel = false;
        // Pedido pelo celular: o app do celular escolhe o próximo. Timer "no fim do vídeo": desliga, não emenda.
        if (!playerFromRemote && !upNext.id.empty() && !(sleep.active && sleep.end)) nextAt = now + 8000;
        else stopVideo();
      } else if (nextAt && now >= nextAt) { Video v = upNext; startVideo(v, 0, false); }
    }
    in.clear();
    Shelf& cur = inResults ? results : cats[(size_t)cat];
    const Video* focusVid = nullptr;
    if (!cur.feed.items.empty()) { cur.sel = std::clamp(cur.sel, 0, (int)cur.feed.items.size() - 1); focusVid = &cur.feed.items[(size_t)cur.sel]; }

    SDL_LockMutex(remote.mu);
    remote.playing = screen == PLAYER ? playing.id : ""; remote.pos = pl.position(); remote.dur = pl.duration; remote.paused = pl.paused;
    SDL_UnlockMutex(remote.mu);

    if (focusVid && screen == HOME) { if (const Thumbs::Slot* s = thumbs.get(thumbsWorker, focusVid->id, frame)) ambientWant = s->avg; }
    ambient = ui::mix(ambient, ambientWant, 0.06f);
    railOpen += ((railFocus ? 1.f : 0.f) - railOpen) * 0.25f;

    // ---------------------------------------------------------------- quadro do vídeo
    gfx::begin(g);
    gfx::rect(g, 0, 0, ui::W, ui::H, ui::BG);
    if (screen == PLAYER) {
      if (player::Frame* f = pl.take()) {
        if (f->w != videoTex.w || f->h != videoTex.h) { if (videoTex.set) gfx::videoDestroy(g, videoTex); gfx::videoCreate(g, videoTex, f->w, f->h); LOG("vídeo %dx%d", f->w, f->h); }
        std::memcpy(gfx::videoStaging(g, videoTex), f->yuv.data(), f->yuv.size()); gfx::videoCommit(g, videoTex);
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
        const Thumbs::Slot* ts = thumbs.get(thumbsWorker, v.id, frame);
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
          gfx::text(g, ui::F_BIG, tr(K::ServerUnavailable), ui::X0, 190, ui::TEXT);
          gfx::text(g, ui::F_BODY, std::string(tr(cur.feed.error)) + " (" + srv.ip + ":" + std::to_string(srv.port) + ")", ui::X0, 240, ui::MUTED);
          gfx::text(g, ui::F_BODY, tr(K::RetryServer), ui::X0, 270, ui::MUTED);
        } else gfx::text(g, ui::F_BODY, tr(K::Empty), ui::X0, 190, ui::MUTED);
      }
      // topo fixo: o nome e as categorias (a grade passa por baixo)
      gfx::rect(g, 0, 0, ui::W, 118, ui::BG, 0);
      gfx::vgrad(g, 0, 118, ui::W, 36, ui::BG, ui::alpha(ui::BG, 0));
      wordmark(g, ui::X0, 22, 1, ui::BG);
      {
        float hw = 24 + 8 + gfx::textWidth(g, ui::F_SMALL, tr(K::Refresh)) + 26 + 24 + 8 + gfx::textWidth(g, ui::F_SMALL, tr(K::Search));
        float lx = ui::W - 64 - hw, pillRight = lx - 18;
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
        if (account.signedIn()) pill(tr(K::AccountPrefix) + account.name, ui::A2);
        float hx3 = hint(g, lx, 32, "X", XX, tr(K::Refresh));
        hint(g, hx3, 32, "Y", XY, tr(K::Search));
        hint(g, 1010, 678, "Menu", XG, tr(K::MoreOptions));
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
      const K labels[] = {K::YouTubeAccount, K::VideosPerRow, K::DefaultQuality, K::Sound, K::SpeakerSync, K::Language, K::SleepTimer, K::SleepTogether};
      for (int i = 0; i < 8; i++) {
        float x = ui::X0, y = 132 + i * 61, w = ui::W - ui::X0 - 64;
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
        else if (i == 2) value = tr(i18n::quality(quality));
        else if (i == 3) {
          value = nestAvailable && nestOut ? nestName : std::string(tr(K::TV));
          if (disabled) gfx::text(g, ui::F_SMALL, tr(nestChecked ? K::NoSpeaker : K::SpeakerChecking), x + 28, y + 29, ui::FAINT);
        } else if (i == 4) value = (nestOffsetMs > 0 ? "+" : "") + std::to_string(nestOffsetMs) + tr(K::Milliseconds);
        else if (i == 5) value = tr(settings.lang == i18n::Lang::PT ? K::Portuguese : K::English);
        else if (i == 6) value = sleepLabel(sleepChoice);
        else value = tr(settings.sleepNotebook ? K::SleepNotebook : K::SleepXbox);
        float right = x + w - 54;
        gfx::text(g, ui::F_KEY, gfx::ellipsize(g, ui::F_KEY, value, i == 0 ? 480 : 320), right, y + 15, disabled ? ui::FAINT : selected ? ui::A2 : ui::TEXT, 2);
        if (selected && i > 0 && !disabled) {
          float ax = right - std::min(340.f, gfx::textWidth(g, ui::F_KEY, value)) - 26;
          gfx::triangle(g, ax, y + 27, ax + 6, y + 22, ax + 6, y + 32, ui::MUTED);
          gfx::triangle(g, x + w - 24, y + 27, x + w - 30, y + 22, x + w - 30, y + 32, ui::MUTED);
        }
      }
      gfx::text(g, ui::F_SMALL, tr(K::SleepTVHelp), ui::X0, 630, ui::MUTED);
      if (settingsRow == 4) gfx::text(g, ui::F_SMALL, tr(K::SyncHelp), ui::X0, 652, ui::FAINT);
      float hx = hint(g, ui::X0, 678, tr(K::BtnVertical), XG, tr(K::ChooseRow));
      hx = hint(g, hx, 678, tr(K::BtnHorizontal), XG, tr(K::ChangeValue));
      if (settingsRow == 0) hx = hint(g, hx, 678, "A", XA, tr(K::Open));
      hint(g, hx, 678, "B", XB, tr(K::Back));
    }

    if (screen == NAV) nav.draw(g, thumbs, thumbsWorker, frame, now, settings.lang);

    if (screen == HOME || screen == SEARCH || screen == SETTINGS || screen == NAV) {
      // barra lateral: ícones; com foco, abre com os nomes
      float rw = ui::RAIL + 200 * railOpen;
      if (railOpen > 0.01f) {
        gfx::rect(g, 0, 0, rw, ui::H, ui::alpha(ui::BG, 0.96f * railOpen), 0);
        gfx::hgrad(g, rw - 1, 0, 200, ui::H, ui::alpha(ui::BG, 0.96f * railOpen), ui::alpha(ui::BG, 0));
      }
      const char* names[] = {tr(K::Home), tr(K::Search), tr(K::Subscriptions), tr(K::Library), tr(K::Music), tr(K::Phone), tr(K::Settings), tr(K::Exit)};
      int page = screen == SETTINGS ? 6 : screen == SEARCH || inResults ? 1 : screen == NAV && !nav.pages.empty() ? nav.page().music ? 4 : nav.pages.front().kind == "subscriptions" ? 2 : nav.pages.front().kind == "library" ? 3 : 0 : 0;
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
      bool show = pl.paused || now < overlayUntil || pl.waiting() || pl.state == player::FAILED;
      if (show) {
        gfx::vgrad(g, 0, 0, ui::W, 200, {0, 0, 0, 0.8f}, {0, 0, 0, 0});
        gfx::vgrad(g, 0, ui::H - 220, ui::W, 220, {0, 0, 0, 0}, {0, 0, 0, 0.88f});
        std::string ttl = pl.title.empty() ? playing.title : pl.title;
        gfx::text(g, ui::F_BIG, gfx::ellipsize(g, ui::F_BIG, ttl, ui::W - 2 * 72), 72, 44, ui::TEXT);
        if (!playing.channel.empty()) gfx::text(g, ui::F_CHAN, playing.channel, 72, 92, ui::MUTED);
        double pos = pl.position(), dur = pl.duration;
        float bx = 72, by = ui::H - 104, bw = ui::W - 144;
        gfx::rect(g, bx, by, bw, 6, {1, 1, 1, 0.22f}, 3);
        bool live = pl.live;
        float frac = live ? 1.f : dur > 0 ? (float)std::clamp(pos / dur, 0.0, 1.0) : 0;
        if (frac > 0) gfx::hgrad(g, bx, by, std::max(6.f, bw * frac), 6, ui::A1, ui::A2, 3);
        gfx::rect(g, bx + bw * frac - 14, by - 11, 28, 28, ui::alpha(ui::A2, 0.35f), 14, 6);
        gfx::rect(g, bx + bw * frac - 8, by - 5, 16, 16, rgb(0xffffff), 8);
        if (live) {
          float lw = gfx::textWidth(g, ui::F_SMALL, tr(K::Live)) + 24;
          gfx::rect(g, bx, by + 22, lw, 28, ui::A1, 6);
          gfx::text(g, ui::F_SMALL, tr(K::Live), bx + lw / 2, by + 26, rgb(0xffffff), 1);
        } else {
          gfx::text(g, ui::F_BODY, fmtTime(pos), bx, by + 22, ui::TEXT);
          if (dur > 0) gfx::text(g, ui::F_BODY, fmtTime(dur), bx + bw, by + 22, ui::MUTED, 2);
        }
        float hx2 = ui::W / 2 - 430;
        hx2 = hint(g, hx2, by + 24, "A", XA, pl.paused ? tr(K::Resume) : tr(K::Pause));
        hx2 = hint(g, hx2, by + 24, "Y", XY, tr(i18n::quality(quality)));
        hx2 = hint(g, hx2, by + 24, "X", XX, (tr(K::SoundPrefix) + (pl.nest ? nestName : std::string(tr(K::TV)))).c_str());
        if (!live) {
          hx2 = hint(g, hx2, by + 24, tr(K::BtnHorizontal), XG, tr(K::TenSeconds));
          hx2 = hint(g, hx2, by + 24, "LB RB", XG, tr(K::OneMinute));
        }
        hint(g, hx2, by + 24, "B", XB, tr(K::Back));
        float ux = hint(g, 72, by - 42, "↑", XG, tr(K::SleepTimer));
        float rx = hint(g, ux, by - 42, "↓", XG, tr(K::Rate));
        if (likeState == "LIKE" || likeState == "DISLIKE")  // a avaliação já dada, como o joinha aceso do YouTube
          glyph(g, likeState == "LIKE" ? icons::THUMB_UP_FILL : icons::THUMB_DOWN_FILL, false, rx - 22, by - 42, ui::TEXT);
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
        if (const Thumbs::Slot* s = thumbs.get(thumbsWorker, upNext.id, frame)) gfx::image(g, s->tex, tx, ty, tw, th, 12);
        float cx = tx + tw + 28, cw = x + w - 32 - cx, cy = ty + 2;
        for (auto& line : gfx::wrap(g, ui::F_H2, upNext.title, cw, 2)) { gfx::text(g, ui::F_H2, line, cx, cy, ui::TEXT); cy += 30; }
        if (!upNext.channel.empty()) gfx::text(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, upNext.channel, cw), cx, cy + 8, ui::MUTED);
        float hx = hint(g, x + 32, y + h - 52, "A", XA, tr(K::WatchNow));
        hint(g, hx, y + h - 52, "B", XB, tr(K::Cancel));
      }
      if (pl.paused && !pl.waiting()) {
        gfx::rect(g, ui::W / 2 - 52, ui::H / 2 - 52, 104, 104, {0, 0, 0, 0.5f}, 52);
        gfx::rect(g, ui::W / 2 - 18, ui::H / 2 - 22, 12, 44, rgb(0xffffff), 3);
        gfx::rect(g, ui::W / 2 + 6, ui::H / 2 - 22, 12, 44, rgb(0xffffff), 3);
      }
      if (pl.waiting()) {
        spinner(g, ui::W / 2, ui::H / 2, 26, t);
        gfx::text(g, ui::F_BODY, pl.state == player::CONNECTING ? tr(K::RequestingVideo) : tr(K::Loading), ui::W / 2, ui::H / 2 + 46, ui::TEXT, 1);
      }
      if (pl.state == player::FAILED) {
        gfx::rect(g, ui::W / 2 - 340, ui::H / 2 - 74, 680, 148, ui::SURFACE, 22);
        gfx::text(g, ui::F_H2, tr(K::VideoFailed), ui::W / 2, ui::H / 2 - 46, ui::TEXT, 1);
        gfx::text(g, ui::F_BODY, tr(i18n::playerError(pl.error)), ui::W / 2, ui::H / 2, ui::MUTED, 1);
      }
    }

    }

    if (sleep.active && screen == PLAYER) {
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
      for (auto& line : gfx::wrap(g, ui::F_BODY, tr(K::CastChoose) + brandName + ".", pw - 96, 2)) {
        gfx::text(g, ui::F_BODY, line, px + 48, ty, ui::TEXT); ty += 25;
      }
      for (auto& line : gfx::wrap(g, ui::F_SMALL, tr(K::CastPath), pw - 96, 2)) {
        gfx::text(g, ui::F_SMALL, line, px + 48, ty + 8, ui::MUTED); ty += 22;
      }
      if (!castCode.empty()) gfx::textGrad(g, ui::F_CODE, castCode, px + 48, py + 186, ui::A1, ui::A2);
      else gfx::text(g, ui::F_BODY, tr(K::CastLoading), px + 48, py + 208, ui::FAINT);
      gfx::text(g, ui::F_SMALL, tr(K::CastRemember), px + 48, py + 264, ui::MUTED);
      gfx::rect(g, px + 48, py + 300, pw - 96, 1, ui::alpha(rgb(0xffffff), 0.08f));
      gfx::text(g, ui::F_SMALL, !castWho.empty() ? tr(K::PhoneConnected) + castWho : std::string(tr(remoteOnline ? K::NoPhone : K::ServerOffline)),
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
      float x = 280, y = 242, w = 720;
      gfx::shadow(g, x, y + 12, w, 226, 24, 32, 0.6f);
      gfx::rect(g, x, y, w, 226, ui::SURFACE, 24);
      std::string title = shutdownAt ? tr(K::SleepShuttingDown) : sleepWarning ? std::string(tr(K::SleepWarning)) +
          std::to_string((int)std::ceil(sleepLeft)) + tr(K::SleepSeconds) : tr(K::SleepTimer);
      gfx::text(g, ui::F_BIG, title, x + 36, y + 32, ui::TEXT);
      if (!shutdownAt) {
        gfx::text(g, ui::F_KEY, sleepWarning ? tr(sleep.notebook ? K::SleepNotebook : K::SleepXbox) : sleepLabel(sleepChoice), x + 36, y + 92, ui::A2);
        float hx = x + 36;
        if (!sleepWarning) hx = hint(g, hx, y + 162, tr(K::BtnHorizontal), XG, tr(K::ChangeValue));
        hx = hint(g, hx, y + 162, "A", XA, tr(sleepWarning ? K::SleepAdd : K::SleepApply));
        hint(g, hx, y + 162, "B", XB, tr(K::Cancel));
      }
    }
    bool shoot = shotPath && (int)frame == shotFrame;
#ifndef __XSDK__
    if (shotPath && std::getenv("XT_SHOT_MS")) shoot = now - runStart >= (uint64_t)std::atoi(std::getenv("XT_SHOT_MS"));
#endif
    gfx::end(g, shoot ? shotPath : nullptr);
    if (shoot) {
      LOG("captura: frame %llu, tempo %llu ms, tela %d, vídeo %s, posição %.2f", (unsigned long long)frame, (unsigned long long)(now - runStart), (int)screen, playing.id.c_str(), pl.position());
      if (screen == NAV && !nav.pages.empty()) LOG("captura navegação: %s sel=%d canal=%d zona=%d pilha=%u", nav.page().kind.c_str(), nav.page().sel, nav.page().channelSel, nav.page().zone, (unsigned)nav.pages.size());
      quit = true;
    }
    frame++;
    if (frame % 600 == 0 && screen == PLAYER) LOG("player: %s pos %.1f quadros %lld perdidos %lld", playing.id.c_str(), pl.position(), (long long)pl.shownFrames, (long long)pl.droppedFrames);
  }
  pl.halt();
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
  LOG("miniaturas: %u bytes GPU, pico de RGBA aguardando upload %u bytes", (unsigned)(thumbs.slots.size() * thumbs.slotBytes), (unsigned)thumbs.peakPending);
  LOG("%s saindo", brandName.c_str());
  SDL_Quit();
  return 0;
}
