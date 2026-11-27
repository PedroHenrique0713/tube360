// Live de membros sem HLS: só adaptiveFormats com live=1. Cada faixa (vídeo, áudio) é lida por segmentos &sq=N de cerca de
// 5 s, fMP4 autossuficiente (ftyp+moov+emsg+moof+mdat). O primeiro segmento entrega ftyp+moov; dos seguintes só moof+mdat,
// de modo que o demuxer enxerga um único fMP4 contínuo. `sq` futuro faz long-poll; X-Head-* dizem onde está a borda.
#pragma once
#include "client.hpp"
#include "thread.hpp"
extern "C" {
#include <libavformat/avformat.h>
}
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace yt {
namespace seq {

// Mantém ftyp+moov (só no primeiro segmento) e moof+mdat; descarta emsg/styp/sidx. Falso se as caixas não fecham.
inline bool strip(const string& in, bool keepInit, string& out, double* startSec = nullptr, uint32_t timescale = 0) {
    out.clear();
    size_t i = 0;
    bool media = false;
    while (i + 8 <= in.size()) {
        uint64_t size = (uint8_t(in[i]) << 24) | (uint8_t(in[i+1]) << 16) | (uint8_t(in[i+2]) << 8) | uint8_t(in[i+3]);
        const char* type = in.data() + i + 4;
        size_t head = 8;
        if (size == 1) {
            if (i + 16 > in.size()) return false;
            size = 0; for (int k = 0; k < 8; ++k) size = (size << 8) | uint8_t(in[i+8+k]);
            head = 16;
        } else if (size == 0) size = in.size() - i;
        if (size < head || size > in.size() - i) return false;
        const bool init = !std::memcmp(type, "ftyp", 4) || !std::memcmp(type, "moov", 4);
        const bool data = !std::memcmp(type, "moof", 4) || !std::memcmp(type, "mdat", 4);
        if (data) media = true;
        if (startSec && timescale && !std::memcmp(type, "moof", 4)) {
            // moof > traf > tfdt: baseMediaDecodeTime absoluto da faixa.
            size_t at = in.find("tfdt", i);
            if (at != string::npos && at + 16 <= i + size) {
                uint8_t version = uint8_t(in[at+4]);
                uint64_t base = 0;
                if (version == 1 && at + 16 <= in.size()) { for (int k = 0; k < 8; ++k) base = (base << 8) | uint8_t(in[at+8+k]); }
                else for (int k = 0; k < 4; ++k) base = (base << 8) | uint8_t(in[at+8+k]);
                *startSec = double(base) / timescale;
            }
        }
        if (data || (init && keepInit)) out.append(in, i, size_t(size));
        i += size_t(size);
    }
    return i == in.size() && media;
}
inline string url(const string& base, long long sq) { return base + "&sq=" + std::to_string(sq); }

// Duração do segmento (s): soma das durações de amostra do trun (ou o default do tfhd). 0 se não deu para ler.
inline double duration(const string& seg, uint32_t timescale) {
    if (!timescale) return 0;
    auto u32 = [&](size_t at) { return at + 4 <= seg.size() ? (uint32_t(uint8_t(seg[at])) << 24) | (uint32_t(uint8_t(seg[at+1])) << 16) | (uint32_t(uint8_t(seg[at+2])) << 8) | uint8_t(seg[at+3]) : 0u; };
    size_t moof = seg.find("moof");
    if (moof == string::npos) return 0;
    uint32_t defaultDuration = 0;
    if (size_t tfhd = seg.find("tfhd", moof); tfhd != string::npos) {
        uint32_t flags = u32(tfhd + 4) & 0xffffff; size_t at = tfhd + 12;
        if (flags & 1) at += 8;
        if (flags & 2) at += 4;
        if (flags & 8) defaultDuration = u32(at);
    }
    size_t trun = seg.find("trun", moof);
    if (trun == string::npos) return 0;
    uint32_t flags = u32(trun + 4) & 0xffffff, count = u32(trun + 8);
    size_t at = trun + 12;
    if (flags & 1) at += 4;
    if (flags & 4) at += 4;
    const size_t each = ((flags & 0x100) ? 4 : 0) + ((flags & 0x200) ? 4 : 0) + ((flags & 0x400) ? 4 : 0) + ((flags & 0x800) ? 4 : 0);
    if (count == 0 || count > 100000) return 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (at + each > seg.size()) return 0;
        total += (flags & 0x100) ? u32(at) : defaultDuration;
        at += each;
    }
    return double(total) / timescale;
}

// Atraso da imagem em relação ao fim do segmento completo mais novo (borda disponível). `headStart` é o X-Head-Time do
// segmento mais novo; `shown` o PTS absoluto em tela. O piso de um live é um segmento.
inline double lag(double headStartSec, double segSec, double shownAbsSec) { return headStartSec + segSec - shownAbsSec; }
// Velocidade da live (WSOLA, tom preservado) no modo de atraso mínimo. `aheadLowSec` é o MENOR adiantamento (mídia entregue menos
// mídia em tela) dos últimos segundos: no piso do formato ele encosta em zero a cada segmento e qualquer atraso da rede vira
// travada, então a reserva fina estica a reprodução (0,95x) até sobrar folga; e o alcance (1,05x) só liga bem acima do piso
// (dois segmentos + 1 s, com reserva) e só desliga perto dele (1,3 segmento + 0,5 s). Entre os limites o estado não muda.
inline double speedFor(double latencySec, double aheadLowSec, double segSec, double current, bool lowLatency) {
    if (!lowLatency) return 1.0;
    if (current > 1.001) return (latencySec <= 1.3 * segSec + .5 || aheadLowSec < 1.0) ? 1.0 : current;
    if (current < 0.999) return aheadLowSec >= 1.2 ? 1.0 : current;
    if (latencySec > 2 * segSec + 1.0 && aheadLowSec >= 2.0) return 1.05;
    if (aheadLowSec < 0.4) return 0.95;
    return 1.0;
}
}  // namespace seq

