// Player: puxa o MPEG-PS do servidor (docs/PROTOCOLO.md, /api/stream), decodifica com o pl_mpeg e entrega quadros
// YUV e áudio. Três threads: rede (enche o anel), decodificação (enche a fila de quadros e o áudio do SDL) e a
// principal (escolhe o quadro pelo relógio do áudio). No 360 cada thread pesada vai para um núcleo próprio: o xsdk
// cria toda thread no núcleo de quem a criou (xsdk/thread.h).
#pragma once
#include <SDL3/SDL.h>

#include <atomic>
#include <cmath>
#include <deque>
#include <string>
#include <vector>

#include "net.hpp"
#include "pl_mpeg.h"
#ifdef __XSDK__
#include "xsdk/thread.h"
#endif

namespace player {

inline void moveToHardwareThread(int hw) {
#ifdef __XSDK__
  xsdk_thread_move((uint32_t)hw);
#else
  (void)hw;
#endif
}

struct Frame { double pts = 0; int w = 0, h = 0; std::vector<uint8_t> yuv; };

// Ajudantes da decodificação por fatias do pl_mpeg (plm_video_run_parallel): a thread do decodificador roda a faixa 0
// e cada ajudante roda a sua num núcleo livre do 360 (hardware threads 3, 1 e 4; o decodificador fica na 2 e a tela na 0).
struct SlicePool {
  static constexpr int N = 3;
  SDL_Thread* t[N] = {}; SDL_Semaphore* go[N] = {}; SDL_Semaphore* done = nullptr;
  void (*job)(void*) = nullptr; void* args[N + 1] = {}; int hw[N] = {3, 1, 4}; int idx[N] = {};
  bool started = false;
  static SlicePool& get() { static SlicePool p; return p; }
  static int main(void* p) {
    SlicePool& s = get(); int i = *(int*)p;
    moveToHardwareThread(s.hw[i]);
    for (;;) { SDL_WaitSemaphore(s.go[i]); s.job(s.args[i + 1]); SDL_SignalSemaphore(s.done); }
    return 0;
  }
  void start() {
    if (started) return;
    started = true; done = SDL_CreateSemaphore(0);
    for (int i = 0; i < N; i++) { idx[i] = i; go[i] = SDL_CreateSemaphore(0); t[i] = SDL_CreateThread(main, "fatias", &idx[i]); }
  }
  static void run(void (*fn)(void*), void** a, int n) {
    SlicePool& s = get();
    int helpers = std::min(n - 1, N);
    s.job = fn;
    for (int i = 0; i < helpers; i++) { s.args[i + 1] = a[i + 1]; SDL_SignalSemaphore(s.go[i]); }
    fn(a[0]);
    for (int i = helpers + 1; i < n; i++) fn(a[i]);
    for (int i = 0; i < helpers; i++) SDL_WaitSemaphore(s.done);
  }
};

enum State { IDLE, CONNECTING, BUFFERING, PLAYING, ENDED, FAILED };

struct Player {
  net::Server srv;
  std::string id, title, error;
  double startAt = 0, duration = 0;
  int quality = 720;
  std::atomic<int> state{IDLE};
  std::atomic<bool> stop{false}, paused{false};

  // anel rede -> decodificação
  SDL_Mutex* mu = nullptr; SDL_Condition* cv = nullptr;
  std::vector<uint8_t> ring; size_t rhead = 0, rcount = 0; bool netEof = false, netErr = false;
  int sock = -1;
  SDL_Thread *netThread = nullptr, *decThread = nullptr;

  // fila de quadros (protegida por mu)
  std::deque<Frame*> frames; std::vector<Frame*> spare; int maxFrames = 10;
  bool decEnded = false;

  // áudio
  SDL_AudioStream* audio = nullptr; bool hasAudio = false;
  float sleepBase = 1; bool sleepFading = false;
  void sleepGain(float gain) {
    if (!audio) return;
    if (gain < 1 && !sleepFading) {
      sleepBase = SDL_GetAudioStreamGain(audio); sleepFading = true;
      LOG("timer: iniciando fade SDL (volume %.3f)", sleepBase);
    }
    if (!sleepFading) return;
    SDL_SetAudioStreamGain(audio, sleepBase * gain);
    if (gain >= 1) {
      sleepFading = false;
      LOG("timer: volume SDL restaurado %.3f", SDL_GetAudioStreamGain(audio));
    }
  }
  SDL_AtomicInt audioBaseMs{-1}, pushed{0};  // início do áudio (ms do fluxo) e amostras por canal entregues
  SDL_AtomicInt netBytes{0};   // bytes recebidos desde o último log de 5 s (velocidade real da rede até o Xbox)
  size_t ringMin = SIZE_MAX;   // nível mínimo do anel no intervalo (protegido por mu)
  double firstPts = -1; uint64_t wallStart = 0, pausedAt = 0, pausedTotal = 0;
  double lastPos = 0;
  int64_t droppedFrames = 0, shownFrames = 0;

