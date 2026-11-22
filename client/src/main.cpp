// Tube360: YouTube nativo no Xbox 360 (BadUpdate), servido pelo notebook (server/, docs/PROTOCOLO.md).
// Um código só: no Fedora (janela do SDL, teclado ou controle) e no 360 (vulkan4xenon + SDL-xenon).
// Interface de TV: barra lateral de ícones, destaque grande do vídeo em foco com a cor dele no ambiente e prateleiras
// horizontais por tema.
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

#ifdef __XSDK__
#include "xsdk/net.h"
// XAM começa com recepção de 16 KB por socket; 63 KB segura o fluxo de vídeo com folga (xsdk/net.h).
extern "C" const XsdkNetworkLimits xsdk_network_limits = {32, 8, 63, 16};
#endif

using gfx::Color;
using gfx::rgb;

// ---------------------------------------------------------------- tema
namespace ui {
const Color BG = rgb(0x0b0b0e), SURFACE = rgb(0x18181d), SURFACE2 = rgb(0x24242b), TEXT = rgb(0xf5f5f7),
            MUTED = rgb(0xa1a1aa), FAINT = rgb(0x6b6b74), A1 = rgb(0xff0f3c), A2 = rgb(0xff5a52), INK = rgb(0x0b0b0e);
enum { F_BODY, F_SMALL, F_CARD, F_H2, F_LOGO, F_BIG, F_KEY, F_TINY, F_HERO, F_CHAN, F_LOGOL };
constexpr float W = 1280, H = 720, RAIL = 96, X0 = 136;
inline Color alpha(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }
inline Color mix(Color a, Color b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t}; }
}  // namespace ui

struct Video { std::string id, channel, views, title; int dur = -1; };
struct Feed { std::string title; std::vector<Video> items; bool loading = false; std::string error; bool more = true, loadingMore = false; };
struct Shelf { std::string title, query; Feed feed; int sel = 0; float scroll = 0; };

static std::string fmtTime(double s) {
  if (s < 0) s = 0;
  int t = (int)s, h = t / 3600, m = (t / 60) % 60, sec = t % 60;
  char b[32];
  if (h) std::snprintf(b, sizeof(b), "%d:%02d:%02d", h, m, sec); else std::snprintf(b, sizeof(b), "%d:%02d", m, sec);
  return b;
}

// ---------------------------------------------------------------- trabalho em segundo plano
// kind: 0 feed home, 1 busca do usuário, 2 miniatura 320x180, 3 busca de prateleira, 4 imagem do destaque 640x360
struct Job { int kind; std::string a; uint64_t stamp; bool fresh = false; int offset = 0; };  // offset: próxima página da grade  // fresh: "Atualizar" (o servidor sorteia de novo)
struct Result { int kind = -1; std::string key; Feed feed; std::vector<uint8_t> rgba; int w = 0, h = 0; uint64_t stamp = 0; int offset = 0; };