class LiveSeq {
public:
    struct Track {
        LiveSeq* owner = nullptr;
        string base;                  // URL com `n` resolvido, sem sq
        std::unique_ptr<Transport> net;
        SDL_Thread* thread = nullptr;
        std::deque<string> chunks; size_t offset = 0;
        int segmentsQueued = 0;
        bool eof = false;
        int error = 0;                // 0 ok; -1 rede; 403/404 do HTTP
        long status = 0;
        long long next = 0;
        bool sendInit = true;
        uint32_t timescale = 0;
        std::atomic<long long> headSeq{-1}, headTimeMs{-1}, wallMs{-1}, pushed{0};
        std::atomic<double> pushedEnd{-1};   // PTS absoluto onde termina o último segmento entregue ao demuxer
        std::atomic<unsigned> segments{0}, gaps{0};
        bool video = false;
    } tracks[2];                      // 0 vídeo, 1 áudio
    SDL_Mutex* mu = SDL_CreateMutex();
    SDL_Condition* cv = SDL_CreateCondition();
    std::atomic_bool closing{false};
    const std::atomic_bool* cancel = nullptr;
    long long first = 0;
    std::atomic<double> segSecValue{5.0};   // lido do trun do primeiro segmento (2 s ou 5 s em lives de jogo)
    double segSec() const { return segSecValue.load(); }
    bool lowLatency = false;
    std::atomic<uint64_t> bytes{0};
    // Atraso real: a resposta que acaba de sair do long-poll de um segmento recém-completo marca o fim dele no relógio do servidor.
    std::atomic<long long> wallMs{-1}, offsetMs{LLONG_MAX};
    std::atomic<uint64_t> wallTick{0};
    std::atomic<long long> edgeSeq{-1}, edgeTick{0};
    uint64_t stallMs = 45000;   // sem segmento novo por tanto tempo: a transmissão parou
    string error;                     // código curto da falha de abertura

