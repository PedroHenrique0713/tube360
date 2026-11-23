// Player: negocia MPEG-TS/H.264 ou MPEG-PS/MPEG-1 em /api/stream e entrega quadros
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
#include "http_chunks.hpp"
#include "pl_mpeg.h"
#include "h264.hpp"
#include "yt/media.hpp"
#include "nest.hpp"
#ifdef __XSDK__
#include "xsdk/thread.h"
#include "xsdk/audio.h"
#endif

namespace player {

inline void moveToHardwareThread(int hw) {
#ifdef __XSDK__
  xsdk_thread_move((uint32_t)hw);
#else
  (void)hw;
#endif
}

struct Frame { double pts = 0; int w = 0, h = 0; bool bt709 = false, fullRange = false; std::vector<uint8_t> yuv; };

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
  nest::Speaker directSpeaker;
  std::atomic<bool> castMediaReady{false};
  net::Server srv;
  std::string id, title, error;
  double startAt = 0, duration = 0;
  int quality = 720;
  std::atomic<int> state{IDLE};
  std::atomic<bool> stop{false}, paused{false}, live{false};  // live: o servidor mandou X-Live (sem duração nem busca)

  // anel rede -> decodificação
  SDL_Mutex* mu = nullptr; SDL_Condition* cv = nullptr;
  std::vector<uint8_t> ring; size_t rhead = 0, rcount = 0; bool netEof = false, netErr = false;
  int sock = -1;
  SDL_Thread *netThread = nullptr, *decThread = nullptr;

  // fila de quadros (protegida por mu)
  std::deque<Frame*> frames; std::vector<Frame*> spare; int maxFrames = 10;
  bool decEnded = false;
  std::atomic<bool> original{false}, retryConverted{false};
  std::atomic<bool> allowOriginal{true};
  bool direct = false, refreshed = false;
  std::string cacheBase;
  std::unique_ptr<yt::Transport> transport;
  std::unique_ptr<yt::Client> youtube;
  yt::Video media;
  uint64_t validUntil = 0;
  int cachedQuality = 0;
  std::atomic<bool> mediaReady{false}, renew{false};
  std::atomic<int> actualHeight{0};
  int queueMin = 0;
  uint64_t originalWatch = 0;
  std::atomic<unsigned> pauseChanges{0};

  // áudio
  SDL_AudioStream* audio = nullptr; std::atomic<bool> hasAudio{false};
  float normalGain = 1, fadeGain = 1;
  std::atomic<bool> audible{true};
#ifdef __XSDK__
  std::atomic<bool> drainingDriver{false};
  std::atomic<uint32_t> playedAtMute{0};
#endif
  bool deviceDrained() {
#ifdef __XSDK__
    if (drainingDriver) {
      XsdkAudioStats stats{};
      xsdk_audio_stats(&stats);
      // Oito blocos na fila, um em mixagem e o último entregue ao kernel. Só played avança com um bloco real.
      if (stats.played - playedAtMute.load() < XSDK_AUDIO_FRAMES_AHEAD_DEFAULT + 2) return false;
      drainingDriver = false;
    }
#endif
    return !audible;
  }
  void sleepGain(float gain) {
    if (gain == fadeGain) return;
    fadeGain = gain;
    if (audio && audible) SDL_SetAudioStreamGain(audio, normalGain * gain);
  }
  void tvSound(bool on) {
    if (audible == on) return;
    if (audio && !SDL_SetAudioStreamGain(audio, on ? normalGain * fadeGain : 0.0f)) return;
#ifdef __XSDK__
    if (!on && audio && firstPts >= 0) {
      XsdkAudioStats stats{};
      xsdk_audio_stats(&stats);
      playedAtMute = stats.played;
      drainingDriver = true;
    }
#endif
    audible = on;
  }
  SDL_AtomicInt audioBaseMs{-1}, pushed{0};  // início do áudio (ms do fluxo) e amostras por canal entregues
  SDL_AtomicInt netBytes{0};   // bytes recebidos desde o último log de 5 s (velocidade real da rede até o Xbox)
  size_t ringMin = SIZE_MAX;   // nível mínimo do anel no intervalo (protegido por mu)
  double firstPts = -1; uint64_t wallStart = 0, pausedAt = 0, pausedTotal = 0;
  double lastPos = 0;
  int64_t droppedFrames = 0, shownFrames = 0;