static void parseFeed(const std::vector<uint8_t>& raw, Feed& feed) {
  std::string body(raw.begin(), raw.end());
  auto lines = net::split(body, '\n');
  auto head = lines.empty() ? std::vector<std::string>{} : net::split(lines[0], '\t');
  if (head.empty() || head[0] != "ok") { feed.error = head.size() > 1 ? head[1] : "Resposta estranha do servidor"; return; }
  feed.title = head.size() > 2 ? head[2] : "";
  for (size_t i = 1; i < lines.size(); i++) {
    auto f = net::split(lines[i], '\t');
    if (f.size() < 5 || f[0].empty()) continue;
    Video v; v.id = f[0]; v.dur = std::atoi(f[1].c_str()); v.channel = f[2]; v.views = f[3]; v.title = f[4];
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
      // imagem do destaque passa na frente de tudo: é o que a pessoa está olhando
      auto pick = jobs.begin();
      for (auto it = jobs.begin(); it != jobs.end(); ++it) if (it->kind == 4) { pick = it; break; }
      Job j = *pick; jobs.erase(pick);
      SDL_UnlockMutex(mu);
      Result r; r.kind = j.kind; r.key = j.a; r.stamp = j.stamp; r.offset = j.offset;
      net::Response resp;
      if (j.kind == 5) {
        net::get(srv, j.a, resp, 8000, 4096);
        r.feed.title.assign(resp.body.begin(), resp.body.end());
      } else if (j.kind == 0 || j.kind == 1 || j.kind == 3) {
        std::string path = j.kind == 0 ? "/api/feed?kind=home&n=24" : "/api/feed?kind=search&n=24&q=" + net::urlEncode(j.a);
        if (j.fresh) path += "&fresh=1";
        if (j.offset) path += "&offset=" + std::to_string(j.offset);
        if (!net::get(srv, path, resp, 30000)) r.feed.error = "Sem resposta do notebook (" + srv.ip + ":" + std::to_string(srv.port) + ")";
        else parseFeed(resp.body, r.feed);
      } else {
        std::string size = j.kind == 4 ? "?w=640&h=360" : "?w=320&h=180";
        if (net::get(srv, "/api/thumb/" + j.a + size, resp, 10000)) {
          int w, h, n;
          unsigned char* px = stbi_load_from_memory(resp.body.data(), (int)resp.body.size(), &w, &h, &n, 4);
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
  std::vector<Slot> slots; std::map<std::string, int> byId; std::map<std::string, Result> waiting; int maxSlots = 64;
  const Slot* get(Worker& w, const std::string& id, uint64_t frame) {
    auto it = byId.find(id);
    if (it != byId.end()) { Slot& s = slots[(size_t)it->second]; s.used = frame; return &s; }
    if (!waiting.count(id)) { w.add({2, id, 0}); waiting[id] = Result{}; }
    return nullptr;
  }
  void arrived(Result&& r) { waiting[r.key] = std::move(r); }
  void upload(gfx::Gfx& g, uint64_t frame, int budget) {
    for (auto it = waiting.begin(); it != waiting.end() && budget > 0;) {
      Result& r = it->second;
      if (r.kind != 2) { ++it; continue; }
      if (r.rgba.empty() || r.w != 320 || r.h != 180) { it = waiting.erase(it); continue; }
      int idx;
      if ((int)slots.size() < maxSlots) { slots.push_back({}); idx = (int)slots.size() - 1; slots[(size_t)idx].tex = gfx::makeImage(g, 320, 180, VK_FORMAT_R8G8B8A8_UNORM, true); }
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

int main(int argc, char** argv) {
  (void)argc; (void)argv;
#ifdef __XSDK__
  if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) { LOG("SDL: %s", SDL_GetError()); return 1; }
  std::string assets = "";
#else
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) { LOG("SDL: %s", SDL_GetError()); return 1; }
  std::string assets = std::getenv("XT_ASSETS") ? std::getenv("XT_ASSETS") : "assets/";
#endif
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
                                  {"inter-700.ttf", 42}, {"inter-600.ttf", 18}, {"inter-300.ttf", 27}})) { LOG("fontes não carregaram"); return 1; }

  gfx::Video videoTex; g.video = &videoTex;
  player::Player pl; pl.init();
  Worker worker; worker.srv = srv;
  SDL_Thread* wt = SDL_CreateThread(Worker::main, "trabalho", &worker);
  // miniaturas numa fila própria, com 3 threads: não esperam atrás das buscas das categorias
  Worker thumbsWorker; thumbsWorker.srv = srv; thumbsWorker.hw = 5;
  SDL_Thread* tw[3];
  for (auto& th : tw) th = SDL_CreateThread(Worker::main, "miniaturas", &thumbsWorker);
  Remote remote; remote.srv = srv;
  SDL_Thread* rt = SDL_CreateThread(Remote::main, "controle", &remote);
  Thumbs thumbs;
  // categorias (fileira de chips): "Tudo" é o feed da casa; as outras são buscas por tema
  std::vector<Shelf> cats = {{"Tudo", "", {}}, {"Música", "músicas brasileiras mais tocadas", {}}, {"Futebol", "melhores momentos futebol brasileiro", {}},
                             {"Games", "gameplay pt br", {}}, {"Podcasts", "cortes podcast", {}}, {"Notícias", "notícias de hoje brasil", {}}};
  Shelf results; bool inResults = false; int cat = 0;
  for (auto& c : cats) c.feed.loading = true;
  worker.add({5, "/api/nest/info", 0});
  worker.add({0, "", 0});
  for (size_t i = 1; i < cats.size(); i++) worker.add({3, cats[i].query, 0});

  enum Screen { HOME, SEARCH, PLAYER } screen = HOME;
  int zone = 0;  // 0 grade, 1 categorias
  float scroll = 0; static float selAnim[600] = {};
  bool railFocus = false; int railSel = 0; float railOpen = 0;
  bool phonePanel = false;
  Color ambient{0.15f, 0.15f, 0.2f, 1}, ambientWant = ambient;
  std::string query; std::vector<std::string> recent;
  int keyRow = 0, keyCol = 0; bool keyInRecent = false; int recentSel = 0;
  const char* keys[4] = {"abcdefghij", "klmnopqrst", "uvwxyzç-'.", "1234567890"};
  uint64_t stamp = 1, overlayUntil = 0; Screen backTo = HOME; std::string toast; uint64_t toastUntil = 0;
  Video playing; bool playerFromRemote = false;
  // 720p é o padrão (o Pedro quer HD); Y alterna para 480p
  int quality = 720;
  // Saída do som: TV (padrão) ou o Nest pelo servidor; audio.txt guarda "nest <ajuste em ms>" ou "tv <ajuste em ms>"
  bool nestOut = false; int nestOffsetMs = 0;
  if (FILE* f = std::fopen((assets + "audio.txt").c_str(), "r")) {
    char modo[16] = {0}; int ms = 0;
    if (std::fscanf(f, "%15s %d", modo, &ms) >= 1) { nestOut = !std::strcmp(modo, "nest"); nestOffsetMs = std::clamp(ms, -2000, 2000); }
    std::fclose(f);
  }
  auto saveAudio = [&]() {
    if (FILE* f = std::fopen((assets + "audio.txt").c_str(), "w")) { std::fprintf(f, "%s %d\n", nestOut ? "nest" : "tv", nestOffsetMs); std::fclose(f); }
  };
  std::string nestName = "Nest";
  bool nestSkip = false;  // o Nest falhou neste vídeo: segue com o som da TV até trocar de vídeo ou apertar X
  std::string castCode, castWho; uint64_t castCodeAt = 0;  // castWho: quem está conectado pelo Transmitir  // código de "Assistir na TV" do receptor (cast/receptor.mjs)
  if (nestOut) worker.add({5, "/api/nest/prepare", 0}, true);  // o Nest sai do Bluetooth antes do primeiro vídeo
  if (FILE* f = std::fopen((assets + "quality.txt").c_str(), "r")) { int q = 0; if (std::fscanf(f, "%d", &q) == 1 && (q == 360 || q == 480 || q == 720)) quality = q; std::fclose(f); }

  Input in; SDL_Gamepad* pad = nullptr;
  bool quit = false; uint64_t frame = 0; int shotFrame = std::getenv("XT_SHOT_FRAME") ? std::atoi(std::getenv("XT_SHOT_FRAME")) : 90;
#ifndef __XSDK__
  if (const char* s = std::getenv("XT_SCREEN")) {
    if (!std::strcmp(s, "search")) { screen = SEARCH; query = "flamengo 0x3 palmeiras"; recent = {"palmeiras 4x1 corinthians", "reações dos corintianos", "flamengo 0x3 palmeiras"}; }
    if (!std::strcmp(s, "rail")) { railFocus = true; }
    if (!std::strcmp(s, "chips")) { zone = 1; }
  }
  if (g.win) SDL_StartTextInput(g.win);
#endif
  auto startVideo = [&](const Video& v, double t, bool fromRemote) {
    if (v.id != playing.id) nestSkip = false;
    playing = v; playerFromRemote = fromRemote;
    videoTex.ready = false;
    pl.nest = nestOut && !nestSkip; pl.nestOffset = nestOffsetMs / 1000.0;
    pl.start(srv, v.id, t, v.title, v.dur, quality);
    if (screen != PLAYER) backTo = screen;
    screen = PLAYER; overlayUntil = SDL_GetTicks() + 5000;
  };
  auto stopVideo = [&]() { pl.halt(); screen = backTo == PLAYER ? HOME : backTo; };
  auto backspace = [&]() {
    if (query.empty()) return;
    do { query.pop_back(); } while (!query.empty() && ((unsigned char)query.back() & 0xC0) == 0x80);
    if (!query.empty() && (unsigned char)query.back() >= 0xC0) query.pop_back();
  };
  auto doSearch = [&](const std::string& q) {
    if (q.empty()) return;
    query = q; recent.erase(std::remove(recent.begin(), recent.end(), q), recent.end()); recent.insert(recent.begin(), q);
    if (recent.size() > 6) recent.pop_back();
    results = Shelf{"Resultados para \xE2\x80\x9C" + q + "\xE2\x80\x9D", q, {}};
    results.feed.loading = true;
    worker.add({1, q, ++stamp}, true);
    inResults = true; zone = 0; scroll = 0; railFocus = false; screen = HOME;
  };
  auto goHome = [&]() { inResults = false; zone = 0; scroll = 0; railFocus = false; screen = HOME; };
  if (const char* pv = std::getenv("XT_PLAY")) { Video v; v.id = pv; startVideo(v, 0, false); }
#ifndef __XSDK__
  if (const char* q = std::getenv("XT_QUERY")) doSearch(q);
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
      if (script && step >= 0 && step % 24 == 0 && step / 24 < (int)std::strlen(script)) {
        char k = script[step / 24];
        int b = k == 'U' ? Input::UP : k == 'D' ? Input::DOWN : k == 'L' ? Input::LEFT : k == 'R' ? Input::RIGHT : k == 'A' ? Input::A
              : k == 'B' ? Input::B : k == 'X' ? Input::X : k == 'Y' ? Input::Y : -1;
        if (b >= 0) in.fired[b] = true;
      }
    }

    // ---------------------------------------------------------------- resultados do trabalho em segundo plano
    Result r;
    while (thumbsWorker.pop(r)) if (r.kind == 2) thumbs.arrived(std::move(r));
    while (worker.pop(r)) {
      if (r.kind == 5) {
        if (r.key == "/api/cast/code") {
          auto f = net::split(r.feed.title, '\t');
          castCode = f.size() > 1 && f[0] == "ok" ? f[1] : "";
          if (f.size() > 2) { castWho = f[2]; while (!castWho.empty() && castWho.back() == '\n') castWho.pop_back(); }
          while (!castCode.empty() && castCode.back() == '\n') castCode.pop_back();
        }
        if (r.key == "/api/nest/info") { auto f = net::split(r.feed.title, '\t'); if (f.size() > 1 && f[0] == "ok") { nestName = f[1]; while (!nestName.empty() && nestName.back() == '\n') nestName.pop_back(); } }
      } else if (r.kind == 0 || r.kind == 3 || (r.kind == 1 && r.stamp == stamp && inResults)) {
        Shelf* alvo = r.kind == 1 ? &results : nullptr;
        if (r.kind == 0) alvo = &cats[0];
        if (r.kind == 3) for (auto& c : cats) if (c.query == r.key) alvo = &c;
        if (alvo) {
          if (r.offset > 0) {  // página seguinte: anexa (se ainda for a continuação da lista que está na tela)
            alvo->feed.loadingMore = false;
            if (r.offset == (int)alvo->feed.items.size() && r.feed.error.empty()) {
              alvo->feed.items.insert(alvo->feed.items.end(), r.feed.items.begin(), r.feed.items.end());
              alvo->feed.more = !r.feed.items.empty();
            }
          } else {
            bool keepSel = r.kind != 1;
            r.feed.more = r.feed.items.size() >= 24;
            alvo->feed = std::move(r.feed);
            if (!keepSel) alvo->sel = 0;
          }
        }
      }
      else if (r.kind == 2) thumbs.arrived(std::move(r));
    }
    thumbs.upload(g, frame, 2);

    // ---------------------------------------------------------------- comandos do celular
    SDL_LockMutex(remote.mu);
    std::deque<std::vector<std::string>> cmds; cmds.swap(remote.cmds);
    bool remoteOnline = remote.online;
    SDL_UnlockMutex(remote.mu);
    for (auto& c : cmds) {
      if (c[0] == "aviso" && c.size() >= 2) {
        toast = c[1]; toastUntil = now + 5000;
        const std::string pre = "Celular conectado: ";
        if (c[1].rfind(pre, 0) == 0) castWho = c[1].substr(pre.size());
        else if (c[1].find("desconectou") != std::string::npos) castWho.clear();
        continue;
      }
      if (c[0] == "play" && c.size() >= 2) {
        Video v; v.id = c[1]; double t = c.size() > 2 ? std::atof(c[2].c_str()) : 0; v.title = c.size() > 3 ? c[3] : "";
        for (auto& c : cats) for (auto& it : c.feed.items) if (it.id == v.id) v = it;
        for (auto& it : results.feed.items) if (it.id == v.id) v = it;
        startVideo(v, t, true); toast = "Pedido do celular"; toastUntil = now + 2500;
      } else if (screen == PLAYER) {
        if (c[0] == "pause") pl.setPaused(true);
        else if (c[0] == "resume") pl.setPaused(false);
        else if (c[0] == "stop") stopVideo();
        else if (c[0] == "seek" && c.size() >= 2) startVideo(playing, std::max(0.0, pl.position() + std::atof(c[1].c_str())), playerFromRemote);
        overlayUntil = now + 4000;
      }
    }

    // ---------------------------------------------------------------- lógica por tela
    Shelf& cur = inResults ? results : cats[(size_t)cat];
    const Video* focusVid = nullptr;
    if (!cur.feed.items.empty()) { cur.sel = std::clamp(cur.sel, 0, (int)cur.feed.items.size() - 1); focusVid = &cur.feed.items[(size_t)cur.sel]; }

    // código de pareamento e quem está conectado pelo Transmitir (o código troca a cada 5 min)
    if (now >= castCodeAt) { worker.add({5, "/api/cast/code", 0}, true); castCodeAt = now + (phonePanel ? 15000 : 30000); }
    if (phonePanel) {
      if (in.hit(Input::A) || in.hit(Input::B)) phonePanel = false;

    } else if (screen == HOME && railFocus) {
      if (in.hit(Input::UP) && railSel > 0) railSel--;
      if (in.hit(Input::DOWN) && railSel < 3) railSel++;
      if (in.hit(Input::RIGHT) || in.hit(Input::B)) railFocus = false;
      if (in.hit(Input::A)) {
        if (railSel == 0) goHome();
        else if (railSel == 1) { screen = SEARCH; keyRow = keyCol = 0; keyInRecent = false; railFocus = false; }
        else if (railSel == 2) { phonePanel = true; castCodeAt = 0; }
        else quit = true;
      }
    } else if (screen == HOME) {
      int n = (int)cur.feed.items.size();
      if (zone == 1 && !inResults) {
        if (in.hit(Input::LEFT)) { if (cat > 0) { cat--; scroll = 0; } else { railFocus = true; railSel = 0; } }
        if (in.hit(Input::RIGHT) && cat + 1 < (int)cats.size()) { cat++; scroll = 0; }
        if ((in.hit(Input::DOWN) || in.hit(Input::A)) && n) zone = 0;
      } else {
        if (in.hit(Input::LEFT)) { if (cur.sel % 4 > 0) cur.sel--; else { railFocus = true; railSel = inResults ? 1 : 0; } }
        if (in.hit(Input::RIGHT) && cur.sel % 4 < 3 && cur.sel + 1 < n) cur.sel++;
        if (in.hit(Input::DOWN)) { if (cur.sel + 4 < n) cur.sel += 4; else if (cur.sel / 4 < (n - 1) / 4) cur.sel = n - 1; }
        // rolagem sem fim: perto do fim da grade, pede a próxima página
        if (n > 0 && cur.sel >= n - 8 && cur.feed.more && !cur.feed.loadingMore && !cur.feed.loading) {
          cur.feed.loadingMore = true;
          if (inResults) worker.add({1, query, stamp, false, n}, true);
          else worker.add(cat == 0 ? Job{0, "", 0, false, n} : Job{3, cur.query, 0, false, n}, true);
        }
        if (in.hit(Input::UP)) { if (cur.sel >= 4) cur.sel -= 4; else if (!inResults) zone = 1; }
        if (in.hit(Input::A) && focusVid) startVideo(*focusVid, 0, false);
      }
      if (in.hit(Input::Y)) { screen = SEARCH; keyRow = keyCol = 0; keyInRecent = false; }
      if (in.hit(Input::X)) {
        if (inResults) { results.feed = Feed{}; results.feed.loading = true; worker.add({1, query, ++stamp, true}, true); }
        else { cur.feed.loading = true; worker.add(cat == 0 ? Job{0, "", 0, true} : Job{3, cur.query, 0, true}, true); }
        cur.sel = 0; scroll = 0; zone = 0; toast = "Buscando vídeos novos…"; toastUntil = now + 2500;
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
        if (in.hit(Input::LEFT)) { if (keyCol > 0) keyCol--; else { screen = HOME; railFocus = true; railSel = 1; } }
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
      if (in.hit(Input::B)) screen = HOME;
    } else if (screen == PLAYER) {
      bool any = false;
      if (in.hit(Input::A) || in.hit(Input::START)) { pl.setPaused(!pl.paused); any = true; }
      double jump = 0;
      if (in.hit(Input::LEFT)) jump = -10;
      if (in.hit(Input::RIGHT)) jump = 10;
      if (in.hit(Input::LB)) jump = -60;
      if (in.hit(Input::RB)) jump = 60;
      if (jump != 0 && pl.state != player::FAILED) {
        double to = std::clamp(pl.position() + jump, 0.0, pl.duration > 0 ? std::max(0.0, pl.duration - 3) : 1e9);
        startVideo(playing, to, playerFromRemote); any = true;
      }
      if (in.hit(Input::Y) && pl.state != player::FAILED) {
        quality = quality == 720 ? 480 : 720;
        startVideo(playing, pl.position(), playerFromRemote);
        toast = quality == 720 ? "Qualidade 720p" : "Qualidade 480p"; toastUntil = now + 2500; any = true;
      }
      if (in.hit(Input::X)) {
        if (nestSkip && nestOut) nestSkip = false;  // X depois de uma falha: tenta o Nest de novo
        else nestOut = !nestOut;
        saveAudio();
        if (!nestOut) { net::Response nr; net::get(srv, "/api/nest/stop", nr, 1500, 1024); }
        startVideo(playing, pl.position(), playerFromRemote);
        toast = nestOut ? "Som no " + nestName : "Som na TV"; toastUntil = now + 3000; any = true;
      }
      if (nestOut && (in.hit(Input::LT) || in.hit(Input::RT))) {
        nestOffsetMs += in.fired[Input::RT] || in.held[Input::RT] ? 40 : -40;
        nestOffsetMs = std::clamp(nestOffsetMs, -2000, 2000); pl.nestOffset = nestOffsetMs / 1000.0; saveAudio();
        char b[64]; std::snprintf(b, sizeof(b), "Sincronia com o %s: %+d ms", nestName.c_str(), nestOffsetMs);
        toast = b; toastUntil = now + 2500; any = true;
      }
      if (in.hit(Input::UP) || in.hit(Input::DOWN)) any = true;
      if (any) overlayUntil = now + 4000;
      if (in.hit(Input::B) || in.hit(Input::BACK)) stopVideo();
      if (pl.state == player::FAILED && pl.nestFailed && !nestSkip) {
        nestSkip = true;
        toast = "O " + nestName + " não respondeu: som na TV (X tenta de novo)"; toastUntil = now + 5000;
        startVideo(playing, pl.position(), playerFromRemote);
      }
      if (pl.state == player::ENDED) {
        SDL_LockMutex(remote.mu); remote.ended = playing.id; SDL_UnlockMutex(remote.mu);
        stopVideo();
      }
    }
    in.clear();

    SDL_LockMutex(remote.mu);
    remote.playing = screen == PLAYER ? playing.id : ""; remote.pos = pl.position(); remote.dur = pl.duration; remote.paused = pl.paused;
    SDL_UnlockMutex(remote.mu);

    if (focusVid) { if (const Thumbs::Slot* s = thumbs.get(thumbsWorker, focusVid->id, frame)) ambientWant = s->avg; }
    ambient = ui::mix(ambient, ambientWant, 0.06f);
    railOpen += ((railFocus ? 1.f : 0.f) - railOpen) * 0.25f;

    // ---------------------------------------------------------------- quadro do vídeo
    gfx::begin(g);
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
      const float gap = 24, cw = std::floor((ui::W - ui::X0 - 64 - 3 * gap) / 4), th = std::round(cw * 9 / 16), rowH = th + 106, top = 150;
      int n = (int)cur.feed.items.size(), row = cur.sel / 4;
      float want = zone == 1 || row == 0 ? 0 : row * rowH - 118;
      scroll += (want - scroll) * 0.2f;
      for (int i = 0; i < n && i < 600; i++) {
        float x = ui::X0 + (i % 4) * (cw + gap), y = top + (i / 4) * rowH - scroll;
        if (y > ui::H || y + rowH < 90) continue;
        const Video& v = cur.feed.items[(size_t)i];
        bool focused = zone == 0 && !railFocus && i == cur.sel;
        float& an = selAnim[i]; an += ((focused ? 1.f : 0.f) - an) * 0.25f;
        float s = 1 + 0.045f * an, w = cw * s, h = th * s, ix = x - (w - cw) / 2, iy = y - (h - th) / 2;
        if (an > 0.02f) {
          gfx::shadow(g, ix, iy + 10, w, h, 14, 22, 0.6f * an);
          gfx::rect(g, ix - 4, iy - 4, w + 8, h + 8, ui::alpha(rgb(0xffffff), an), 16);
        }
        const Thumbs::Slot* ts = thumbs.get(thumbsWorker, v.id, frame);
        if (ts) gfx::image(g, ts->tex, ix, iy, w, h, 12);
        else gfx::rect(g, ix, iy, w, h, ui::SURFACE, 12);
        if (v.dur > 0) {
          std::string d = fmtTime(v.dur); float dw = gfx::textWidth(g, ui::F_TINY, d) + 12;
          gfx::rect(g, ix + w - dw - 7, iy + h - 27, dw, 20, {0, 0, 0, 0.8f}, 5);
          gfx::text(g, ui::F_TINY, d, ix + w - dw / 2 - 7, iy + h - 24, ui::TEXT, 1);
        } else if (v.dur == 0) {
          gfx::rect(g, ix + w - 70, iy + h - 27, 63, 20, ui::A1, 5);
          gfx::text(g, ui::F_TINY, "AO VIVO", ix + w - 38, iy + h - 24, rgb(0xffffff), 1);
        }
        float ty = y + th + 14;
        for (auto& l : gfx::wrap(g, ui::F_CARD, v.title, cw, 2)) { gfx::text(g, ui::F_CARD, l, x, ty, focused ? ui::TEXT : rgb(0xe4e4e7)); ty += 23; }
        std::string meta = v.channel + (v.views.empty() ? "" : "  \xC2\xB7  " + v.views);
        gfx::text(g, ui::F_SMALL, gfx::ellipsize(g, ui::F_SMALL, meta, cw), x, ty + 3, ui::MUTED);
      }
      if (n == 0) {
        if (cur.feed.loading) { spinner(g, ui::X0 + (ui::W - ui::X0) / 2 - 32, 380, 22, t); gfx::text(g, ui::F_BODY, "Carregando vídeos…", ui::X0 + (ui::W - ui::X0) / 2 - 32, 420, ui::MUTED, 1); }
        else if (!cur.feed.error.empty()) {
          gfx::text(g, ui::F_BIG, "Não deu para falar com o notebook", ui::X0, 190, ui::TEXT);
          gfx::text(g, ui::F_BODY, cur.feed.error, ui::X0, 240, ui::MUTED);
          gfx::text(g, ui::F_BODY, "Ligue o servidor no notebook e aperte X para tentar de novo.", ui::X0, 270, ui::MUTED);
        } else gfx::text(g, ui::F_BODY, "Nada encontrado.", ui::X0, 190, ui::MUTED);
      }
      // topo fixo: o nome e as categorias (a grade passa por baixo)
      gfx::rect(g, 0, 0, ui::W, 118, ui::BG, 0);
      gfx::vgrad(g, 0, 118, ui::W, 36, ui::BG, ui::alpha(ui::BG, 0));
      wordmark(g, ui::X0, 22, 1, ui::BG);
      {
        float lx = ui::W - 64 - 196;
        if (!castWho.empty()) {
          float w = gfx::textWidth(g, ui::F_SMALL, "Celular: " + castWho) + 30;
          gfx::rect(g, lx - w - 16, 30, w, 28, ui::alpha(rgb(0x34c759), 0.16f), 14);
          gfx::rect(g, lx - w - 4, 41, 7, 7, rgb(0x34c759), 3.5f);
          gfx::text(g, ui::F_SMALL, "Celular: " + castWho, lx - w + 10, 34, ui::TEXT);
        }
        float hx3 = hint(g, lx, 32, "X", XX, "Atualizar");
        hint(g, hx3, 32, "Y", XY, "Buscar");
      }
      if (inResults) {
        gfx::text(g, ui::F_H2, gfx::ellipsize(g, ui::F_H2, cur.title, 760), ui::X0, 78, ui::TEXT);
        hint(g, ui::W - 64 - 120, 80, "B", XB, "Início");
      } else {
        float cx = ui::X0;
        for (int c = 0; c < (int)cats.size(); c++) {
          float w = gfx::textWidth(g, ui::F_BODY, cats[(size_t)c].title) + 34;
          bool sel = c == cat, f = sel && zone == 1 && !railFocus;
          if (f) gfx::shadow(g, cx, 76 + 5, w, 40, 20, 14, 0.5f);
          gfx::rect(g, cx, 76, w, 40, f ? rgb(0xffffff) : sel ? ui::alpha(rgb(0xffffff), 0.9f) : ui::alpha(rgb(0xffffff), 0.09f), 20);
          gfx::text(g, ui::F_BODY, cats[(size_t)c].title, cx + w / 2, 85, sel ? ui::INK : ui::TEXT, 1);
          if (cats[(size_t)c].feed.loading && cats[(size_t)c].feed.items.empty()) gfx::rect(g, cx + w - 12, 82, 6, 6, ui::A1, 3);
          cx += w + 10;
        }
      }
    } else if (screen == SEARCH) {
      gfx::rect(g, 0, 0, ui::W, ui::H, ui::alpha(ui::BG, 0.55f), 0);
      gfx::text(g, ui::F_BIG, "Buscar", ui::X0, 52, ui::TEXT);
      float fx = ui::X0, fy = 112, fw = 652, fh = 60;
      gfx::rect(g, fx, fy, fw, fh, ui::alpha(rgb(0xffffff), 0.08f), 30);
      gfx::rect(g, fx + 1, fy + 1, fw - 2, fh - 2, ui::SURFACE, 29);
      icon(g, 1, fx + 34, fy + 30, ui::MUTED, ui::SURFACE);
      float qx = fx + 62;
      if (query.empty()) gfx::text(g, ui::F_KEY, "O que você quer assistir?", qx, fy + 17, ui::FAINT);
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
      const char* acts[3] = {"Espaço", "Apagar", "Buscar"};
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
      gfx::text(g, ui::F_H2, "Buscas recentes", rx, 122, ui::TEXT);
      if (recent.empty()) gfx::text(g, ui::F_SMALL, "O que você buscar aparece aqui.", rx, 162, ui::FAINT);
      for (size_t i = 0; i < recent.size(); i++) {
        bool f = keyInRecent && (int)i == recentSel; float y = 160 + i * 58;
        gfx::rect(g, rx, y, 370, 48, f ? rgb(0xffffff) : ui::alpha(rgb(0xffffff), 0.06f), 24);
        icon(g, 1, rx + 26, y + 24, f ? ui::INK : ui::FAINT, f ? rgb(0xffffff) : ui::SURFACE);
        gfx::text(g, ui::F_BODY, gfx::ellipsize(g, ui::F_BODY, recent[i], 300), rx + 50, y + 13, f ? ui::INK : ui::TEXT);
      }
      gfx::vgrad(g, 0, ui::H - 70, ui::W, 70, ui::alpha(ui::BG, 0), ui::alpha(ui::BG, 0.96f));
      float hx2 = ui::X0;
      hx2 = hint(g, hx2, ui::H - 40, "A", XA, "Digitar");
      hx2 = hint(g, hx2, ui::H - 40, "X", XX, "Apagar");
      hx2 = hint(g, hx2, ui::H - 40, "Y", XY, "Espaço");
      hx2 = hint(g, hx2, ui::H - 40, "Menu", XG, "Buscar");
      hint(g, hx2, ui::H - 40, "B", XB, "Voltar");
    }

    if (screen == HOME || screen == SEARCH) {
      // barra lateral: ícones; com foco, abre com os nomes
      float rw = ui::RAIL + 200 * railOpen;
      if (railOpen > 0.01f) {
        gfx::rect(g, 0, 0, rw, ui::H, ui::alpha(ui::BG, 0.96f * railOpen), 0);
        gfx::hgrad(g, rw - 1, 0, 200, ui::H, ui::alpha(ui::BG, 0.96f * railOpen), ui::alpha(ui::BG, 0));
      }
      const char* names[4] = {"Início", "Buscar", "Celular", "Sair"};
      int page = screen == SEARCH ? 1 : inResults ? 1 : 0;
      for (int i = 0; i < 4; i++) {
        float y = 170 + i * 64 + (i == 3 ? 250 : 0);
        bool f = railFocus && railSel == i, act = i == page;
        if (f) gfx::rect(g, 30, y - 24, 52 + 168 * railOpen, 48, rgb(0xffffff), 24);
        else if (act) gfx::rect(g, 30, y - 24, 52, 48, ui::alpha(rgb(0xffffff), 0.1f), 24);
        if (act && !f) gfx::vgrad(g, 16, y - 12, 4, 24, ui::A1, ui::A2, 2);
        Color ic = f ? ui::INK : act ? ui::TEXT : ui::MUTED;
        icon(g, i, 56, y, ic, f ? rgb(0xffffff) : act ? ui::mix(ui::BG, rgb(0xffffff), 0.1f) : ui::BG);
        if (railOpen > 0.05f) gfx::text(g, ui::F_BODY, names[i], 94, y - 12, ui::alpha(f ? ui::INK : ui::TEXT, railOpen));
        if (i == 2) gfx::rect(g, 66, y - 18, 9, 9, !castWho.empty() ? rgb(0x34c759) : remoteOnline ? rgb(0xd4d4d8) : ui::FAINT, 4.5f);
      }
    }

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
        float frac = dur > 0 ? (float)std::clamp(pos / dur, 0.0, 1.0) : 0;
        if (frac > 0) gfx::hgrad(g, bx, by, std::max(6.f, bw * frac), 6, ui::A1, ui::A2, 3);
        gfx::rect(g, bx + bw * frac - 14, by - 11, 28, 28, ui::alpha(ui::A2, 0.35f), 14, 6);
        gfx::rect(g, bx + bw * frac - 8, by - 5, 16, 16, rgb(0xffffff), 8);
        gfx::text(g, ui::F_BODY, fmtTime(pos), bx, by + 22, ui::TEXT);
        if (dur > 0) gfx::text(g, ui::F_BODY, fmtTime(dur), bx + bw, by + 22, ui::MUTED, 2);
        float hx2 = ui::W / 2 - 430;
        hx2 = hint(g, hx2, by + 24, "A", XA, pl.paused ? "Continuar" : "Pausar");
        hx2 = hint(g, hx2, by + 24, "Y", XY, quality == 720 ? "720p" : "480p");
        hx2 = hint(g, hx2, by + 24, "X", XX, nestOut ? ("Som: " + nestName).c_str() : "Som: TV");
        hx2 = hint(g, hx2, by + 24, "\xE2\x97\x80 \xE2\x96\xB6", XG, "10 s");
        hx2 = hint(g, hx2, by + 24, "LB RB", XG, "1 min");
        hint(g, hx2, by + 24, "B", XB, "Voltar");
      }
      if (pl.paused && !pl.waiting()) {
        gfx::rect(g, ui::W / 2 - 52, ui::H / 2 - 52, 104, 104, {0, 0, 0, 0.5f}, 52);
        gfx::rect(g, ui::W / 2 - 18, ui::H / 2 - 22, 12, 44, rgb(0xffffff), 3);
        gfx::rect(g, ui::W / 2 + 6, ui::H / 2 - 22, 12, 44, rgb(0xffffff), 3);
      }
      if (pl.waiting()) {
        spinner(g, ui::W / 2, ui::H / 2, 26, t);
        gfx::text(g, ui::F_BODY, pl.state == player::CONNECTING ? "Pedindo o vídeo ao notebook…" : "Carregando…", ui::W / 2, ui::H / 2 + 46, ui::TEXT, 1);
      }
      if (pl.state == player::FAILED) {
        gfx::rect(g, ui::W / 2 - 340, ui::H / 2 - 74, 680, 148, ui::SURFACE, 22);
        gfx::text(g, ui::F_H2, "Não deu para tocar este vídeo", ui::W / 2, ui::H / 2 - 46, ui::TEXT, 1);
        gfx::text(g, ui::F_BODY, gfx::ellipsize(g, ui::F_BODY, pl.error, 600), ui::W / 2, ui::H / 2, ui::MUTED, 1);
      }
    }

    if (phonePanel) {
      gfx::rect(g, 0, 0, ui::W, ui::H, {0, 0, 0, 0.6f}, 0);
      float pw = 760, ph = 430, px = ui::W / 2 - pw / 2, py = ui::H / 2 - ph / 2;
      gfx::shadow(g, px, py + 10, pw, ph, 28, 40, 0.6f);
      gfx::rect(g, px, py, pw, ph, ui::SURFACE, 28);
      icon(g, 2, px + 56, py + 62, ui::TEXT, ui::SURFACE);
      gfx::text(g, ui::F_BIG, "Transmitir do celular", px + 92, py + 42, ui::TEXT);
      gfx::text(g, ui::F_BODY, "No app do YouTube, toque em Transmitir e escolha “" + brandName + "”.", px + 48, py + 108, ui::TEXT);
      gfx::text(g, ui::F_BODY, "Se não aparecer: sua foto → Configurações → Assistir na TV → Inserir código da TV:", px + 48, py + 140, ui::MUTED);
      if (!castCode.empty()) gfx::textGrad(g, ui::F_HERO, castCode, px + 48, py + 178, ui::A1, ui::A2);
      else gfx::text(g, ui::F_BODY, "Buscando o código… (o receptor do notebook precisa estar ligado)", px + 48, py + 192, ui::FAINT);
      gfx::text(g, ui::F_SMALL, "O código muda a cada 5 minutos. Depois de parear uma vez, o celular lembra do " + brandName + ".", px + 48, py + 248, ui::FAINT);
      gfx::rect(g, px + 48, py + 290, pw - 96, 1, ui::alpha(rgb(0xffffff), 0.08f), 0);
      gfx::text(g, ui::F_SMALL, "Ou no navegador do celular:", px + 48, py + 308, ui::MUTED);
      gfx::text(g, ui::F_BODY, "http://" + srv.ip + ":" + std::to_string(srv.port), px + 48 + gfx::textWidth(g, ui::F_SMALL, "Ou no navegador do celular:") + 10, py + 305, ui::TEXT);
      gfx::text(g, ui::F_SMALL, !castWho.empty() ? "Celular conectado: " + castWho : remoteOnline ? "Nenhum celular conectado ainda." : "Sem conexão com o notebook agora.",
                px + 48, py + 340, !castWho.empty() ? rgb(0x34c759) : ui::FAINT);
      hint(g, px + 48, py + 380, "A", XA, "Fechar");
    }
    if (!toast.empty() && now < toastUntil) {
      float w = gfx::textWidth(g, ui::F_BODY, toast) + 52;
      gfx::rect(g, ui::W / 2 - w / 2, 24, w, 46, ui::SURFACE2, 23);
      gfx::text(g, ui::F_BODY, toast, ui::W / 2, 36, ui::TEXT, 1);
    }

    bool shoot = shotPath && (int)frame == shotFrame;
    gfx::end(g, shoot ? shotPath : nullptr);
    if (shoot) quit = true;
    frame++;
    if (frame % 600 == 0 && screen == PLAYER) LOG("player: %s pos %.1f quadros %lld perdidos %lld", playing.id.c_str(), pl.position(), (long long)pl.shownFrames, (long long)pl.droppedFrames);
  }
  pl.halt();
  SDL_LockMutex(worker.mu); worker.quit = true; SDL_SignalCondition(worker.cv); SDL_UnlockMutex(worker.mu);
  remote.quit = true;
  SDL_LockMutex(thumbsWorker.mu); thumbsWorker.quit = true; SDL_BroadcastCondition(thumbsWorker.cv); SDL_UnlockMutex(thumbsWorker.mu);
  SDL_WaitThread(wt, nullptr);
  for (auto* th : tw) SDL_WaitThread(th, nullptr);
  SDL_WaitThread(rt, nullptr);
  vkDeviceWaitIdle(g.dev);
  LOG("%s saindo", brandName.c_str());
  SDL_Quit();
  return 0;
}
