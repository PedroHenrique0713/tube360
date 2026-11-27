// setLiveSpeed(): o relógio do áudio não pode saltar na troca (nos dois sentidos), o ícone de velocidade escolhida não é tocado
// e a âncora não depende do sentinela audioBaseMs < 0.   f67-speed-test
#include <cmath>
#include <cstdio>
#define LOG(...) SDL_Log(__VA_ARGS__)
#include "player.hpp"
#include "music_player.hpp"
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
    // Sair de uma live no meio do alcance e tocar uma música (só áudio): o relógio da música não herda o ritmo nem a âncora da live.
    {
        music::Player m; m.init();
        m.liveRate = 1.05; m.liveClockOffset = 0.37; m.live = true;
        m.audioOnly = true; m.speed = 1;
        m.start("musicvid001", 0, "song", 200, 720, false);
        CHECK(m.liveRate.load() == 1.0 && m.liveClockOffset == 0.0, "a music start clears the live rate (%.2f) and anchor (%.2f)", m.liveRate.load(), m.liveClockOffset);
        CHECK(m.rate() == 1.0, "music clock runs at 1x");
        m.halt();
    }
    // Música com a caixa pedida e nenhum Nest configurado (sem nest.txt): toca na TV, direto, sem erro de console, e
    // `speakerMissing` deixa a tela mostrar o mesmo aviso do vídeo (K::NoSpeaker).
    {
        music::Player m; m.init();
        CHECK(!m.directSpeaker.configured(), "no nest.txt: the speaker is not configured");
        m.audioOnly = true;
        m.start("musicvid001", 0, "song", 200, 720, true);
        CHECK(!m.speakerRequested && m.speakerMissing.load(), "unconfigured speaker: requested=%d missing=%d", (int)m.speakerRequested, (int)m.speakerMissing.load());
        CHECK(m.playingAudio && m.decThread && m.error.empty() && m.state != player::FAILED, "music starts its own audio on the TV (state %d, error '%s')", (int)m.state.load(), m.error.c_str());
        CHECK(m.output.load() == player::Player::LOCAL, "output is the TV");
        m.halt();
        m.start("musicvid001", 0, "song", 200, 720, false);
        CHECK(!m.speakerMissing.load(), "no speaker asked, nothing to warn about");
        m.halt();
        music::Player v; v.init(); v.audioOnly = false;
        v.start("videoid0001", 0, "clip", 100, 720, true);
        CHECK(!v.speakerRequested && v.speakerMissing.load() && v.error.empty() && v.state != player::FAILED, "a video with the same request plays on the TV and warns the same way");
        v.halt();
    }
    std::puts(failures ? "RESULT FAILED" : "RESULT OK");
    return failures ? 1 : 0;
}