  // A reserva PCM e a posse da saída são independentes do último relógio recebido do Nest.
  enum Output { CAST, RELEASING, LOCAL, PRIMING };
  std::atomic<Output> output{LOCAL};
  std::atomic<int> trimMs{-1}, cueMs{-1}, wantedOffsetMs{0};
  std::atomic<bool> armed{false}, mutedForCast{false}, localPlaying{false}, pictureReady{false};
  uint64_t mutedAt = 0, lastFrameMs = 0;
  std::atomic<uint32_t> lastPacketMs{0};
  int deviceMs = 40, outputBytesPerSec = 0, outputFrameBytes = 0;
  bool nest = false, speakerRequested = false; double nestOffset = 0;
  SDL_Thread* nestThread = nullptr; SDL_Mutex* nmu = nullptr;
  int nestState = 0;  // 0 aguardando/obsoleto, 1 tocando, 2 pausado, 3 parado, 4 erro, 5 bufferizando
  double nestPos = 0, nestStampMs = 0;
  std::atomic<int> session{0}, nestCmd{0}, masterMs{-1}, speakerTargetMs{-1};
  int previousSession = 0;
  std::atomic<bool> nestFailed{false};
  std::atomic<int> speakerPause{0};  // mudança feita no alto-falante, aplicada pela thread da tela
  uint64_t openedMs = 0, readyMs = 0, lastLiveOpen = 0, tuneAt = 0;
  double screenSec = -1, tickMs = 0, liveDebt = 0, nextLiveFrame = 0;
  bool tracking = false, refill = false, reservePaused = false;

