// Player: abre o vídeo direto no YouTube (VOD por faixas, HLS ou segmentos ao vivo) e entrega quadros YUV e áudio.
// Três threads: decodificação (enche a fila de quadros e o áudio do SDL), Nest e a principal (escolhe o quadro pelo
// relógio do áudio). No 360 cada thread pesada vai para um núcleo próprio: o xsdk cria toda thread no núcleo de quem a
// criou (xsdk/thread.h).
#pragma once
#include <SDL3/SDL.h>

#include <atomic>
#include <cmath>
#include "player_volume.hpp"
#include <deque>
#include <string>
#include <vector>

#include "h264.hpp"
#include "decode_budget.hpp"
#include "playback_mode.hpp"
#include "quality_recovery.hpp"
#include "yt/media.hpp"
#include "yt/console_account.hpp"
#include "nest.hpp"
#include "audio_tempo.hpp"
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

struct Frame { double pts = 0; int w = 0, h = 0, sourceFps = 0; bool bt709 = false, fullRange = false; std::vector<uint8_t> yuv; std::shared_ptr<video::Surface> surface; };

enum State { IDLE, CONNECTING, BUFFERING, PLAYING, ENDED, FAILED };

#if defined(TUBE360_F40_BENCH) || defined(TUBE360_F33_BENCH)
// Só nas builds de bancada: mudo.txt ao lado do xex zera o volume mantendo o relógio do áudio (teste de madrugada).
inline bool benchMute = false;
#endif
struct Player {
  nest::Speaker directSpeaker;
  std::atomic<bool> castMediaReady{false};
  std::string id, title, error;
  double startAt = 0, duration = 0;
  std::atomic<double> speed{1};      // velocidade ESCOLHIDA pela pessoa (painel de velocidade); a live fica em 1
  // Alcance/folga da live (0,95x a 1,05x, WSOLA): do app, não da pessoa. Fica fora de `speed` para não acender o ícone de velocidade
  // nem bloquear o Nest; os relógios usam rate() = speed * liveRate.
  std::atomic<double> liveRate{1};
  double liveClockOffset = 0;        // reancoragem do relógio do áudio na troca de liveRate (pode ser negativa; não usa audioBaseMs)
  double rate() const { return speed.load() * liveRate.load(); }
  bool loop = false;
  audio::Tempo tempo;
  int quality = 720;
  int videoMode = playback::AUTO, cachedVideoMode = -1;
  std::string audioSelection = "original", cachedAudio;
  std::vector<yt::AudioTrack> audioTracks;
  std::string selectedAudio;
  std::atomic<bool> tracksReady{false}, adapted{false};
  std::atomic<unsigned> qualityAvailable{1};
  std::atomic<bool> qualityModesReady{false},manualQualityWarning{false};
  std::atomic<int> presentFps{30};
  std::atomic<int> state{IDLE};
  std::atomic<bool> stop{false}, paused{false}, live{false};  // live: sem duração nem busca

  // anel do leitor da live (HLS) -> decodificação
  SDL_Mutex* mu = nullptr; SDL_Condition* cv = nullptr;
  std::vector<uint8_t> ring; size_t rhead = 0, rcount = 0; bool netEof = false, netErr = false;
  SDL_Thread *decThread = nullptr;