  // Som no Nest (Google Cast): o servidor manda só o áudio para o alto-falante, o vídeo vem sem áudio e segue a posição
  // que o Nest informa (/api/nest/pos a cada 200 ms). nestOffset: ajuste fino em segundos (positivo adianta a imagem).
  bool nest = false; double nestOffset = 0;
  SDL_Thread* nestThread = nullptr; SDL_Mutex* nmu = nullptr;
  int nestState = 0;  // 0 esperando, 1 tocando, 2 pausado, 3 parado, 4 erro (protegido por nmu)
  double nestPos = 0, nestStampMs = 0;
  // Relógio suavizado: corre liso no relógio do Xbox e é puxado 10% por amostra para a posição do Nest (cada amostra
  // chega com 50-100 ms de variação; seguir cru dava trancos e quadros pulados). Salto acima de 0,4 s re-ancora.
  bool smValid = false; double smBase = 0, smWallMs = 0;
  int smBig = 0;                       // amostras seguidas com erro grande (salto só com duas confirmando)
  SDL_AtomicInt nestJumps{0}, nestSlow{0};  // saltos do relógio e respostas lentas descartadas (para o log)
  // Se o Nest some (cai, recarrega), o vídeo segue no relógio do Xbox a partir do último ponto bom, em vez de congelar.
  double lastGood = -1, lastGoodMs = 0, nestLostMs = 0;
  std::atomic<int> nestCmd{0};  // 1 pausar, 2 continuar
  std::atomic<bool> nestFailed{false};  // o Nest não respondeu: o app troca para o som da TV sozinho