  void init() {
    mu = SDL_CreateMutex(); cv = SDL_CreateCondition(); nmu = SDL_CreateMutex();
    SlicePool::get().start();
    plm_video_run_parallel = SlicePool::run;
    plm_video_parallel_jobs = SlicePool::N + 1;
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 44100};
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!audio) SDL_Log("áudio: %s", SDL_GetError());
    else {
      normalGain = SDL_GetAudioStreamGain(audio);
      SDL_AudioSpec device{};
      int samples = 0;
      if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(audio), &device, &samples) && device.freq > 0) {
        deviceMs = std::max(1, (samples * 1000 + device.freq - 1) / device.freq);
#ifdef __XSDK__
        // SDL informa 256 amostras por chamada; o driver xsdk abre a fila com a profundidade padrão.
        deviceMs = (XSDK_AUDIO_FRAMES_AHEAD_DEFAULT * XSDK_AUDIO_FRAME_SAMPLES * 1000 + XSDK_AUDIO_RATE_HZ / 2) / XSDK_AUDIO_RATE_HZ;
#endif
        outputFrameBytes = SDL_AUDIO_FRAMESIZE(device);
        outputBytesPerSec = device.freq * outputFrameBytes;
      }
    }
  }

  static int netMain(void* p) { ((Player*)p)->netLoop(); return 0; }
  static int decMain(void* p) { ((Player*)p)->decLoop(); return 0; }
  static int nestMain(void* p) { ((Player*)p)->nestLoop(); return 0; }

  void start(const net::Server& s, const std::string& vid, double t, const std::string& ttl, double dur, int q, bool speaker, bool tryDirect = true, bool renewing = false) {
    if (vid != id || state == IDLE || quality != q) allowOriginal = true;
    halt(!speaker);
    direct = tryDirect && (!speaker || directSpeaker.configured());
    castMediaReady = false;
    endLiveReader = false; refreshed = renewing; renew = mediaReady = false; actualHeight = 0;
    if (renewing) validUntil = 0;
    SDL_SetAtomicInt(&netBytes, 0);
    original = retryConverted = false; originalWatch = 0; queueMin = maxFrames;
    speakerRequested = speaker; nest = false;
    srv = s; id = vid; startAt = std::max(0.0, t); title = ttl; duration = dur; quality = q; error.clear();
    ring.assign(6u << 20, 0); rhead = rcount = 0; netEof = netErr = false; decEnded = false;
    SDL_SetAtomicInt(&audioBaseMs, -1); SDL_SetAtomicInt(&pushed, 0); firstPts = -1; wallStart = 0; pausedAt = 0; pausedTotal = 0; lastPos = startAt;
    droppedFrames = shownFrames = 0;
    stop = false; paused = false; live = false; state = CONNECTING;
    hasAudio = false; readyMs = tuneAt = 0; screenSec = -1; liveDebt = 0; tickMs = nextLiveFrame = 0;
    openedMs = SDL_GetTicks(); masterMs = -1; tracking = refill = reservePaused = false; session = 0; speakerPause = 0; speakerTargetMs = -1;
    if (audio) { SDL_PauseAudioStreamDevice(audio); SDL_ClearAudioStream(audio); SDL_SetAudioStreamFrequencyRatio(audio, 1.0f); }
    output = speaker ? CAST : RELEASING;
    trimMs = cueMs = -1; armed = mutedForCast = localPlaying = pictureReady = false;
    mutedAt = lastFrameMs = 0;
    tvSound(false);
    nestFailed = false;
    SDL_LockMutex(nmu); nestState = 0; nestPos = 0; nestStampMs = 0; SDL_UnlockMutex(nmu); nestCmd = 0;
    if (direct) {
      output = speaker || !directSpeaker.sample().quiet ? CAST : LOCAL; original = true;
      decThread = yt::worker(decMain, "youtube", this);
      if (!decThread) state = FAILED;
      nestThread = yt::worker(nestMain, "nest", this);
      return;
    }
    netThread = SDL_CreateThread(netMain, "rede", this);
    decThread = SDL_CreateThread(decMain, "decod", this);
    nestThread = SDL_CreateThread(nestMain, "nest", this);
  }

  void halt(bool stopNest = true) {
    stop = true;
    if (stopNest) directSpeaker.release();
    if (!netThread && !decThread) return;
    uint64_t began = SDL_GetTicks();
    LOG("player: encerrando %s", id.c_str());
    endLiveReader = true;
    tvSound(false);
    if (audio) SDL_PauseAudioStreamDevice(audio);
    SDL_LockMutex(mu); SDL_BroadcastCondition(cv);
    if (sock >= 0) ::shutdown(sock, SHUT_RDWR);
    SDL_UnlockMutex(mu);
    if (netThread) SDL_WaitThread(netThread, nullptr);
    if (decThread) SDL_WaitThread(decThread, nullptr);
    if (nestThread) SDL_WaitThread(nestThread, nullptr);
    netThread = decThread = nestThread = nullptr;
    if (!direct && speakerRequested && session != 0) previousSession = session.load();
    if (!direct && speakerRequested && stopNest && previousSession) {
      struct Stop { net::Server srv; int sid; };
      auto* request = new Stop{srv, previousSession};
      SDL_Thread* t = SDL_CreateThread([](void* p) -> int {
        auto* req = (Stop*)p; net::Response r;
        std::string path = "/api/nest/stop";
        if (req->sid > 0) path += "?sid=" + std::to_string(req->sid);
        net::get(req->srv, path, r, 1200, 1024);
        delete req; return 0;
      }, "nest-stop", request);
      if (t) SDL_DetachThread(t); else delete request;
      previousSession = 0;
    }
    size_t bytes = ring.capacity();
    for (auto* f : frames) { bytes += f->yuv.capacity(); delete f; }
    for (auto* f : spare) { bytes += f->yuv.capacity(); delete f; }
    frames.clear(); spare.clear();
    std::vector<uint8_t>().swap(ring);
    LOG("player: fim %s, buffers %u KiB liberados, %llu ms", id.c_str(), (unsigned)(bytes >> 10),
        (unsigned long long)(SDL_GetTicks() - began));
    if (audio) SDL_ClearAudioStream(audio);
    state = IDLE;
  }

  std::atomic_bool endLiveReader{false};
  void putBytes(const uint8_t* p, size_t n) {
    lastPacketMs = (uint32_t)SDL_GetTicks();
    SDL_LockMutex(mu);
    while (n > 0 && !stop && !endLiveReader) {
      while (rcount == ring.size() && !stop && !endLiveReader) SDL_WaitConditionTimeout(cv, mu, 100);
      if (stop || endLiveReader) break;
      size_t tail = (rhead + rcount) % ring.size();
      size_t room = std::min(ring.size() - rcount, ring.size() - tail);
      size_t k = std::min(room, n);
      std::memcpy(&ring[tail], p, k); rcount += k; p += k; n -= k; SDL_AddAtomicInt(&netBytes, (int)k);
      SDL_BroadcastCondition(cv);
    }
    SDL_UnlockMutex(mu);
  }

  void netLoop() {
    moveToHardwareThread(4);
    net::Response r; std::vector<uint8_t> extra;
    char path[256];
    std::snprintf(path, sizeof(path), "/api/stream/%s?t=%.2f&q=%d%s%s", id.c_str(), startAt, quality, allowOriginal ? "&codec=h264" : "", speakerRequested ? "&speaker=1" : "");
    int fd = net::connectTo(srv, 20000);
    SDL_LockMutex(mu); sock = fd; SDL_UnlockMutex(mu);
    if (stop || fd < 0 || !net::readGet(fd, srv, path, r, extra) || r.status != 200) {
      SDL_LockMutex(mu); sock = -1; SDL_UnlockMutex(mu);
      if (fd >= 0) ::close(fd);
      if (stop) return;
      std::string why = fd < 0 ? "" : net::header(r, "X-Error");  // motivo que o YouTube deu (membros, privado...)
      error = fd < 0 ? "Sem conexão com o servidor do notebook"
            : why == "members" ? "Vídeo só para membros do canal"
            : why == "private" ? "Vídeo privado"
            : why == "age" ? "Vídeo com restrição de idade"
            : why == "unavailable" ? "Vídeo indisponível no YouTube"
            : "O servidor não conseguiu abrir este vídeo";
      SDL_LockMutex(mu); netErr = true; SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
      return;
    }
    std::string d = net::header(r, "X-Duration");
    if (!d.empty() && duration <= 0) duration = std::atof(d.c_str());
    live = net::header(r, "X-Live") == "1";
    std::string tt = net::header(r, "X-Title");
    if (!tt.empty() && title.empty()) title = net::urlDecode(tt);
    std::string sid = net::header(r, "X-Stream-Id");
    if (!speakerRequested && net::header(r, "X-Nest-Handoff") != "1" && directSpeaker.sample().quiet) output = LOCAL;
    original = net::header(r, "X-Video-Codec") == "h264";
    session = sid.empty() ? -1 : std::atoi(sid.c_str());
    if (state == CONNECTING) state = BUFFERING;
    auto put = [&](const uint8_t* p, size_t n) { putBytes(p,n); };
    std::string encoding = net::header(r, "Transfer-Encoding");
    std::transform(encoding.begin(), encoding.end(), encoding.begin(), [](unsigned char c){ return std::tolower(c); });
    const bool chunked = encoding == "chunked";
    net::ChunkReader chunks;
    auto deliver = [&](const uint8_t* p, size_t n) {
      if (chunked) return chunks.feed(p, n, put);
      put(p, n); return true;
    };
    bool valid = deliver(extra.data(), extra.size());
    std::vector<uint8_t> buf(64 << 10);
    while (!stop && valid && !chunks.finished()) {
      ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && !stop) continue;  // pausa longa: o servidor só espera
      if (n <= 0) break;
      valid = deliver(buf.data(), (size_t)n);
    }
    if (original && !stop && (!valid || (chunked && !chunks.finished()))) retryConverted = true;
    SDL_LockMutex(mu); sock = -1; netEof = true; SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
    ::close(fd);
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

  void trimReserve() {
    // Descartar PCM realmente enfileirado, no formato do dispositivo, somente enquanto a reserva está muda.
    int trim = trimMs.exchange(-1);
    if (trim > 0 && outputFrameBytes && hasAudio) {
      uint8_t discarded[4096];
      SDL_LockAudioStream(audio);
      double queued = audioQueuedSec();
      int bytes = int(std::min(queued, std::max(0.0, trim / 1000.0 - tvClock(0, queued))) * outputBytesPerSec);
      while (bytes >= outputFrameBytes && !stop) {
        int n = std::min(bytes, (int)sizeof(discarded)) / outputFrameBytes * outputFrameBytes;
        int got = SDL_GetAudioStreamData(audio, discarded, n);
        if (got <= 0) break;
        bytes -= got;
      }
      SDL_UnlockAudioStream(audio);
    }
  }

  void decLoop() {
    moveToHardwareThread(2);
    if (direct) { decodeOriginal(); return; }
    SDL_LockMutex(mu);
    while (!rcount && !netEof && !netErr && !stop) SDL_WaitCondition(cv, mu);
    SDL_UnlockMutex(mu);
    if (stop) return;
    if (original) { decodeOriginal(); return; }
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
      double reserve = lastDecodedPts < 0 ? 0.12 : 0.50;
      int audioBudget = 24;
      if (live && lastDecodedPts >= 0 && !videoDone) {
        SDL_LockMutex(mu); bool needsPicture = frames.size() < 2; SDL_UnlockMutex(mu);
        if (needsPicture) audioBudget = audioQueuedSec() > 0.08 ? 0 : 2;
      }
      for (int k = 0; k < audioBudget && !audioDone && !stop && audioQueuedSec() < reserve; k++) {
        plm_samples_t* s = plm_decode_audio(plm);
        if (!s) { if (plm_has_ended(plm)) audioDone = true; break; }
        SDL_LockAudioStream(audio);
        SDL_PutAudioStreamData(audio, s->interleaved, (int)(s->count * 2 * sizeof(float)));
        SDL_AddAtomicInt(&pushed, (int)s->count);
        if (SDL_GetAtomicInt(&audioBaseMs) < 0) SDL_SetAtomicInt(&audioBaseMs, (int)std::lround(s->time * 1000));
        SDL_UnlockAudioStream(audio);
        did = true;
      }
      trimReserve();
      // atrasado em relação ao áudio: pula quadros B até alcançar
      double c = masterMs.load() / 1000.0;
      bool nearCue = output == PRIMING && std::fabs(lastDecodedPts - cueMs / 1000.0) < 0.4;
      plm_video_skip_b_frames = (!nearCue && c >= 0 && lastDecodedPts >= 0 && lastDecodedPts < c - (speakerRequested ? 0.12 : 0.04)) ? 1 : 0;
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
        LOG("decod: %d quadros (%.1f ms cada), %d B pulados, fila %d, anel %u KB (mín %u), rede %.1f Mbit/s, nest %d, perdidos %lld, ocioso %d, PCM %.0f ms",
                statFrames, statFrames ? statNs / 1e6 / statFrames : 0.0, plm_video_skipped_b_total - statSkip0, q, (unsigned)(ring_ >> 10),
                (unsigned)(rmin >> 10), mbit, speakerRequested ? st : -1, (long long)droppedFrames, statIdle, audioQueuedSec() * 1000);
        statAt = SDL_GetTicks(); statFrames = 0; statNs = 0; statIdle = 0; statSkip0 = plm_video_skipped_b_total;
      }
    }
    SDL_LockMutex(mu); decEnded = true; SDL_UnlockMutex(mu);
    plm_destroy(plm);
  }

  void enqueue(plm_frame_t* f) {
    const uint8_t* planes[] = {f->y.data, f->cb.data, f->cr.data};
    const int strides[] = {(int)f->y.width, (int)f->cb.width, (int)f->cr.width};
    enqueuePlanes(f->width, f->height, f->time, planes, strides, false, false);
  }

  void enqueuePlanes(int w, int h, double pts, const uint8_t* const* planes, const int* strides, bool bt709, bool full) {
    if (actualHeight != h) actualHeight = h;
    SDL_LockMutex(mu);
    Frame* fr = spare.empty() ? new Frame() : spare.back();
    if (!spare.empty()) spare.pop_back();
    SDL_UnlockMutex(mu);
    int cw = (w + 1) / 2, ch = (h + 1) / 2;
    fr->w = w; fr->h = h; fr->pts = pts; fr->bt709 = bt709; fr->fullRange = full;
    fr->yuv.resize((size_t)w * h + 2 * (size_t)cw * ch);
    uint8_t* dst = fr->yuv.data();
    for (int i = 0; i < 3; ++i) {
      int width = i ? cw : w, height = i ? ch : h;
      if (strides[i] == width) {
        size_t bytes = size_t(width) * height;
        std::memcpy(dst, planes[i], bytes); dst += bytes;
      } else {
        for (int y = 0; y < height; ++y, dst += width) std::memcpy(dst, planes[i] + y * strides[i], width);
      }
    }
    SDL_LockMutex(mu); frames.push_back(fr); SDL_UnlockMutex(mu);
  }

  static int readOriginal(void* user, uint8_t* dst, int size);
  bool putOriginalAudio(h264::Stream& s, int count, double pts);
  void decodeOriginal();
  void directNestLoop();

  void nestLoop() {
    moveToHardwareThread(5);
    if (direct) { directNestLoop(); return; }
    directSpeaker.release();
    while (!stop && !directSpeaker.sample().quiet) SDL_Delay(20);
    while (!stop && state == CONNECTING) SDL_Delay(15);
    if (stop || state == FAILED) return;
    std::string sid = session > 0 ? "&sid=" + std::to_string(session.load()) : "";
    std::string query = sid.empty() ? "" : "?" + sid.substr(1);
    if (!speakerRequested && output == LOCAL) return;
    int revision = 0, claim = 0;
    auto playAt = [&](double pos) {
      char path[180];
      std::snprintf(path, sizeof(path), "/api/nest/play?id=%s&t=%.3f&claim=%d%s", id.c_str(), pos, claim, sid.c_str());
      net::Response r;
      if (!net::get(srv, path, r, 1500, 1024)) { revision = 0; return false; }
      auto fields = net::split(std::string(r.body.begin(), r.body.end()), '\t');
      revision = fields.size() > 1 ? std::atoi(fields[1].c_str()) : 0;
      return fields[0].find("ok") == 0;
    };
    // O Cast não deve adotar um relógio adiantado enquanto o primeiro MPEG ainda está sendo decodificado.
    while (speakerRequested && (!pictureReady || SDL_GetTicks() - openedMs < (uint64_t)deviceMs || !deviceDrained()) &&
           !stop && state != FAILED && state != ENDED) SDL_Delay(10);
    if (stop || state == FAILED || state == ENDED) return;
    if (speakerRequested && !playAt(live ? 0 : startAt)) output = RELEASING;
    uint64_t requested = SDL_GetTicks(), cmdAt = 0, badSince = 0, localSince = 0;
    int expected = 0, last = 0;
    while (!stop) {
      uint64_t ms = SDL_GetTicks();
      if (output == RELEASING) {
        net::Response r;
        if (session > 0 && net::get(srv, "/api/nest/local" + query + "&rev=" + std::to_string(revision), r, 1000, 1024)) {
          auto f = net::split(std::string(r.body.begin(), r.body.end()), '\t');
          if (f.size() > 1 && f[0] == "QUIET") {
            claim = std::atoi(f[1].c_str());
            armed = mutedForCast = false; cueMs = -1;
            output = LOCAL; localSince = ms; badSince = 0; expected = last = 0;
            if (!speakerRequested) return;
          }
        }
        SDL_Delay(100); continue;
      }
      if (output == LOCAL) {
        if (!paused && localPlaying && ms - localSince > 1200 && speakerTargetMs >= 0) {
          // O MP3 da nova URL espera o primeiro GET antes de receber autorização para sair.
          cueMs = (int)std::lround(std::round((speakerTargetMs + 2000) * 0.030) * (1000.0 / 30));
          output = PRIMING; armed = mutedForCast = false;
          requested = ms;
          if (!playAt((live ? 0 : startAt) + (cueMs - wantedOffsetMs) / 1000.0)) output = RELEASING;
          last = expected = 0;
        }
        SDL_Delay(30); continue;
      }
      int cmd = nestCmd.exchange(0);
      if (cmd && output == CAST) {
        net::Response r;
        net::get(srv, (cmd == 1 ? "/api/nest/pause" : "/api/nest/resume") + query, r, 1000, 1024);
        expected = cmd == 1 ? 2 : 1; cmdAt = SDL_GetTicks();
      }
      double a = SDL_GetTicksNS() / 1e6;
      net::Response pr;
      bool ok = net::get(srv, "/api/nest/pos" + query, pr, 900, 1024);
      double b = SDL_GetTicksNS() / 1e6;
      int st = 0;
      double sample = 0;
      if (ok && !stop && b - a <= 120) {
        auto f = net::split(std::string(pr.body.begin(), pr.body.end()), '\t');
        st = f[0] == "PLAYING" ? 1 : f[0] == "PAUSED" ? 2 : f[0] == "IDLE" ? 3 : f[0] == "ERRO" ? 4 : f[0] == "BUFFERING" ? 5 : 0;
        sample = f.size() > 1 ? std::atof(f[1].c_str()) - (live ? 0 : startAt) : 0;
        SDL_LockMutex(nmu);
        nestState = st; nestPos = sample; nestStampMs = (a + b) / 2;
        SDL_UnlockMutex(nmu);
        if (output == PRIMING && f.size() > 3 && revision && std::atoi(f[3].c_str()) == revision) armed = true;
      }
      if (output == PRIMING) {
        if (mutedForCast && armed) {
          net::Response r;
          if (net::get(srv, "/api/nest/unmute" + query + "&rev=" + std::to_string(revision), r, 900, 1024)) {
            auto f = net::split(std::string(r.body.begin(), r.body.end()), '\t');
            if (f.size() < 2) { output = RELEASING; continue; }
            SDL_LockMutex(nmu);
            nestState = 1; nestPos = std::atof(f[1].c_str()) - (live ? 0 : startAt);
            nestStampMs = SDL_GetTicksNS() / 1e6;
            SDL_UnlockMutex(nmu);
            output = CAST; badSince = 0; last = 1;
          } else output = RELEASING;
        } else if (paused || ms - requested > 4000 || speakerTargetMs > cueMs + 40) output = RELEASING;
      } else {
        if (st == 1 || st == 2) {
          if (expected && expected == st) expected = 0;
          else if (expected && ms - cmdAt > 2500 && !nestCmd) nestCmd = expected == 2 ? 1 : 2;
          else if (!expected && !nestCmd && last && last != st) speakerPause = st == 2 ? 1 : 2;
          last = st;
        }
        bool healthy = st == 2 || (st == 1 && (speakerTargetMs < 0 ||
            std::fabs(sample - (speakerTargetMs - wantedOffsetMs) / 1000.0) < 0.50));
        if (healthy) badSince = 0;
        else if (!badSince) badSince = ms;
        else if (!paused && ms - badSince > (last ? 800u : 2800u)) {
          output = RELEASING; claim = 0;
        }
      }
      SDL_Delay(output == CAST ? 85 : 15);
    }
  }

  double tvClock(double wall, double queued) {
    int base = SDL_GetAtomicInt(&audioBaseMs);
    if (!hasAudio || base < 0) return wall;
    double played = SDL_GetAtomicInt(&pushed) / 44100.0 - queued - deviceMs / 1000.0;
    return base / 1000.0 + std::max(0.0, played);
  }

  void setPaused(bool p, bool notifySpeaker = true) {
    if (p == paused) return;
    paused = p; ++pauseChanges;
    if (speakerRequested && notifySpeaker) nestCmd = p ? 1 : 2;
    if (audio) {
      if (p) SDL_PauseAudioStreamDevice(audio);
      else if (firstPts >= 0 && !refill && !reservePaused) SDL_ResumeAudioStreamDevice(audio);
    }
    if (p) pausedAt = SDL_GetTicks();
    else if (pausedAt) {
      lastFrameMs = SDL_GetTicks();
      if (wallStart) pausedTotal += SDL_GetTicks() - std::max(wallStart, pausedAt);
      pausedAt = 0; tickMs = SDL_GetTicksNS() / 1e6;
    }
  }

  Frame* take() {
    int external = speakerPause.exchange(0);
    if (external) setPaused(external == 1, false);
    double now = SDL_GetTicksNS() / 1e6;
    uint64_t ms = SDL_GetTicks();
    wantedOffsetMs = (int)std::lround(nestOffset * 1000);
    if (refreshed && shownFrames > 150) refreshed = false;
    if (direct && live && net::serverEnabled && !paused && (state == BUFFERING || state == PLAYING)) {
      if (!originalWatch) originalWatch = ms;
      if (ms - std::max(originalWatch,lastFrameMs) > 15000) retryConverted = true;
    }
    if (direct && !paused && (renew || retryConverted)) {
      bool again = renew && !refreshed;
      if (again || net::serverEnabled) {
        LOG("console: %s em %.3f s", again ? "renovando URL" : "reserva no servidor", position());
        start(srv, id, position(), title, duration, quality, speakerRequested, again, again);
      } else { if (audio) SDL_PauseAudioStreamDevice(audio); error = "console"; state = FAILED; retryConverted = renew = false; }
      return nullptr;
    }
    if (original && !direct && !paused && (state == BUFFERING || state == PLAYING)) {
      if (!originalWatch) originalWatch = ms;
      if (retryConverted || ms - std::max(originalWatch, lastFrameMs) > 6500) {
        LOG("decod: H.264 -> MPEG-1 em %.3f s", position());
        allowOriginal = false;
        start(srv, id, position(), title, duration, quality, speakerRequested, false);
        return nullptr;
      }
    } else if (!direct || !live || paused) originalWatch = 0;
    bool starved = false;
    if (!direct && live && !paused && lastFrameMs && ms - lastFrameMs > 1100 &&
        (uint32_t)ms - lastPacketMs.load() > (speakerRequested ? 3000u : 1350u) &&
        output != PRIMING && (!lastLiveOpen || ms - lastLiveOpen > 20000)) {
      SDL_LockMutex(mu);
      starved = !decEnded && rcount == 0 && audioQueuedSec() < 0.05;
      SDL_UnlockMutex(mu);
    }
    if (starved) {
      lastLiveOpen = ms;
      LOG("live: reabrindo na borda, sem quadro há %.3f s", (ms - lastFrameMs) / 1000.0);
      start(srv, id, 0, title, 0, quality, speakerRequested);
      return nullptr;
    }
    double speaker = -1;
    Output route = output.load();
    if (speakerRequested && route == CAST) {
      SDL_LockMutex(nmu);
      if (nestState == 1 && now - nestStampMs < (direct ? 8000 : 650)) speaker = nestPos + (now - nestStampMs) / 1000.0;
      SDL_UnlockMutex(nmu);
    }
    SDL_LockMutex(mu);
    Frame* show = nullptr;
    if (firstPts < 0 && !paused && !frames.empty() && (frames.size() >= (original || (speakerRequested && live) ? 6u : 2u) || decEnded) &&
        (!hasAudio || audioQueuedSec() >= (speakerRequested && live ? 0.20 : 0.10) || decEnded)) {
      pictureReady = true;
      if (!readyMs) readyMs = ms;
      // Espera inicial limitada. Se o DMR demorar, a TV já tem o áudio deste mesmo stream.
      if (!speakerRequested || speaker >= 0 || ms - readyMs >= 1400) {
        firstPts = frames.front()->pts; wallStart = ms;
        screenSec = speaker >= 0 ? std::max(firstPts, speaker + nestOffset) : firstPts;
        tickMs = now;
        if (audio) SDL_ResumeAudioStreamDevice(audio);
      }
    }
    if (firstPts >= 0 && !paused) {
      double wall = firstPts + (ms - wallStart - pausedTotal) / 1000.0;
      if (audio) SDL_LockAudioStream(audio);
      double queued = audioQueuedSec(), tv = tvClock(wall, queued);
      if (audio) SDL_UnlockAudioStream(audio);
      double dt = std::max(0.0, (now - tickMs) / 1000.0);
      double predicted = screenSec + dt;
      // O último PCM já foi entregue ao dispositivo. Apresenta também a cauda do vídeo até o último PTS.
      if (original && decEnded && queued <= 0) tv = std::max(tv, predicted);
      bool follow = speaker >= 0 && route == CAST;
      bool local = route == LOCAL || route == PRIMING;
      double cue = cueMs / 1000.0;
      if (route == PRIMING && (mutedAt || (audible ? tv : predicted) >= cue - deviceMs / 1000.0)) {
        local = false;
        if (!mutedAt) mutedAt = ms;
      } else if (route != PRIMING) mutedAt = 0;
      if (hasAudio && !decEnded && (!speakerRequested || audible) && !follow && !refill && queued <= 0 && ms - wallStart > 100) {
        SDL_PauseAudioStreamDevice(audio); refill = true;
        LOG("som: fila PCM vazia em %.3f s", screenSec);
      } else if (refill && (follow || queued >= 0.10 || decEnded)) {
        if (!reservePaused) SDL_ResumeAudioStreamDevice(audio);
        refill = false;
      }
      double target = follow ? speaker + nestOffset : audible ? tv : predicted;
      // Sem saltos na troca de relógio. Na TV sozinha o PCM efetivamente consumido continua sendo o mestre.
      if (!refill)
        screenSec = speakerRequested && !audible ? predicted + std::clamp(target - predicted, -dt * 0.15, dt * 0.15) : tv;
      if (route == PRIMING && cue >= 0)
        screenSec = std::min(screenSec, std::max(cue, predicted - dt));
      if (follow && mutedForCast.exchange(false)) screenSec = std::max(screenSec, speaker + nestOffset);
      tickMs = now; masterMs = (int)std::lround(screenSec * 1000);
      speakerTargetMs = (int)std::lround(screenSec * 1000);
      bool due = !direct || !live || now >= nextLiveFrame;
      while (due && !frames.empty() && frames.front()->pts <= screenSec + 0.010) {
        if (show) { spare.push_back(show); droppedFrames++; }
        show = frames.front(); frames.pop_front();
      }
      if (show && direct && live) {
        constexpr double period = 1000.0 / 30;
        nextLiveFrame = (now - nextLiveFrame >= period ? now : nextLiveFrame) + period;
      }
      if (original) {
        queueMin = std::min(queueMin, (int)frames.size());
        if (show) SDL_BroadcastCondition(cv);
      }
      double display = show ? show->pts : lastPos - (live ? 0 : startAt);
      bool aligned = std::fabs(tv - display) <= 0.022 && (queued >= 0.10 || decEnded);
      bool on = local && (!hasAudio || audible || aligned);
      if (route == PRIMING && armed && mutedAt && ms - mutedAt >= (uint64_t)deviceMs && deviceDrained() &&
          std::fabs(lastPos - (live ? 0 : startAt) - cue) < 0.020 && std::fabs(display - cue) < 0.020 && (decEnded || (!frames.empty() &&
          frames.back()->pts - display >= 0.15 && queued >= 0.18))) mutedForCast = true;
      if (on != audible || follow != tracking) {
        LOG("som: %s, Nest %.3f TV %.3f imagem %.3f", on ? "TV" : follow ? "Nest" : "aguardando", speaker, tv, display);
        tracking = follow;
      }
      tvSound(on); localPlaying = audible.load(); nest = follow;
      if (speakerRequested && hasAudio) {
        if (!on && display - tv > 0.025) trimMs = (int)std::lround(display * 1000);
        bool hold = !on && tv - display > (reservePaused ? 0.010 : 0.035);
        if (hold != reservePaused) {
          reservePaused = hold;
          if (hold) SDL_PauseAudioStreamDevice(audio);
          else if (!refill) SDL_ResumeAudioStreamDevice(audio);
        }
      }
      if (ms >= tuneAt && audio && hasAudio) {
        tuneAt = ms + 120;
        // O PCM mudo acompanha o Nest. Depois de um underrun na live, a TV recupera até 3% por vez.
        double ratio = speakerRequested && !on ? 1.0 + std::clamp((display - tv) * 1.2, -0.35, 0.08)
                              : 1.0 + (live && liveDebt > 0.15 ? 0.03 : 0.0);
        SDL_SetAudioStreamFrequencyRatio(audio, (float)ratio);
      }
      if (show) {
        if (!shownFrames) LOG("primeiro quadro: %llu ms, pts %.3f, live %d, som %s", (unsigned long long)(ms - openedMs),
                              show->pts, (int)live.load(), follow ? "Nest" : "TV");
        lastFrameMs = ms; shownFrames++; if (state == BUFFERING || state == CONNECTING) state = PLAYING;
        lastPos = (live ? 0 : startAt) + show->pts;
      }
      if (live) liveDebt = std::max(0.0, wall - lastPos);
    }
    if (speakerRequested && audio && !hasAudio && firstPts >= 0 && speaker < 0 && ms - readyMs > 3500) {
      nestFailed = true; state = FAILED;  // servidor antigo sem MP2 de reserva
    }
    bool drained = decEnded && frames.empty();
    SDL_UnlockMutex(mu);
    if (drained && !retryConverted && state != FAILED && (!hasAudio || audioQueuedSec() < 0.05)) state = ENDED;
    return show;
  }
  void recycle(Frame* f) { SDL_LockMutex(mu); spare.push_back(f); SDL_UnlockMutex(mu); }

  double position() const { return lastPos; }
  bool waiting() const { return state == CONNECTING || state == BUFFERING; }
};

}  // namespace player

#include "player_h264.hpp"
#include "player_nest.hpp"
