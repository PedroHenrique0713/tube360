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

// Atraso da imagem em relação ao fim do segmento completo mais novo (borda disponível). `headStart` é o X-Head-Time do
// segmento mais novo; `shown` o PTS absoluto em tela. O piso de um live de 5 s é um segmento.
inline double lag(double headStartSec, double segSec, double shownAbsSec) { return headStartSec + segSec - shownAbsSec; }
// Histerese do alcance a 1,05x: liga quando o atraso passa de um segmento + 1 s e há reserva; desliga ao chegar perto do
// piso ou quando a reserva acaba (alcançar sem reserva só trocaria atraso por travada).
inline double catchUp(double lagSec, double aheadSec, double segSec, double current, bool lowLatency) {
    if (!lowLatency) return 0;
    if (current > 0) return (lagSec <= segSec + .3 || aheadSec < 1.0) ? 0 : current;
    return (lagSec > segSec + 1.0 && aheadSec >= 2.0) ? 0.05 : 0;
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
    double segSec = 5.0;
    bool lowLatency = false;
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
        auto head = fetch(tracks[0], videoUrl);          // sem sq: o segmento completo mais novo
        if (head.status != 200 || head.headSeq < 0) { error = head.error.empty() ? "http_" + std::to_string(head.status) : head.error; tracks[0].status = head.status; return false; }
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
    double headStartSec() const { auto ms = tracks[0].headTimeMs.load(); return ms < 0 ? -1 : ms / 1000.0; }
    long long headSeq() const { return tracks[0].headSeq.load(); }
    double pushedEndSec() const { return tracks[0].pushedEnd.load(); }
    int failure() const { return std::max(tracks[0].error, tracks[1].error); }
    long httpStatus() const { return tracks[0].status ? tracks[0].status : tracks[1].status; }

private:
    Response fetch(Track& t, const string& url) {
        Request req; req.url = url; req.media = true; req.timeoutMs = 20000; req.connectTimeoutMs = 8000;
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
        if (r.wallMs >= 0) t.wallMs = r.wallMs;
    }
    bool push(Track& t, string body, bool) {
        string out; double start = -1;
        if (!t.timescale) t.timescale = readTimescale(body);
        if (!seq::strip(body, t.sendInit, out, &start, t.timescale ? t.timescale : 90000)) return false;
        SDL_LockMutex(mu);
        t.chunks.push_back(std::move(out)); ++t.segmentsQueued;
        SDL_BroadcastCondition(cv);
        SDL_UnlockMutex(mu);
        t.sendInit = false; ++t.segments; t.pushed += 1;
        if (start >= 0) t.pushedEnd = start + segSec;
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
    void run(Track& t) {
        int failures = 0;
        while (!stopped()) {
            SDL_LockMutex(mu);
            while (t.segmentsQueued >= 2 && !stopped()) SDL_WaitConditionTimeout(cv, mu, 100);
            SDL_UnlockMutex(mu);
            if (stopped()) break;
            auto r = fetch(t, seq::url(t.base, t.next));
            if (stopped()) break;
            if (r.status == 200 && r.error.empty()) {
                note(t, r);
                if (!push(t, std::move(r.body), false)) { finish(t, -2, 200); return; }
                ++t.next; failures = 0;
                continue;
            }
            if (r.status == 403) { finish(t, 403, 403); return; }   // URL vencida: o player renova
            if (r.status == 404 || r.status == 400) {
                // Segmento fora da janela do servidor: salta para a borda (uma descontinuidade, contada).
                auto edge = fetch(t, t.base);
                if (edge.status == 200 && edge.headSeq > t.next && ++t.gaps <= 3) { note(t, edge); t.next = edge.headSeq; continue; }
                finish(t, 404, r.status); return;
            }
            if (++failures > 4 || r.failure == Failure::CANCELLED) { finish(t, -1, r.status); return; }
            t.net->backoff(250u * unsigned(failures));
        }
        SDL_LockMutex(mu); t.eof = true; SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
    }
};
}
