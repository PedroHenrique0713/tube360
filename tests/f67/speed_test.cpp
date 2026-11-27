// setLiveSpeed(): o relógio do áudio não pode saltar na troca (nos dois sentidos), o ícone de velocidade escolhida não é tocado
// e a âncora não depende do sentinela audioBaseMs < 0.   f67-speed-test
#include <cmath>
#include <cstdio>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#undef PL_MPEG_IMPLEMENTATION
#define LOG(...) SDL_Log(__VA_ARGS__)
#include "player.hpp"
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL %s:%d %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::puts(""); } } while (0)
int main() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"); SDL_Init(SDL_INIT_AUDIO);
    player::Player p; p.init();
    SDL_PauseAudioStreamDevice(p.audio);                       // fila parada: o "tocado" só anda quando o teste empurra PCM
    p.live = true; p.hasAudio = true; p.speed = 1; p.deviceMs = 0;
    SDL_SetAtomicInt(&p.audioBaseMs, 8); SDL_SetAtomicInt(&p.pushed, 20 * 44100);   // 20 s tocados desde o PTS 0,008
    auto clock = [&] { SDL_LockAudioStream(p.audio); double c = p.tvClock(0, p.audioQueuedSec()); SDL_UnlockAudioStream(p.audio); return c; };
    const double before = clock();
    CHECK(std::fabs(before - 20.008) < .002, "clock before %.3f", before);
    double previous = before;
    for (double next : {1.05, 1.05, 0.95, 1.0, 1.05, 1.0}) {
        p.setLiveSpeed(next);
        const double after = clock();
        CHECK(std::fabs(after - previous) < .002, "clock continuous across the switch to %.2fx: %.3f -> %.3f", next, previous, after);
        CHECK(SDL_GetAtomicInt(&p.audioBaseMs) == 8, "audioBaseMs untouched (%d): it keeps its 'no base' meaning", SDL_GetAtomicInt(&p.audioBaseMs));
        CHECK(p.speed.load() == 1.0 && std::fabs(p.liveRate.load() - next) < 1e-9, "chosen speed stays 1x, the live rate is %.2f", p.liveRate.load());
        // meio segundo de áudio de saída tocado no ritmo novo: o relógio anda a `next` vezes
        SDL_AddAtomicInt(&p.pushed, 22050);
        const double advanced = clock() - after;
        CHECK(std::fabs(advanced - 0.5 * next) < .003, "0.5 s of output at %.2fx advances the clock by %.3f s", next, advanced);
        previous = clock();
    }
    // Várias trocas depois de muito tempo: o relógio fica negativo-seguro (nenhum sentinela), nunca volta ao relógio de parede.
    for (int i = 0; i < 40; ++i) { p.setLiveSpeed(i % 2 ? 1.05 : 0.95); SDL_AddAtomicInt(&p.pushed, 44100); }
    CHECK(SDL_GetAtomicInt(&p.audioBaseMs) == 8 && clock() > previous + 30, "40 switches keep the clock moving forward (%.2f)", clock());
    // Velocidade escolhida pela pessoa: a live recusa o alcance (speed != 1).
    p.setLiveSpeed(1.0); p.speed = 1.5; p.setLiveSpeed(1.05);
    CHECK(p.liveRate.load() == 1.0, "the app does not stack its rate on a speed chosen by the person");
    std::puts(failures ? "RESULT FAILED" : "RESULT OK");
    return failures ? 1 : 0;
}