  // fila de quadros (protegida por mu)
  std::deque<Frame*> frames; std::vector<Frame*> spare; int maxFrames = 10, startupFrames = 6;
  bool decEnded = false;
  std::atomic<bool> decDone{false};        // espelho de decEnded para o vigia (lido sem o mutex)
  std::atomic<uint64_t> framesIn{0};       // quadros decodificados entregues à fila, monotônico (vigia do player)
  std::atomic<bool> consoleFailure{false};  // as opções diretas acabaram: o vídeo para com a mensagem do console
  bool refreshed = false;
  bool sameUrlRetry = false;  // vídeo de membros: o 403 foi passageiro, a renovação reaproveita a mesma URL
  std::string cacheBase, requestLanguage = "en";
  std::shared_ptr<yt::ConsoleAccount> memberAccount;
  bool memberVideo = false;
  // `client`: o cliente de quem chama (a Música tem o próprio e nenhum vídeo tocou antes); sem ele, o do vídeo.
  std::string memberResponse(yt::Client* client = nullptr) {
    if (!memberVideo || !memberAccount || !memberAccount->active()) return {};
    // O TVHTML5 só entrega links tocáveis com o signatureTimestamp do player TV (o mesmo programa que resolve o `n`).
    std::string path = "/api/video/" + id + "/player?lang=" + requestLanguage;
    if (!client) client = youtube.get();
    if (client) if (int sts = client->nsigTimestamp()) path += "&sts=" + std::to_string(sts);
    auto reply = memberAccount->request(path);
    return reply.ok() ? std::move(reply.body) : std::string{};
  }
  std::unique_ptr<yt::Transport> transport;
  std::unique_ptr<yt::Client> youtube;
  yt::Video media;
  uint64_t validUntil = 0;
  int cachedQuality = 0;
  std::atomic<bool> mediaReady{false}, renew{false};
  std::atomic<int> actualHeight{0};
  std::atomic<int> actualFps{0};
  int videoMetrics = 0;
  video::Allocator* videoBuffers = nullptr;
  int h264Threads = 4, h264ThreadType = FF_THREAD_FRAME, h264LoopFilter = -1;
  bool h264FullFilter = false;
  std::atomic<int> activeH264Threads{0}, activeLoopFilter{0};
  std::atomic<int> activeVideoItag{0};
  std::atomic<bool> activeH264Spread{false};
  double observed1080Ms = 0;
  int queueMin = 0;
  std::atomic<unsigned> pauseChanges{0};
  playback::QualityRecovery qualityRecovery;
  void resetQualityMeasurements(){++pauseChanges;}