    ~LiveSeq() { close(); SDL_DestroyCondition(cv); SDL_DestroyMutex(mu); }
    bool stopped() const { return closing || (cancel && *cancel); }

    // Descobre a borda pela faixa de vídeo, escolhe o segmento inicial e dispara os leitores.
    // lowLatency: começa no segmento completo mais novo; senão, `back` segmentos antes (reserva de rede).
    bool open(const string& videoUrl, const string& audioUrl, bool lowLatencyStart, const std::atomic_bool* stopFlag,
              std::unique_ptr<Transport> videoNet, std::unique_ptr<Transport> audioNet, int back = 2) {
        cancel = stopFlag; lowLatency = lowLatencyStart;
        tracks[0].base = videoUrl; tracks[1].base = audioUrl;
        tracks[0].net = std::move(videoNet); tracks[1].net = std::move(audioNet);
        for (int i = 0; i < 2; ++i) { tracks[i].owner = this; tracks[i].video = i == 0; }
        string seed;
        // Sem sq: o segmento completo mais novo. Mesmo pedido de espera dos segmentos (a live pode estar começando): 204, tempo esgotado ou
        // 404 sem corpo são "ainda não / parada", não falha de formato, e não justificam descer degrau.
        auto head = fetch(tracks[0], videoUrl, true);
        if (head.status != 200 || head.headSeq < 0) {
            const bool waiting = head.status == 204 || (head.status == 0 && head.retryable) || (head.status == 200 && head.headSeq < 0);
            error = waiting ? "waiting" : head.error.empty() ? "http_" + std::to_string(head.status) : head.error;
            tracks[0].status = head.status; return false;
        }
        note(tracks[0], head);
        const long long start = std::max(0LL, head.headSeq - (lowLatency ? 0 : std::max(0, back)));
        first = start;
        for (auto& t : tracks) t.next = start;
        if (start == head.headSeq) seed = std::move(head.body);
        if (!seed.empty()) {
            if (!push(tracks[0], std::move(seed), true)) { error = "bad_segment"; return false; }
            tracks[0].next = start + 1;
        }
        for (auto& t : tracks) {
            t.thread = worker([](void* p) { auto* t = static_cast<Track*>(p); t->owner->run(*t); return 0; }, t.video ? "seq-v" : "seq-a", &t);
            if (!t.thread) { error = "thread"; return false; }
        }
        return true;
    }
    void close() {
        closing = true;
        SDL_LockMutex(mu); SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
        for (auto& t : tracks) if (t.thread) { SDL_WaitThread(t.thread, nullptr); t.thread = nullptr; }
    }
    // Callback do AVIO: bloqueia até haver bytes, AVERROR_EOF no fim, AVERROR_EXIT ao cancelar.
    static int pull(void* user, uint8_t* dst, int size) {
        auto& t = *static_cast<Track*>(user); auto& o = *t.owner;
        SDL_LockMutex(o.mu);
        while (t.chunks.empty() && !t.eof && !o.stopped()) SDL_WaitConditionTimeout(o.cv, o.mu, 100);
        int got = 0;
        if (!t.chunks.empty()) {
            auto& c = t.chunks.front();
            got = int(std::min<size_t>(size, c.size() - t.offset));
            std::memcpy(dst, c.data() + t.offset, size_t(got));
            if ((t.offset += size_t(got)) == c.size()) { t.chunks.pop_front(); t.offset = 0; --t.segmentsQueued; }
            SDL_BroadcastCondition(o.cv);
        }
        const bool stopping = o.stopped(), done = t.eof && t.chunks.empty();
        const int error = t.error;
        SDL_UnlockMutex(o.mu);
        if (got) return got;
        if (stopping) return AVERROR_EXIT;
        return done ? (error ? AVERROR(EIO) : AVERROR_EOF) : 0;
    }
    // Estado da borda, vindo do vídeo.
    // Atraso real da imagem em tela (s): agora no relógio do servidor menos o instante em que aquele quadro era o "ao vivo".
    // Sem medida ainda do fim de um segmento recém-fechado, devolve o atraso até o fim do segmento mais novo.
    double latencySec(double shownAbsSec) const {
        const long long off = offsetMs.load(), wall = wallMs.load();
        if (off == LLONG_MAX || wall < 0) return lag(headStartSec(), segSec(), shownAbsSec);
        const double now = double(wall) + double(SDL_GetTicks() - wallTick.load());
        return (now - (shownAbsSec * 1000.0 + double(off))) / 1000.0;
    }
    static double lag(double headStartSec, double segSec, double shownAbsSec) { return seq::lag(headStartSec, segSec, shownAbsSec); }
    double headStartSec() const { auto ms = tracks[0].headTimeMs.load(); return ms < 0 ? -1 : ms / 1000.0; }
    long long headSeq() const { return tracks[0].headSeq.load(); }
    double pushedEndSec() const { return tracks[0].pushedEnd.load(); }
    int failure() const { return std::max(tracks[0].error, tracks[1].error); }
    long httpStatus() const { return tracks[0].status ? tracks[0].status : tracks[1].status; }

private:
    // `poll`: pedido de sq futuro. O servidor segura a resposta até o segmento fechar (até ~7,5 s): sem corte por velocidade mínima.
    Response fetch(Track& t, const string& url, bool poll = false) {
        Request req; req.url = url; req.media = true; req.timeoutMs = poll ? 20000 : 12000; req.connectTimeoutMs = 8000; req.longPoll = poll;
        Response r;
        for (int hop = 0; hop < 4; ++hop) {
            r = t.net->send(req);
            if (r.status < 300 || r.status > 399 || !r.error.empty()) return r;
            const auto& next = r.location;
            auto slash = next.find('/', 8);
            if (!next.starts_with("https://") || slash == string::npos || !next.substr(8, slash - 8).ends_with(".googlevideo.com") ||
                next.find_first_of("@\\ \r\n\t") != string::npos) { r.error = "invalid_media_redirect"; return r; }
            req.url = next;
        }
        return r;
    }
    void note(Track& t, const Response& r) {
        if (r.headSeq >= 0) t.headSeq = r.headSeq;
        if (r.headTimeMs >= 0) t.headTimeMs = r.headTimeMs;
        if (r.wallMs >= 0) {
            t.wallMs = r.wallMs;
            if (t.video) {
                wallMs = r.wallMs; wallTick = SDL_GetTicks();
                // Resposta de um segmento que acabou de fechar (o pedido esperou por ele): o relógio do servidor marca o fim do segmento.
                if (r.headSeq >= 0 && r.segmentSeq == r.headSeq && r.seconds > .15 && r.headTimeMs >= 0) {
                    long long candidate = r.wallMs - (r.headTimeMs + (long long)(segSec() * 1000));
                    if (candidate < offsetMs.load()) offsetMs = candidate;
                }
            }
        }
    }
    bool push(Track& t, string body, bool) {
        string out; double start = -1;
        if (!t.timescale) t.timescale = readTimescale(body);
        if (!seq::strip(body, t.sendInit, out, &start, t.timescale ? t.timescale : 90000)) return false;
        SDL_LockMutex(mu);
        t.chunks.push_back(std::move(out)); ++t.segmentsQueued;
        SDL_BroadcastCondition(cv);
        SDL_UnlockMutex(mu);
        t.sendInit = false; ++t.segments; t.pushed += 1; bytes += body.size();
        if (t.video && t.segments == 1) if (double d = seq::duration(body, t.timescale ? t.timescale : 90000); d >= 0.5 && d <= 30) segSecValue = d;
        if (start >= 0) t.pushedEnd = start + segSec();
        return true;
    }
    // mdhd > timescale do primeiro `trak` (versão 0 ou 1). 0 se não achou.
    static uint32_t readTimescale(const string& b) {
        auto at = b.find("mdhd");
        if (at == string::npos || at + 24 > b.size()) return 0;
        size_t o = at + 4 + (uint8_t(b[at+4]) == 1 ? 20 : 12);
        if (o + 4 > b.size()) return 0;
        return (uint32_t(uint8_t(b[o])) << 24) | (uint32_t(uint8_t(b[o+1])) << 16) | (uint32_t(uint8_t(b[o+2])) << 8) | uint8_t(b[o+3]);
    }
    void finish(Track& t, int error, long status) {
        SDL_LockMutex(mu);
        t.error = error; t.status = status ? status : t.status; t.eof = true;
        SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
    }
    // Borda para o salto depois de um 404: a primeira trilha que precisa consulta, a outra reaproveita (mesmo sq nas duas, A/V alinhados).
    long long edgeFor(Track& t) {
        SDL_LockMutex(mu);
        const long long shared = edgeSeq.load();
        const bool recent = shared >= 0 && SDL_GetTicks() - uint64_t(edgeTick.load()) < 6000 && shared > t.next;
        SDL_UnlockMutex(mu);
        if (recent) return shared;
        auto edge = fetch(t, t.base);
        if (edge.status != 200 || edge.headSeq <= t.next) return -1;
        note(t, edge);
        SDL_LockMutex(mu); edgeSeq = edge.headSeq; edgeTick = (long long)SDL_GetTicks(); SDL_UnlockMutex(mu);
        return edge.headSeq;
    }
    void run(Track& t) {
        int failures = 0;
        uint64_t lastData = SDL_GetTicks();
        while (!stopped()) {
            SDL_LockMutex(mu);
            const bool blocked = t.segmentsQueued >= 2;
            while (t.segmentsQueued >= 2 && !stopped()) SDL_WaitConditionTimeout(cv, mu, 100);
            SDL_UnlockMutex(mu);
            if (stopped()) break;
            if (blocked) lastData = SDL_GetTicks();   // esperou a TV consumir (pausa): não conta como transmissão parada
            // Sem segmento novo por 45 s (headSeq parado): a transmissão acabou ou parou. Fim limpo, o player mostra "terminou" em vez de congelar.
            if (SDL_GetTicks() - lastData > stallMs) { finish(t, 0, 0); return; }
            auto r = fetch(t, seq::url(t.base, t.next), true);
            if (stopped()) break;
            if (r.status == 200 && r.error.empty()) {
                note(t, r);
                if (!push(t, std::move(r.body), false)) { finish(t, -2, 200); return; }
                ++t.next; failures = 0; lastData = SDL_GetTicks();
                continue;
            }
            if (r.status == 403) { finish(t, 403, 403); return; }   // URL vencida: o player renova
            if (r.status == 404 || r.status == 400) {
                // Segmento fora da janela do servidor: salta para a borda (uma descontinuidade, contada), a mesma para vídeo e áudio.
                long long edge = ++t.gaps <= 3 ? edgeFor(t) : -1;
                if (edge >= 0) { t.next = edge; continue; }
                finish(t, 404, r.status); return;
            }
            // "Ainda não": 204, ou o long-poll que estourou o prazo com o sq acima da borda conhecida. Não é falha.
            const bool ahead = t.headSeq.load() < 0 || t.next > t.headSeq.load();
            if (r.status == 204 || (ahead && r.status == 0 && !r.error.empty() && r.failure != Failure::CANCELLED && r.retryable)) {
                t.net->backoff(200);
                continue;
            }
            if (++failures > 4 || r.failure == Failure::CANCELLED) { finish(t, -1, r.status); return; }
            t.net->backoff(250u * unsigned(failures));
        }
        SDL_LockMutex(mu); t.eof = true; SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
    }
};
}