  void init() {
    mu = SDL_CreateMutex(); cv = SDL_CreateCondition(); nmu = SDL_CreateMutex();
    SlicePool::get().start();
    plm_video_run_parallel = SlicePool::run;
    plm_video_parallel_jobs = SlicePool::N + 1;
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 44100};
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!audio) SDL_Log("áudio: %s", SDL_GetError());
  }

  static int netMain(void* p) { ((Player*)p)->netLoop(); return 0; }
  static int decMain(void* p) { ((Player*)p)->decLoop(); return 0; }
  static int nestMain(void* p) { ((Player*)p)->nestLoop(); return 0; }

  void start(const net::Server& s, const std::string& vid, double t, const std::string& ttl, double dur, int q) {
    halt(false);  // o próximo play no Nest troca o áudio sozinho; parar antes só atrasaria
    srv = s; id = vid; startAt = std::max(0.0, t); title = ttl; duration = dur; quality = q; error.clear();
    ring.assign(6u << 20, 0); rhead = rcount = 0; netEof = netErr = false; decEnded = false;
    SDL_SetAtomicInt(&audioBaseMs, -1); SDL_SetAtomicInt(&pushed, 0); firstPts = -1; wallStart = 0; pausedAt = 0; pausedTotal = 0; lastPos = startAt;
    droppedFrames = shownFrames = 0;
    stop = false; paused = false; state = CONNECTING;
    if (audio) { SDL_ClearAudioStream(audio); SDL_ResumeAudioStreamDevice(audio); }
    nestFailed = false;
    SDL_LockMutex(nmu); nestState = 0; nestPos = 0; nestStampMs = 0; smValid = false; lastGood = -1; lastGoodMs = 0; nestLostMs = 0; SDL_UnlockMutex(nmu); nestCmd = 0;
    netThread = SDL_CreateThread(netMain, "rede", this);
    decThread = SDL_CreateThread(decMain, "decod", this);
    if (nest) nestThread = SDL_CreateThread(nestMain, "nest", this);
  }

  void halt(bool stopNest = true) {
    if (!netThread && !decThread) return;
    stop = true;
    SDL_LockMutex(mu); SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
    if (sock >= 0) ::shutdown(sock, SHUT_RDWR);
    if (netThread) SDL_WaitThread(netThread, nullptr);
    if (decThread) SDL_WaitThread(decThread, nullptr);
    if (nestThread) SDL_WaitThread(nestThread, nullptr);
    netThread = decThread = nestThread = nullptr;
    if (nest && stopNest) {
      // fora da thread principal: com o servidor lento, a tela travava até 1,5 s esperando a resposta
      auto* s = new net::Server(srv);
      SDL_Thread* t = SDL_CreateThread([](void* p) -> int {
        auto* sv = (net::Server*)p; net::Response r; net::get(*sv, "/api/nest/stop", r, 1500, 1024); delete sv; return 0;
      }, "nest-stop", s);
      if (t) SDL_DetachThread(t); else delete s;
    }
    for (auto* f : frames) spare.push_back(f);
    frames.clear();
    if (audio) SDL_ClearAudioStream(audio);
    state = IDLE;
  }

  void netLoop() {
    moveToHardwareThread(4);
    net::Response r; std::vector<uint8_t> extra;
    char path[256];
    std::snprintf(path, sizeof(path), "/api/stream/%s?t=%.1f&q=%d%s", id.c_str(), startAt, quality, nest ? "&a=0" : "");
    int fd = net::openGet(srv, path, 20000, r, extra);
    if (fd < 0 || r.status != 200) {
      if (fd >= 0) ::close(fd);
      SDL_LockMutex(mu); netErr = true; SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
      error = fd < 0 ? "Sem conexão com o servidor do notebook" : "O servidor não conseguiu abrir este vídeo";
      return;
    }
    sock = fd;
    std::string d = net::header(r, "X-Duration");
    if (!d.empty() && duration <= 0) duration = std::atof(d.c_str());
    std::string tt = net::header(r, "X-Title");
    if (!tt.empty() && title.empty()) title = net::urlDecode(tt);
    if (state == CONNECTING) state = BUFFERING;
    auto put = [&](const uint8_t* p, size_t n) {
      SDL_LockMutex(mu);
      while (n > 0 && !stop) {
        while (rcount == ring.size() && !stop) SDL_WaitConditionTimeout(cv, mu, 100);
        size_t tail = (rhead + rcount) % ring.size();
        size_t room = std::min(ring.size() - rcount, ring.size() - tail);
        size_t k = std::min(room, n);
        std::memcpy(&ring[tail], p, k); rcount += k; p += k; n -= k; SDL_AddAtomicInt(&netBytes, (int)k);
        SDL_BroadcastCondition(cv);
      }
      SDL_UnlockMutex(mu);
    };
    if (!extra.empty()) put(extra.data(), extra.size());
    std::vector<uint8_t> buf(64 << 10);
    while (!stop) {
      ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && !stop) continue;  // pausa longa: o servidor só espera
      if (n <= 0) break;
      put(buf.data(), (size_t)n);
    }
    sock = -1; ::close(fd);
    SDL_LockMutex(mu); netEof = true; SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
  }

  // Chamado pelo pl_mpeg quando quer mais bytes: entrega o que houver no anel, esperando até 100 ms.
  static void loadCb(plm_buffer_t* b, void* user) {
    Player& p = *(Player*)user;
    uint8_t tmp[64 << 10]; size_t got = 0;
    SDL_LockMutex(p.mu);
    if (p.rcount == 0 && !p.netEof && !p.netErr && !p.stop) SDL_WaitConditionTimeout(p.cv, p.mu, 100);
    got = std::min(p.rcount, sizeof(tmp));
    for (size_t i = 0; i < got;) {
      size_t k = std::min(got - i, p.ring.size() - p.rhead);
      std::memcpy(tmp + i, &p.ring[p.rhead], k); p.rhead = (p.rhead + k) % p.ring.size(); i += k;
    }
    p.rcount -= got;
    if (p.rcount < p.ringMin) p.ringMin = p.rcount;
    bool ended = (p.netEof || p.netErr) && p.rcount == 0;
    SDL_BroadcastCondition(p.cv);
    SDL_UnlockMutex(p.mu);
    if (got) plm_buffer_write(b, tmp, got);
    if (ended && !got) plm_buffer_signal_end(b);
  }

  double audioQueuedSec() { return audio ? SDL_GetAudioStreamQueued(audio) / (44100.0 * 2 * 4) : 0; }

  void decLoop() {
    moveToHardwareThread(2);
    plm_buffer_t* buf = plm_buffer_create_with_capacity(512 << 10);
    plm_buffer_set_load_callback(buf, loadCb, this);
    plm_t* plm = plm_create_with_buffer(buf, 1);
    while (!stop && !plm_has_headers(plm)) {
      SDL_LockMutex(mu); bool dead = (netEof || netErr) && rcount == 0; SDL_UnlockMutex(mu);
      if (dead) break;
      SDL_Delay(5);
    }
    if (stop || !plm_has_headers(plm)) {
      if (!stop) { if (error.empty()) error = "O vídeo não chegou (o servidor fechou a conexão)"; state = FAILED; }
      plm_destroy(plm); return;
    }
    hasAudio = audio && plm_get_num_audio_streams(plm) > 0;
    plm_set_audio_enabled(plm, hasAudio ? 1 : 0);
    bool videoDone = false, audioDone = !hasAudio;
    double lastDecodedPts = -1; int statFrames = 0, statIdle = 0, statSkip0 = plm_video_skipped_b_total; uint64_t statNs = 0, statAt = SDL_GetTicks();
    plm_video_skip_b_frames = 0;
    while (!stop) {
      bool did = false;
      // áudio primeiro e até sobrar meio segundo: um quadro de vídeo no 360 leva mais que um bloco de áudio (26 ms)
      for (int k = 0; k < 48 && !audioDone && !stop && audioQueuedSec() < 0.6; k++) {
        plm_samples_t* s = plm_decode_audio(plm);
        if (!s) { if (plm_has_ended(plm)) audioDone = true; break; }
        SDL_PutAudioStreamData(audio, s->interleaved, (int)(s->count * 2 * sizeof(float)));
        SDL_AddAtomicInt(&pushed, (int)s->count);
        if (SDL_GetAtomicInt(&audioBaseMs) < 0) SDL_SetAtomicInt(&audioBaseMs, (int)std::lround(s->time * 1000));
        did = true;
      }
      // atrasado em relação ao áudio: pula quadros B até alcançar
      double c = clock();
      plm_video_skip_b_frames = (c >= 0 && lastDecodedPts >= 0 && lastDecodedPts < c - (nest ? 0.12 : 0.04)) ? 1 : 0;
      SDL_LockMutex(mu); bool room = (int)frames.size() < maxFrames; SDL_UnlockMutex(mu);
      if (!videoDone && room) {
        uint64_t t0 = SDL_GetTicksNS();
        plm_frame_t* f = plm_decode_video(plm);
        if (f) { enqueue(f); did = true; lastDecodedPts = f->time; statFrames++; statNs += SDL_GetTicksNS() - t0; }
        else if (plm_has_ended(plm)) videoDone = true;
      }
      if (videoDone && audioDone) break;
      if (!did) { SDL_Delay(2); statIdle++; }
      if (SDL_GetTicks() - statAt >= 5000) {
        SDL_LockMutex(mu); size_t ring_ = rcount, rmin = ringMin == SIZE_MAX ? rcount : ringMin; ringMin = SIZE_MAX; int q = (int)frames.size(); SDL_UnlockMutex(mu);
        double mbit = SDL_SetAtomicInt(&netBytes, 0) * 8.0 / 1e6 / ((SDL_GetTicks() - statAt) / 1000.0);
        int st = 0; SDL_LockMutex(nmu); st = nestState; SDL_UnlockMutex(nmu);
        LOG("decod: %d quadros (%.1f ms cada), %d B pulados, fila %d, anel %u KB (mín %u), rede %.1f Mbit/s, nest %d, perdidos %lld, ocioso %d, saltos %d, lentas %d",
                statFrames, statFrames ? statNs / 1e6 / statFrames : 0.0, plm_video_skipped_b_total - statSkip0, q, (unsigned)(ring_ >> 10),
                (unsigned)(rmin >> 10), mbit, nest ? st : -1, (long long)droppedFrames, statIdle, SDL_SetAtomicInt(&nestJumps, 0), SDL_SetAtomicInt(&nestSlow, 0));
        statAt = SDL_GetTicks(); statFrames = 0; statNs = 0; statIdle = 0; statSkip0 = plm_video_skipped_b_total;
      }
    }
    SDL_LockMutex(mu); decEnded = true; SDL_UnlockMutex(mu);
    plm_destroy(plm);
  }

  void enqueue(plm_frame_t* f) {
    SDL_LockMutex(mu);
    Frame* fr = spare.empty() ? new Frame() : spare.back();
    if (!spare.empty()) spare.pop_back();
    SDL_UnlockMutex(mu);
    int w = (int)f->width, h = (int)f->height, cw = (w + 1) / 2, ch = (h + 1) / 2;
    fr->w = w; fr->h = h; fr->pts = f->time;
    fr->yuv.resize((size_t)w * h + 2 * (size_t)cw * ch);
    uint8_t* o = fr->yuv.data();
    for (int y = 0; y < h; y++, o += w) std::memcpy(o, f->y.data + (size_t)y * f->y.width, (size_t)w);
    for (int y = 0; y < ch; y++, o += cw) std::memcpy(o, f->cb.data + (size_t)y * f->cb.width, (size_t)cw);
    for (int y = 0; y < ch; y++, o += cw) std::memcpy(o, f->cr.data + (size_t)y * f->cr.width, (size_t)cw);
    SDL_LockMutex(mu); frames.push_back(fr); SDL_UnlockMutex(mu);
  }

  // Manda o Nest tocar quando o vídeo já tem os primeiros quadros (som e imagem começam juntos) e acompanha a posição.
  void nestLoop() {
    moveToHardwareThread(5);
    for (;;) {
      SDL_LockMutex(mu); bool ready = (int)frames.size() >= 4 || decEnded; SDL_UnlockMutex(mu);
      if (stop || state == FAILED) return;
      if (ready) break;
      SDL_Delay(30);
    }
    char path[160];
    std::snprintf(path, sizeof(path), "/api/nest/play?id=%s&t=%.2f", id.c_str(), startAt);
    net::Response r;
    if (!net::get(srv, path, r, 5000, 1024)) { error = "O servidor não respondeu ao pedido do Nest"; nestFailed = true; state = FAILED; return; }
    uint64_t erroDesde = 0, pedidoEm = SDL_GetTicks(), comandoEm = 0; bool tocou = false;
    int esperado = 0;  // estado que o Xbox pediu e o Nest ainda não mostrou (1 tocando, 2 pausado)
    while (!stop) {
      int c = nestCmd.exchange(0);
      if (c == 1) net::get(srv, "/api/nest/pause", r, 2000, 1024);
      if (c == 2) net::get(srv, "/api/nest/resume", r, 2000, 1024);
      if (c) { comandoEm = SDL_GetTicks(); esperado = c == 1 ? 2 : 1; }
      if (c == 2 && !tocou) pedidoEm = SDL_GetTicks();
      double a = SDL_GetTicksNS() / 1e6;
      net::Response pr;
      if (net::get(srv, "/api/nest/pos", pr, 2000, 1024)) {
        double b = SDL_GetTicksNS() / 1e6;
        auto f = net::split(std::string(pr.body.begin(), pr.body.end()), '\t');
        int st = f[0] == "PLAYING" ? 1 : f[0] == "PAUSED" ? 2 : f[0] == "IDLE" ? 3 : f[0] == "ERRO" ? 4 : 0;
        SDL_LockMutex(nmu);
        nestState = st;
        if (f.size() > 1) {
          nestPos = std::atof(f[1].c_str()) - startAt; nestStampMs = (a + b) / 2;  // o servidor manda a posição absoluta no vídeo
          if (st == 1) {
            double mine = smBase + (nestStampMs - smWallMs) / 1000.0, err = nestPos - mine;
            if (!smValid) { smBase = nestPos; smWallMs = nestStampMs; smValid = true; smBig = 0; }
            else if (b - a > 120) SDL_AddAtomicInt(&nestSlow, 1);  // resposta lenta no Wi-Fi: o valor já chegou velho
            else if (std::fabs(err) > 0.5) {
              if (++smBig >= 2) { smBase = nestPos; smWallMs = nestStampMs; smBig = 0; SDL_AddAtomicInt(&nestJumps, 1); }
            } else { smBig = 0; smBase += err * 0.05; }
          } else smValid = false;
        }
        SDL_UnlockMutex(nmu);
        if (st == 1) tocou = true;
        // Pausado ou retomado no próprio Nest (toque em cima, Google Home): a imagem acompanha. Só depois de vê-lo
        // tocando este vídeo (logo após o play, o servidor ainda pode mostrar o vídeo anterior pausado). Enquanto o Nest não
        // mostra o último pedido do Xbox (até 10 s), o estado antigo dele não conta como mudança vinda de fora.
        if (esperado && (st == esperado || SDL_GetTicks() - comandoEm > 10000)) esperado = 0;
        if (tocou && !esperado && nestCmd == 0) {
          if (st == 1 && paused) { paused = false; LOG("nest: retomado no alto-falante, o vídeo segue"); }
          else if (st == 2 && !paused) { paused = true; LOG("nest: pausado no alto-falante, o vídeo pausa"); }
        }
        if (st == 4) {
          if (!erroDesde) erroDesde = SDL_GetTicks();
          if (SDL_GetTicks() - erroDesde > 3000) { error = "O Nest não tocou: " + (f.size() > 2 ? f[2] : std::string("sem resposta")); nestFailed = true; state = FAILED; return; }
        } else erroDesde = 0;
        // nunca começou a tocar: não deixa o vídeo parado esperando (pausado no começo não conta; o prazo recomeça ao continuar)
        if (!tocou && !paused && SDL_GetTicks() - pedidoEm > 15000) { error = "O Nest não começou a tocar"; nestFailed = true; state = FAILED; return; }
      }
      SDL_Delay(200);
    }
  }

  // Relógio do vídeo em segundos do fluxo: pelo Nest, pelo áudio que já tocou, ou pelo relógio de parede sem áudio.
  double clock() {
    if (nest) {
      SDL_LockMutex(nmu);
      double c = -1, nowMs = SDL_GetTicksNS() / 1e6;
      if (nestState == 1 && smValid) c = smBase + (nowMs - smWallMs) / 1000.0 + nestOffset;
      else if (nestState == 1) c = nestPos + (nowMs - nestStampMs) / 1000.0 + nestOffset;
      else if (nestState == 2) c = nestPos + nestOffset;
      if (c >= 0) { lastGood = c; lastGoodMs = nowMs; nestLostMs = 0; }
      else if (lastGood >= 0 && !paused) {
        if (!nestLostMs) nestLostMs = nowMs;
        if (nowMs - nestLostMs > 1500) c = lastGood + (nowMs - lastGoodMs) / 1000.0;  // segue sem o Nest
      }
      SDL_UnlockMutex(nmu);
      return c;
    }
    int base = SDL_GetAtomicInt(&audioBaseMs);
    if (hasAudio && base >= 0) {
      double played = SDL_GetAtomicInt(&pushed) / 44100.0 - audioQueuedSec() - 0.04;
      return base / 1000.0 + std::max(0.0, played);
    }
    if (firstPts < 0 || wallStart == 0) return -1;
    uint64_t now = SDL_GetTicks();
    uint64_t pausedNow = pausedAt ? now - pausedAt : 0;
    return firstPts + (double)(now - wallStart - pausedTotal - pausedNow) / 1000.0;
  }

  void setPaused(bool p) {
    if (p == paused) return;
    paused = p;
    if (nest) nestCmd = p ? 1 : 2;
    if (audio) { if (p) SDL_PauseAudioStreamDevice(audio); else SDL_ResumeAudioStreamDevice(audio); }
    if (p) pausedAt = SDL_GetTicks(); else if (pausedAt) { pausedTotal += SDL_GetTicks() - pausedAt; pausedAt = 0; }
  }

  // Quadro que deve aparecer agora (o chamador devolve com recycle), ou nullptr.
  Frame* take() {
    SDL_LockMutex(mu);
    Frame* show = nullptr;
    if (firstPts < 0 && !frames.empty()) {
      SDL_LockMutex(nmu); bool nestTocando = nestState == 1; SDL_UnlockMutex(nmu);
      bool audioReady = nest ? nestTocando : (!hasAudio || SDL_GetAtomicInt(&audioBaseMs) >= 0);
      if (audioReady && ((int)frames.size() >= 4 || decEnded)) { firstPts = frames.front()->pts; wallStart = SDL_GetTicks(); }
    }
    if (firstPts >= 0 && !paused) {
      double c = clock();
      while (!frames.empty() && frames.front()->pts <= c + 0.010) {
        if (show) { spare.push_back(show); droppedFrames++; }
        show = frames.front(); frames.pop_front();
      }
      if (show) { shownFrames++; if (state == BUFFERING || state == CONNECTING) state = PLAYING; }
    }
    bool drained = decEnded && frames.empty();
    SDL_UnlockMutex(mu);
    if (show) lastPos = startAt + std::max(0.0, show->pts - (firstPts >= 0 ? firstPts : show->pts));
    if (drained && state != FAILED && (!hasAudio || audioQueuedSec() < 0.05)) state = ENDED;
    return show;
  }
  void recycle(Frame* f) { SDL_LockMutex(mu); spare.push_back(f); SDL_UnlockMutex(mu); }

  double position() const { return lastPos; }
  bool waiting() const { return state == CONNECTING || state == BUFFERING; }
};

}  // namespace player
