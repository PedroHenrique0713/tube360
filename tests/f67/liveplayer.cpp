// Revisor F67 (delta): o Player real do app numa live pública, gravando o PCM que sai para a TV (postmix do dispositivo SDL dummy),
// a velocidade, o relógio do áudio e o PTS do quadro em tela. Mede troca de velocidade (WSOLA), estalo, buraco e A/V.
//   liveplayer2 <seq|hls> <player.json|videoId> <cache> <lowlatency 0|1> <segundos> <saida.wav> <saida.csv>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#undef PL_MPEG_IMPLEMENTATION
#define LOG(...) SDL_Log(__VA_ARGS__)
#include "player.hpp"
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
static std::mutex pcmMu; static std::vector<float> pcm; static uint64_t firstPcmMs = 0;
static void SDLCALL postmix(void*, const SDL_AudioSpec* spec, float* buf, int len) {
    std::lock_guard<std::mutex> l(pcmMu);
    if (!firstPcmMs) firstPcmMs = SDL_GetTicks();
    int n = len / int(sizeof(float)); if (spec->channels != 2) return;
    pcm.insert(pcm.end(), buf, buf + n);
}
static void wav(const char* path, const std::vector<float>& s) {
    FILE* f = std::fopen(path, "wb"); uint32_t data = uint32_t(s.size() * 2), rate = 44100, br = rate * 4;
    std::fwrite("RIFF", 1, 4, f); uint32_t sz = 36 + data; std::fwrite(&sz, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fl = 16; uint16_t pcmT = 1, ch = 2, ba = 4, bits = 16; std::fwrite(&fl, 4, 1, f); std::fwrite(&pcmT, 2, 1, f); std::fwrite(&ch, 2, 1, f);
    std::fwrite(&rate, 4, 1, f); std::fwrite(&br, 4, 1, f); std::fwrite(&ba, 2, 1, f); std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
    for (float v : s) { int16_t x = int16_t(std::clamp(v, -1.f, 1.f) * 32767); std::fwrite(&x, 2, 1, f); }
    std::fclose(f);
}
int main(int argc, char** argv) {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    SDL_Init(SDL_INIT_AUDIO);
    const std::string mode = argv[1]; const bool low = std::atoi(argv[4]) == 1; const int seconds = std::atoi(argv[5]);
    net::serverEnabled = false;
    player::Player p; p.cacheBase = argv[3]; p.init();
    if (!p.audio) { std::puts("sem audio"); return 2; }
    SDL_SetAudioPostmixCallback(SDL_GetAudioStreamDevice(p.audio), postmix, nullptr);
    auto net = yt::https(nullptr);
    std::string id;
    if (mode == "seq") {
        std::ifstream f(argv[2]); std::stringstream ss; ss << f.rdbuf();
        picojson::value j; picojson::parse(j, ss.str()); id = j.get("videoDetails").get("videoId").to_str();
        yt::Client client(*net, argv[3]);
        p.media = client.player(id, 720, nullptr, 0, "original", ss.str());
        std::printf("media: status=%s liveSeq=%d nsigSolved=%d video=%dp lower=%zu\n", p.media.status.c_str(), int(p.media.liveSeq), int(p.media.nsigSolved), p.media.video.height, p.media.lowerVideo.size());
        p.cachedQuality = 720; p.cachedVideoMode = 0; p.cachedAudio = "original"; p.validUntil = SDL_GetTicks() + 3600000;
    } else id = argv[2];
    p.liveLowLatency = low;
    p.start({"127.0.0.1", 1}, id, 0, "live", 0, 720, false, true, false, 0);
    FILE* csv = std::fopen(argv[7], "w"); std::fprintf(csv, "ms,speed,clock,display,state,shown,dropped\n");
    uint64_t t0 = SDL_GetTicks(); double lastSpeed = 1; int switches = 0;
    const int pauseAt = std::getenv("PAUSE_AT") ? std::atoi(std::getenv("PAUSE_AT")) : -1, pauseMs = std::getenv("PAUSE_MS") ? std::atoi(std::getenv("PAUSE_MS")) : 0;
    bool pausedOnce = false; uint64_t resumeAt = 0;
    while (SDL_GetTicks() - t0 < uint64_t(seconds) * 1000 && p.state != player::FAILED && p.state != player::ENDED) {
        if (auto* fr = p.take()) p.recycle(fr);
        if (pauseAt >= 0 && !pausedOnce && SDL_GetTicks() - t0 >= uint64_t(pauseAt) * 1000) { pausedOnce = true; p.setPaused(true); resumeAt = SDL_GetTicks() + pauseMs; std::printf("t=%.2f pausa %d ms (sem reabrir, simula travada de rede)\n", (SDL_GetTicks() - t0) / 1000.0, pauseMs); }
        if (resumeAt && SDL_GetTicks() >= resumeAt) { resumeAt = 0; p.setPaused(false); std::printf("t=%.2f retoma\n", (SDL_GetTicks() - t0) / 1000.0); }
        const double sp = p.liveRate.load();
        if (sp != lastSpeed) { ++switches; std::printf("t=%.2f velocidade %.2f -> %.2f\n", (SDL_GetTicks() - t0) / 1000.0, lastSpeed, sp); lastSpeed = sp; }
        std::fprintf(csv, "%llu,%.3f,%.4f,%.4f,%d,%lld,%lld\n", (unsigned long long)(SDL_GetTicks() - t0), sp, p.position(), p.lastPos, int(p.state.load()), (long long)p.shownFrames, (long long)p.droppedFrames);
        SDL_Delay(8);
    }
    std::fclose(csv);
    std::printf("RESULT estado=%d erro=%s quadros=%lld perdidos=%lld trocas=%d\n", int(p.state.load()), p.error.c_str(), (long long)p.shownFrames, (long long)p.droppedFrames, switches);
    p.halt();
    { std::lock_guard<std::mutex> l(pcmMu); wav(argv[6], pcm); std::printf("pcm %.1f s, inicio %llu ms\n", pcm.size() / 2 / 44100.0, (unsigned long long)(firstPcmMs - t0)); }
    return 0;
}