  // áudio
  SDL_AudioStream* audio = nullptr; std::atomic<bool> hasAudio{false};
  float normalGain = 1, fadeGain = 1;
  // Volume escolhido na interface (0..1), por cima do fade do sono e do volume do celular. Gravado em settings.txt (`volume`, 0 a 100).
  float userVolume = 1;
  std::atomic<bool> audible{true};
  uint64_t tvMutedSince = 0;
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
    if (audio && audible) SDL_SetAudioStreamGain(audio, normalGain * gain * userVolume);
  }
  // Dois volumes separados. TV: ganho relativo do app (`settings.volume`, 0..100, passos de 5), aplicado no stream de áudio.
  // Nest: o nível real da caixa, ajustado ao vivo a partir do que ela informa; nunca é gravado e nunca é enviado na abertura do vídeo.
  int volumePercent() const { return (int)std::lround(userVolume * 100); }
  int setVolumePercent(int percent) {  // só TV
    percent = volume::clampPercent(percent);
    userVolume = percent / 100.f;
    if (audio && audible) SDL_SetAudioStreamGain(audio, normalGain * fadeGain * userVolume);
    return percent;
  }
  int stepVolume(int direction) { return setVolumePercent(volume::stepped(volumePercent(), direction)); }
  // Passo na caixa (Nest) a partir do volume real dela; devolve o nível novo em % ou -1 se a caixa não informou o volume.
  int stepSpeaker(int direction) {
    auto s = directSpeaker.sample();
    double known = s.actual >= 0 ? s.actual : s.volume;  // o que a caixa informou no último status vence o último pedido
    if (!nest || !directSpeaker.configured() || known < 0) return -1;
    int base = (int)std::lround(known * 100), next = volume::stepped(base, direction);
    if (next != base) directSpeaker.volume(next / 100.0, next == 0);
    return next;
  }
  void tvSound(bool on) {
    if (audible == on) return;
    if (audio && !SDL_SetAudioStreamGain(audio, on ? normalGain * fadeGain * userVolume : 0.0f)) return;
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
  double firstPts = -1; uint64_t wallStart = 0, pausedAt = 0, pausedTotal = 0;
  double lastPos = 0;
  int64_t droppedFrames = 0, shownFrames = 0;

  // A reserva PCM e a posse da saída são independentes do último relógio recebido do Nest.
  enum Output { CAST, RELEASING, LOCAL, PRIMING };
  std::atomic<Output> output{LOCAL};
  std::atomic<int> trimMs{-1}, cueMs{-1}, wantedOffsetMs{0};
  std::atomic<bool> armed{false}, mutedForCast{false}, localPlaying{false}, pictureReady{false};
  uint64_t mutedAt = 0, lastFrameMs = 0;
  int deviceMs = 40, outputBytesPerSec = 0, outputFrameBytes = 0;
  double clockStepMs=24, tvSample=-1, tvSampleAt=0;
  bool nest = false, speakerRequested = false; double nestOffset = 0;
  SDL_Thread* nestThread = nullptr; SDL_Mutex* nmu = nullptr;
  int nestState = 0;  // 0 aguardando/obsoleto, 1 tocando, 2 pausado, 3 parado, 4 erro, 5 bufferizando, 6 sem progresso
  bool nestClockReady = true;
  double nestPos = 0, nestStampMs = 0, nestSeenMs = 0;
  std::atomic<int> nestCmd{0}, masterMs{-1}, speakerTargetMs{-1};
  std::atomic<bool> nestFailed{false};
  std::atomic<bool> speakerFallback{false};
  std::atomic<bool> speakerLiveOnTv{false};   // pediu a caixa numa live: a live toca na TV (o Nest não recebe live), sem culpar a caixa
  std::atomic<int> speakerPause{0};  // mudança feita no alto-falante, aplicada pela thread da tela
  uint64_t openedMs = 0, readyMs = 0, tuneAt = 0;
  double screenSec = -1, tickMs = 0, liveDebt = 0, nextLiveFrame = 0;
  bool liveLowLatency = false;          // opção só da TV (settings.liveLowLatency), lida por main.cpp antes de start()
  bool tracking = false, refill = false, reservePaused = false;

  void init() {
    mu = SDL_CreateMutex(); cv = SDL_CreateCondition(); nmu = SDL_CreateMutex();
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 44100};
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!audio) SDL_Log("áudio: %s", SDL_GetError());
    else {
      normalGain = SDL_GetAudioStreamGain(audio);
#if defined(TUBE360_F40_BENCH) || defined(TUBE360_F33_BENCH)
      if (benchMute) { normalGain = 0; SDL_SetAudioStreamGain(audio, 0.0f); }
#endif
      SDL_AudioSpec device{};
      int samples = 0;
      if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(audio), &device, &samples) && device.freq > 0) {
        deviceMs = std::max(1, (samples * 1000 + device.freq - 1) / device.freq);
        clockStepMs=std::max(1.0,samples*1000.0/device.freq);
#ifdef __XSDK__
        // SDL informa 256 amostras por chamada; o driver xsdk abre a fila com a profundidade padrão.
        deviceMs = (XSDK_AUDIO_FRAMES_AHEAD_DEFAULT * XSDK_AUDIO_FRAME_SAMPLES * 1000 + XSDK_AUDIO_RATE_HZ / 2) / XSDK_AUDIO_RATE_HZ;
#endif
        outputFrameBytes = SDL_AUDIO_FRAMESIZE(device);
        outputBytesPerSec = device.freq * outputFrameBytes;
      }
    }
  }

  static int decMain(void* p) { ((Player*)p)->decodeOriginal(); return 0; }
  static int nestMain(void* p) { moveToHardwareThread(5); ((Player*)p)->directNestLoop(); return 0; }
  void directFailed();
  // A caixa pedida sem Nest configurado toca na TV; `speakerMissing` deixa quem chama mostrar o aviso (K::NoSpeaker).
  std::atomic<bool> speakerMissing{false};
  bool resolveSpeaker(bool requested) {
    speakerMissing = requested && !directSpeaker.configured();
    return requested && !speakerMissing;
  }

  void start(const std::string& vid, double t, const std::string& ttl, double dur, int q, bool speaker, bool renewing = false, int mode = -1, const std::string& track = {}, bool members = false) {
    // Um restart interno pode já ter chamado halt; IDLE não apaga o custo deste vídeo.
    bool newQualitySession=vid!=id || quality!=q || (mode>=0 && mode!=videoMode);
    if (vid != id) { speed=1; loop=false; }
    speaker = resolveSpeaker(speaker && speed == 1);
    halt(!speaker);
    if(newQualitySession) observed1080Ms=0;
    if(newQualitySession)qualityRecovery=playback::QualityRecovery(SDL_GetTicks());
    memberVideo = members || (vid == id && memberVideo);
    tempo.reset(speed);
    if (mode >= 0) videoMode=mode;
    if (!track.empty()) audioSelection=track;
    else if (vid != id) audioSelection="original";
    audioTracks.clear(); selectedAudio.clear(); tracksReady=false; adapted=false;
    presentFps=playback::rate(videoMode);
    qualityAvailable=1;qualityModesReady=false;manualQualityWarning=false;
    castMediaReady = false;
    endLiveReader = false; refreshed = renewing; renew = mediaReady = false; actualHeight = actualFps = 0;
    if (renewing && !sameUrlRetry) validUntil = 0;
    sameUrlRetry = false;
    SDL_SetAtomicInt(&netBytes, 0);
    consoleFailure = false; maxFrames = 10; startupFrames = 6; queueMin = maxFrames;
    speakerRequested = speaker; nest = false;
    id = vid; startAt = std::max(0.0, t); title = ttl; duration = dur; quality = q; error.clear();
    ring.assign(6u << 20, 0); rhead = rcount = 0; netEof = netErr = false; decEnded = false; decDone = false;
    SDL_SetAtomicInt(&audioBaseMs, -1); SDL_SetAtomicInt(&pushed, 0); liveRate = 1; liveClockOffset = 0; firstPts = -1; wallStart = 0; pausedAt = 0; pausedTotal = 0; lastPos = startAt;
    droppedFrames = shownFrames = 0;
    activeH264Threads=0;activeLoopFilter=0;activeH264Spread=false;activeVideoItag=0;
    tvSample=-1;tvSampleAt=0;
    stop = false; paused = false; live = false; state = CONNECTING;
    hasAudio = false; readyMs = tuneAt = 0; screenSec = -1; liveDebt = 0; tickMs = nextLiveFrame = 0;
    openedMs = SDL_GetTicks(); masterMs = -1; tracking = refill = reservePaused = false; speakerPause = 0; speakerTargetMs = -1;
    if (audio) { SDL_PauseAudioStreamDevice(audio); SDL_ClearAudioStream(audio); SDL_SetAudioStreamFrequencyRatio(audio, 1.0f); }
    output = speaker ? CAST : RELEASING;
    trimMs = cueMs = -1; armed = mutedForCast = localPlaying = pictureReady = false;
    mutedAt = lastFrameMs = 0; tvMutedSince = 0;
    tvSound(false);
    nestFailed = false;
    speakerFallback = false; speakerLiveOnTv = false;
    SDL_LockMutex(nmu); nestState = 0; nestClockReady = false; nestPos = 0; nestStampMs = nestSeenMs = 0; SDL_UnlockMutex(nmu); nestCmd = 0;
    output = speaker || !directSpeaker.sample().quiet ? CAST : LOCAL;
    decThread = yt::worker(decMain, "youtube", this);
    if (!decThread) state = FAILED;
    nestThread = yt::worker(nestMain, "nest", this);
  }

  void halt(bool stopNest = true) {
    stop = true;
    if (stopNest) directSpeaker.release();
    if (!decThread) return;
    uint64_t began = SDL_GetTicks();
    LOG("player: encerrando %s", id.c_str());
    endLiveReader = true;
    tvSound(false);
    if (audio) SDL_PauseAudioStreamDevice(audio);
    SDL_LockMutex(mu); SDL_BroadcastCondition(cv);
    SDL_UnlockMutex(mu);
    SDL_WaitThread(decThread, nullptr);
    if (nestThread) SDL_WaitThread(nestThread, nullptr);
    decThread = nestThread = nullptr;
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

  double audioQueuedSec() { return audio ? SDL_GetAudioStreamQueued(audio) / (44100.0 * 2 * 4) : 0; }

  bool putAudio(const float* pcm, int count, double pts) {
    if (!audio || count < 0 || !std::isfinite(pts)) return false;
    if (!count) return true;
    SDL_LockAudioStream(audio);
    if (SDL_GetAtomicInt(&audioBaseMs)<0) SDL_SetAtomicInt(&audioBaseMs,int(std::lround(pts*1000)));
    auto sink=[&](const float* data, int frames) {
      if (!SDL_PutAudioStreamData(audio,data,frames*2*sizeof(float))) return false;
      SDL_AddAtomicInt(&pushed,frames); return true;
    };
    bool ok=rate()==1 ? sink(pcm,count) : tempo.push(pcm,count,sink);
    SDL_UnlockAudioStream(audio); return ok;
  }
  bool finishAudio() {
    if (!audio || rate()==1) return true;
    SDL_LockAudioStream(audio);
    bool ok=tempo.push(nullptr,0,[&](const float* pcm,int frames) {
      if (!SDL_PutAudioStreamData(audio,pcm,frames*2*sizeof(float))) return false;
      SDL_AddAtomicInt(&pushed,frames); return true;
    },true);
    SDL_UnlockAudioStream(audio); return ok;
  }

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

  void enqueuePlanes(int w, int h, double pts, const uint8_t* const* planes, const int* strides, bool bt709, bool full, int fps = 0,
                     std::shared_ptr<video::Surface> surface = {}) {
    SDL_LockMutex(mu);
    Frame* fr = spare.empty() ? new Frame() : spare.back();
    if (!spare.empty()) spare.pop_back();
    SDL_UnlockMutex(mu);
    int cw = (w + 1) / 2, ch = (h + 1) / 2;
    fr->w = w; fr->h = h; fr->pts = pts; fr->bt709 = bt709; fr->fullRange = full; fr->sourceFps = fps;
    fr->surface = std::move(surface);
    if (fr->surface) { SDL_LockMutex(mu); frames.push_back(fr); ++framesIn; SDL_UnlockMutex(mu); return; }
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
    SDL_LockMutex(mu); frames.push_back(fr); ++framesIn; SDL_UnlockMutex(mu);
  }

  static int readOriginal(void* user, uint8_t* dst, int size);
  bool putOriginalAudio(h264::Stream& s, int count, double pts);
  void decodeOriginal();
  void directNestLoop();

  double tvClock(double wall, double queued) {
    int base = SDL_GetAtomicInt(&audioBaseMs);
    if (!hasAudio || base < 0) return wall;
    double played = SDL_GetAtomicInt(&pushed) / 44100.0 - queued - deviceMs / 1000.0;
    return base / 1000.0 + std::max(0.0, played) * rate() + liveClockOffset;
  }

  // Live: muda o ritmo (0,95x a 1,05x) SEM reabrir, pelo WSOLA do app (tom preservado). O relógio não pode saltar: guarda o
  // relógio do áudio no instante da troca e reancora os de parede. A âncora é própria (`liveClockOffset`): o `audioBaseMs` tem o
  // valor -1 como "sem base" e não serve para guardar um relógio que fica negativo.
  void setLiveSpeed(double next) {
    const double old = rate();
    if (!live || speed.load() != 1 || next == liveRate.load()) return;
    SDL_LockMutex(mu);
    if (audio) SDL_LockAudioStream(audio);
    const int base = SDL_GetAtomicInt(&audioBaseMs);
    if (base >= 0 && hasAudio) {
      const double played = std::max(0.0, SDL_GetAtomicInt(&pushed) / 44100.0 - audioQueuedSec() - deviceMs / 1000.0);
      const double clock = base / 1000.0 + played * old + liveClockOffset;
      liveClockOffset = clock - (base / 1000.0 + played * next);
    }
    if (wallStart) {
      const uint64_t ms = SDL_GetTicks();
      const double elapsed = double(ms - wallStart - pausedTotal);
      wallStart = ms - pausedTotal - uint64_t(std::max(0.0, elapsed * old / next));
    }
    liveRate = next;
    tempo.reset(next);
    if (audio) SDL_UnlockAudioStream(audio);
    SDL_UnlockMutex(mu);
  }

  void setPaused(bool p, bool notifySpeaker = true) {
    if (p == paused) return;
    paused = p; ++pauseChanges;
    tvSample=-1;tvSampleAt=0;
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
    if (!paused && (renew || consoleFailure)) {
      if (renew && !refreshed) {
        LOG("console: renovando URL em %.3f s", position());
        start(id, position(), title, duration, quality, speakerRequested, true);
      } else { if (audio) SDL_PauseAudioStreamDevice(audio); error = "console"; state = FAILED; consoleFailure = renew = false; }
      return nullptr;
    }
    double speaker = -1;
    Output route = output.load();
    if (speakerRequested && route == CAST) {
      SDL_LockMutex(nmu);
      if (nestClockReady && nestStampMs > 0 && (nestState == 1 || (paused && nestState == 2)) && now - nestSeenMs < 8000)
        speaker = nestPos + (!paused && nestState == 1 ? (now - nestStampMs) / 1000.0 : 0);
      SDL_UnlockMutex(nmu);
    }
    SDL_LockMutex(mu);
    Frame* show = nullptr;
    if (firstPts < 0 && !frames.empty() && (frames.size() >= size_t(startupFrames) || decEnded) &&
        (!hasAudio || audioQueuedSec() >= (speakerRequested && live ? 0.20 : 0.10) || decEnded)) {
      pictureReady = true;
      if (!readyMs) readyMs = ms;
      // Espera inicial limitada. Se o DMR demorar, a TV já tem o áudio deste mesmo stream.
      if (!speakerRequested || speaker >= 0 || (route != CAST && ms - readyMs >= 1400)) {
        firstPts = frames.front()->pts; wallStart = ms;
        screenSec = speaker >= 0 ? std::max(firstPts, speaker + nestOffset) : firstPts;
        tickMs = now;
        if (paused) {
          show=frames.front(); frames.pop_front();
          nest=tracking=speakerRequested && speaker>=0;
          SDL_BroadcastCondition(cv);
        }
        else if (audio) SDL_ResumeAudioStreamDevice(audio);
      }
    }
    if (firstPts >= 0 && !paused) {
      double wall = firstPts + (ms - wallStart - pausedTotal) / 1000.0 * rate();
      if (audio) SDL_LockAudioStream(audio);
      double queued = audioQueuedSec(), tv = tvClock(wall, queued);
      if (audio) SDL_UnlockAudioStream(audio);
      // SDL entrega o consumo em blocos. Entre callbacks, o mesmo bloco continua saindo no dispositivo.
      if (!speakerRequested && hasAudio && queued>0 && !refill && tv>firstPts+.0001) {
        if (tvSampleAt<=0 || std::fabs(tv-tvSample)>.000001) { tvSample=tv;tvSampleAt=now; }
        tv+=std::min(std::max(0.0,now-tvSampleAt),clockStepMs)/1000.0 * rate();
      }
      double dt = std::max(0.0, (now - tickMs) / 1000.0);
      double predicted = screenSec + dt * rate();
      // O último PCM já foi entregue ao dispositivo. Apresenta também a cauda do vídeo até o último PTS.
      if (decEnded && queued <= 0) tv = std::max(tv, predicted);
      bool follow = speaker >= 0 && route == CAST;
      bool local = route == LOCAL || route == PRIMING;
      double cue = cueMs / 1000.0;
      if (route == PRIMING && armed && (mutedAt || (audible ? tv : predicted) >= cue - deviceMs / 1000.0)) {
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
      // 1% corrige até 10 ms/s: 29,7–30,3 quadros/s numa fonte 30 fps, sem acelerar/frear 15% pelo Wi-Fi.
      // Na TV sozinha o PCM efetivamente consumido continua sendo o mestre.
      if (!refill)
        screenSec = speakerRequested && !audible ? predicted + std::clamp(target - predicted, -dt * 0.01, dt * 0.01) : tv;
      // Só segura a imagem para drenar o dispositivo local; uma confirmação Cast nunca prende o vídeo.
      if (route == PRIMING && armed && !mutedForCast && mutedAt && ms-mutedAt <= (uint64_t)deviceMs+120 && cue >= 0)
        screenSec = std::min(screenSec, std::max(cue, predicted - dt));
      tickMs = now; masterMs = (int)std::lround(screenSec * 1000);
      speakerTargetMs = (int)std::lround(screenSec * 1000);
      while (!frames.empty() && frames.front()->pts <= screenSec + 0.010) {
        if (show) { show->surface.reset(); spare.push_back(show); droppedFrames++; }
        show = frames.front(); frames.pop_front();
      }
      queueMin = std::min(queueMin, (int)frames.size());
      if (show) SDL_BroadcastCondition(cv);
      double display = show ? show->pts : lastPos - (live ? 0 : startAt);
      bool aligned = std::fabs(tv - display) <= 0.022 && (queued >= 0.10 || decEnded);
      bool on = local && (!hasAudio || audible || aligned);
      // Rede de segurança: só na TV, com PCM na fila, o som nunca fica mudo mais de 1,5 s esperando alinhar.
      if (!on && local && hasAudio && !speakerRequested && route == LOCAL && queued >= 0.10) {
        if (!tvMutedSince) tvMutedSince = ms;
        else if (ms - tvMutedSince > 1500) {
          LOG("som: TV forçada após %llu ms mudo, TV %.3f imagem %.3f", (unsigned long long)(ms - tvMutedSince), tv, display);
          on = true;
        }
      }
      if (on) tvMutedSince = 0;
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
      if (live) liveDebt = std::max(0.0, wall - lastPos);
    }
    if (show) {
      actualHeight = show->h; actualFps = show->sourceFps;
      if (!shownFrames) LOG("primeiro quadro: %llu ms, pts %.3f, live %d, som %s", (unsigned long long)(ms - openedMs),
                            show->pts, (int)live.load(), tracking ? "Nest" : "TV");
      lastFrameMs = ms; shownFrames++; if (state == BUFFERING || state == CONNECTING) state = PLAYING;
      lastPos = (live ? 0 : startAt) + show->pts;
    }
    if (speakerRequested && audio && !hasAudio && firstPts >= 0 && speaker < 0 && ms - readyMs > 3500) {
      nestFailed = true; state = FAILED;  // sem o relógio da caixa e sem PCM local: a TV assume
    }
    bool drained = decEnded && frames.empty();
    SDL_UnlockMutex(mu);
    if (drained && !consoleFailure && state != FAILED && (!hasAudio || audioQueuedSec() < 0.05)) state = ENDED;
    return show;
  }
  void recycle(Frame* f) { f->surface.reset(); SDL_LockMutex(mu); spare.push_back(f); SDL_UnlockMutex(mu); }

  double position() const { return lastPos; }
  bool waiting() const { return state == CONNECTING || state == BUFFERING; }
};

}  // namespace player

#include "player_h264.hpp"
#include "player_nest.hpp"
