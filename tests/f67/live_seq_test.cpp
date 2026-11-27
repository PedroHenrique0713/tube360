// LiveSeq (live de membros por segmentos &sq=N): caixas, PTS absoluto, borda, 403/404, cancelamento e alcance.
//   f67-live-seq-test <pasta do make_fixture.py>
#include "yt/live_seq.hpp"
#include "yt/media.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
using namespace yt;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL %s:%d %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::puts(""); } } while (0)

static string slurp(const string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static string box(const char* t, const string& payload) {
    uint32_t n = uint32_t(payload.size() + 8); string o;
    for (int k = 3; k >= 0; --k) o += char(n >> (8 * k));
    return o + string(t, 4) + payload;
}

// Servidor de live simulado: a borda avança uma vez por `tick` ms; sq acima da borda espera (long-poll).
struct Fake final : Transport {
    string dir; int count = 0; long long headNow = 4; long long base = 14077013; double segSec = 2;
    std::atomic<int> tick{60};
    std::atomic<long long> head{4};
    std::atomic<long> failStatus{0}; std::atomic<long long> failFrom{-1}, missingBelow{-1};
    std::atomic<unsigned> requests{0}; bool stallAs204 = false, headAs204 = false;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    long long headAt() const {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        return std::min<long long>(count - 1, head.load() + ms / tick.load());
    }
    Response request(const string& url, const string&, bool) override { Request r; r.url = url; return send(r); }
    Response send(const Request& r) override {
        ++requests;
        Response res; bool video = r.url.find("track=v") != string::npos;
        auto at = r.url.find("&sq=");
        long long sq = at == string::npos ? -1 : std::atoll(r.url.c_str() + at + 4);
        if (failStatus && failFrom >= 0 && sq >= failFrom) { res.status = failStatus; return res; }
        if (sq >= 0 && sq < missingBelow) { res.status = 404; return res; }
        if (sq < 0 && headAs204) { res.status = 204; return res; }
        if (sq < 0) sq = headAt();
        for (int waited = 0; sq > headAt() && !cancelled(); waited += 5) { if (waited > 4000) { res.status = 503; return res; } std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        if (sq >= count) { for (int w = 0; w < 600 && !cancelled(); w += 5) std::this_thread::sleep_for(std::chrono::milliseconds(5)); res.status = stallAs204 ? 204 : 404; return res; }
        res.status = 200; res.body = slurp(dir + (video ? "/v_" : "/a_") + std::to_string(sq) + ".bin");
        res.headSeq = headAt(); res.segmentSeq = sq; res.headTimeMs = (base + res.headSeq * (long long)segSec) * 1000LL; res.wallMs = 1795766719878LL;
        return res;
    }
    bool cancelled() const override { return stopFlag && *stopFlag; }
    const std::atomic_bool* stopFlag = nullptr;
};
struct Shared { Fake* f; };
// Cada faixa recebe um Transport próprio; o teste usa dois proxies que apontam para o mesmo Fake.
struct Proxy final : Transport {
    Fake& f; bool video;
    Proxy(Fake& fake, bool v) : f(fake), video(v) {}
    Response request(const string& u, const string&, bool) override { Request r; r.url = u; return send(r); }
    Response send(const Request& r) override { Request q = r; q.url += video ? "&track=v" : "&track=a"; auto at = q.url.find("&sq="); (void)at; return f.send(q); }
    bool cancelled() const override { return f.cancelled(); }
};

// HLS público: playlist de segmentos de 2 s com PROGRAM-DATE-TIME; o leitor entra N segmentos antes da borda.
struct HlsFake final : Transport {
    Response request(const string& url, const string&, bool) override {
        Response r; r.status = 200;
        if (url.ends_with("/master.m3u8")) r.body = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=1280x720,FRAME-RATE=30,CODECS=\"avc1.4d401f,mp4a.40.2\"\nmedia.m3u8\n";
        else if (url.ends_with("/media.m3u8")) {
            r.body = "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:100\n";
            for (int i = 0; i < 6; ++i) { char l[160]; std::snprintf(l, sizeof l, "#EXT-X-PROGRAM-DATE-TIME:2026-11-27T10:00:%02d.000Z\n#EXTINF:2.000,\nseg%d.ts\n", 2 * i, 100 + i); r.body += l; }
        } else r.body = "ts";
        return r;
    }
};
struct Decoded { unsigned video = 0, audio = 0; double firstV = -1, lastV = -1, firstA = -1, lastA = -1, maxGapV = 0; bool monotonic = true; int err = 0; };
// Decodifica uma faixa pela mesma via do player: AVIO custom, formato mov, sem seek.
static Decoded decode(LiveSeq::Track* t, bool video, unsigned maxFrames) {
    Decoded d;
    AVFormatContext* fmt = avformat_alloc_context();
    auto* buf = (uint8_t*)av_malloc(32768);
    fmt->pb = avio_alloc_context(buf, 32768, 0, t, LiveSeq::pull, nullptr, nullptr);
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    d.err = avformat_open_input(&fmt, nullptr, av_find_input_format("mov"), nullptr);
    if (d.err < 0) return d;
    int idx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) if (fmt->streams[i]->codecpar->codec_type == (video ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO)) idx = int(i);
    if (idx < 0) { d.err = -1; return d; }
    const AVCodec* codec = avcodec_find_decoder(fmt->streams[idx]->codecpar->codec_id);
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, fmt->streams[idx]->codecpar); avcodec_open2(ctx, codec, nullptr);
    AVPacket* pkt = av_packet_alloc(); AVFrame* fr = av_frame_alloc();
    double prev = -1;
    while ((d.err = av_read_frame(fmt, pkt)) >= 0 && (video ? d.video : d.audio) < maxFrames) {
        if (pkt->stream_index == idx && avcodec_send_packet(ctx, pkt) >= 0)
            while (avcodec_receive_frame(ctx, fr) >= 0) {
                double pts = fr->best_effort_timestamp * av_q2d(fmt->streams[idx]->time_base);
                if (video) { ++d.video; if (d.firstV < 0) d.firstV = pts; d.lastV = pts; if (prev >= 0) { d.maxGapV = std::max(d.maxGapV, pts - prev); if (pts <= prev) d.monotonic = false; } prev = pts; }
                else { ++d.audio; if (d.firstA < 0) d.firstA = pts; d.lastA = pts; }
            }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt); av_frame_free(&fr); avcodec_free_context(&ctx);
    avio_context_free(&fmt->pb); avformat_close_input(&fmt);
    return d;
}

static void pure() {
    // strip: ftyp+moov só no primeiro; emsg/styp descartados; caixa truncada reprova.
    string seg = box("ftyp", "isom") + box("moov", string(40, 'm')) + box("emsg", string(12, 'e')) + box("moof", string(30, 'f')) + box("mdat", string(100, 'd'));
    string out;
    CHECK(seq::strip(seg, true, out) && out.size() == seg.size() - box("emsg", string(12, 'e')).size(), "first keeps init, drops emsg (%zu)", out.size());
    CHECK(seq::strip(seg, false, out) && out == box("moof", string(30, 'f')) + box("mdat", string(100, 'd')), "later segments only moof+mdat");
    CHECK(!seq::strip(seg.substr(0, seg.size() - 5), false, out), "truncated box rejected");
    CHECK(!seq::strip(box("ftyp", "isom") + box("moov", "x"), false, out), "segment without media rejected");
    string big = box("moof", "a"); big += string("\0\0\0\1mdat", 8) + string(8, '\0'); big[big.size() - 1] = 24;
    big += string(8, 'z');
    CHECK(seq::strip(big, false, out) && out.size() == big.size(), "64-bit box size");
    CHECK(seq::url("https://x/y?a=1&n=Z", 12) == "https://x/y?a=1&n=Z&sq=12", "sq url");
    // atraso e alcance (piso = um segmento; histerese)
    CHECK(std::fabs(seq::lag(100, 5, 100) - 5) < 1e-9, "lag at the start of the newest segment is one segment");
    CHECK(seq::speedFor(15, 6, 5, 1, true) == 1.05, "behind with a buffer -> 1.05x");
    CHECK(seq::speedFor(15, 0.5, 5, 1, true) == 1.0 && seq::speedFor(15, 0.3, 5, 1, true) == 0.95, "behind but no buffer -> never speeds up; a thin buffer stretches");
    CHECK(seq::speedFor(9, 6, 5, 1, true) == 1.0 && seq::speedFor(11.5, 6, 5, 1, true) == 1.05, "5 s segments: catch-up above 2 segments + 1 s");
    CHECK(seq::speedFor(5.2, 6, 2, 1, true) == 1.05 && seq::speedFor(4.9, 6, 2, 1, true) == 1.0, "2 s segments scale the threshold");
    // histerese larga: entre 1,3 segmento + 0,5 s e 2 segmentos + 1 s o estado não muda (o serrote de um segmento não liga e desliga)
    for (double l = 8.0; l <= 11.0; l += .25) CHECK(seq::speedFor(l, 6, 5, 1.05, true) == 1.05 && seq::speedFor(l, 6, 5, 1, true) == 1.0, "no flapping inside the window (%.2f)", l);
    CHECK(seq::speedFor(6.9, 6, 5, 1.05, true) == 1.0 && seq::speedFor(12, .5, 5, 1.05, true) == 1.0, "catch-up off near the floor or without buffer");
    CHECK(seq::speedFor(5, .3, 5, 1, true) == 0.95 && seq::speedFor(5, 1.0, 5, 0.95, true) == 0.95 && seq::speedFor(5, 1.2, 5, 0.95, true) == 1.0, "thin buffer: 0.95x until 1.2 s of slack, hysteresis between");
    CHECK(seq::speedFor(20, 20, 5, 1, false) == 1.0 && seq::speedFor(5, 0.1, 5, 1, false) == 1.0, "option off never changes the speed");
    // duração do segmento lida do trun (sem número fixo)
    {
        string trun = string("\0\0\1\0\0\0\0\3", 8);        // flags 0x100 (duração por amostra), 3 amostras
        for (int d : {3000, 3000, 3000}) { for (int k = 3; k >= 0; --k) trun += char(d >> (8 * k)); }
        string moof = box("moof", box("traf", box("trun", trun)));
        CHECK(std::fabs(seq::duration(moof, 1500) - 6.0) < 1e-9, "segment duration from the trun (%.3f)", seq::duration(moof, 1500));
        CHECK(seq::duration("nada", 90000) == 0, "unreadable segment gives 0");
    }
}

int main(int argc, char** argv) {
    pure();
    if (argc < 2) { std::puts("(sem pasta de fixture: só as partes puras)"); return failures ? 1 : 0; }
    Fake fake; fake.dir = argv[1];
    { auto m = slurp(fake.dir + "/meta.txt"); std::istringstream in(m); string k; long long v; while (in >> k >> v) { if (k == "segments") fake.count = int(v); if (k == "segsec") fake.segSec = double(v); if (k == "base") fake.base = v; } }
    std::atomic_bool stop{false}; fake.stopFlag = &stop;
    CHECK(fake.count >= 6, "fixture has %d segments", fake.count);

    // 1. Padrão: começa 2 segmentos antes da borda; o PTS absoluto (14 milhões de s) sai intacto.
    {
        fake.head = 4; fake.t0 = std::chrono::steady_clock::now(); fake.tick = 4000;
        LiveSeq ls;
        bool ok = ls.open("https://r1.googlevideo.com/videoplayback?live=1", "https://r1.googlevideo.com/videoplayback?live=1", false, &stop,
                          std::make_unique<Proxy>(fake, true), std::make_unique<Proxy>(fake, false), 2);
        CHECK(ok, "open: %s", ls.error.c_str());
        CHECK(ls.first == 2 && ls.headSeq() == 4, "start two segments behind the edge (first=%lld head=%lld)", ls.first, ls.headSeq());
        double lagStart = seq::lag(ls.headStartSec(), fake.segSec, ls.headStartSec() - 2 * fake.segSec);
        CHECK(std::fabs(lagStart - 3 * fake.segSec) < .01, "default start lag = three segments (%.1f)", lagStart);
        Decoded v, a;
        std::thread tv([&] { v = decode(&ls.tracks[0], true, 80); });
        std::thread ta([&] { a = decode(&ls.tracks[1], false, 80); });
        tv.join(); ta.join();
        std::printf("default: video %u frames %.3f..%.3f, audio %u frames %.3f..%.3f, max video gap %.3f s\n", v.video, v.firstV, v.lastV, a.audio, a.firstA, a.lastA, v.maxGapV);
        CHECK(v.video >= 80, "decoded %u video frames (err %d)", v.video, v.err);
        CHECK(a.audio >= 40, "decoded %u audio frames (err %d)", a.audio, a.err);
        CHECK(v.firstV > 14000000 && std::fabs(v.firstV - (fake.base + 2 * fake.segSec)) < .2, "absolute PTS kept, no 33-bit wrap (%.3f)", v.firstV);
        CHECK(std::fabs(a.firstA - v.firstV) < .1, "audio and video share the timeline (%.3f vs %.3f)", a.firstA, v.firstV);
        CHECK(v.monotonic && v.maxGapV < .06, "video PTS continuous across segments (gap %.3f)", v.maxGapV);
        CHECK(ls.tracks[0].gaps == 0, "no gaps");
        CHECK(std::fabs(ls.segSec() - fake.segSec) < .05, "segment duration is read from the stream, not fixed (%.2f)", ls.segSec());
        stop = true; ls.close(); stop = false;
    }
    // 2. Atraso mínimo: começa na borda (o segmento completo mais novo).
    {
        fake.head = 5; fake.t0 = std::chrono::steady_clock::now(); fake.tick = 4000;
        LiveSeq ls;
        bool ok = ls.open("https://r1.googlevideo.com/videoplayback?live=1", "https://r1.googlevideo.com/videoplayback?live=1", true, &stop,
                          std::make_unique<Proxy>(fake, true), std::make_unique<Proxy>(fake, false));
        CHECK(ok && ls.first == 5, "low latency starts at the edge (first=%lld)", ls.first);
        Decoded v; std::thread tv([&] { v = decode(&ls.tracks[0], true, 45); }); Decoded a; std::thread ta([&] { a = decode(&ls.tracks[1], false, 20); }); tv.join(); ta.join();
        double lag = seq::lag(ls.headStartSec(), ls.segSec(), v.firstV);
        std::printf("low latency: first frame %.3f, lag %.2f s (floor %.1f s)\n", v.firstV, lag, fake.segSec);
        CHECK(v.video >= 45 && std::fabs(v.firstV - (fake.base + 5 * fake.segSec)) < .2, "first frame is the edge segment (%.3f)", v.firstV);
        CHECK(lag <= fake.segSec + .05, "lag at start is one segment (%.2f)", lag);
        stop = true; ls.close(); stop = false;
    }
    // 3. 403 (URL vencida) vira erro com o status; 404 salta para a borda, uma vez.
    {
        fake.head = 3; fake.t0 = std::chrono::steady_clock::now(); fake.tick = 100000; fake.failStatus = 403; fake.failFrom = 5;
        LiveSeq ls;
        ls.open("https://r1.googlevideo.com/videoplayback?live=1", "https://r1.googlevideo.com/videoplayback?live=1", false, &stop,
                std::make_unique<Proxy>(fake, true), std::make_unique<Proxy>(fake, false), 0);
        fake.head = 5; fake.tick = 50; fake.t0 = std::chrono::steady_clock::now();
        Decoded v = decode(&ls.tracks[0], true, 1000);
        std::printf("403: decoded %u frames, status %ld, failure %d\n", v.video, ls.httpStatus(), ls.failure());
        CHECK(ls.httpStatus() == 403 && ls.failure() == 403, "403 surfaced (%ld)", ls.httpStatus());
        CHECK(v.err != 0 && v.video > 0, "frames before the 403 were delivered, then the read failed (%d)", v.err);
        stop = true; ls.close(); stop = false; fake.failStatus = 0; fake.failFrom = -1;
    }
    {
        fake.head = 6; fake.t0 = std::chrono::steady_clock::now(); fake.tick = 100000; fake.missingBelow = 4;
        LiveSeq ls;
        ls.open("https://r1.googlevideo.com/videoplayback?live=1", "https://r1.googlevideo.com/videoplayback?live=1", false, &stop,
                std::make_unique<Proxy>(fake, true), std::make_unique<Proxy>(fake, false), 3);  // sq=3 está fora da janela
        Decoded v = decode(&ls.tracks[0], true, 30);
        std::printf("404 -> edge: gaps %u, first frame %.3f, %u frames\n", ls.tracks[0].gaps.load(), v.firstV, v.video);
        CHECK(ls.tracks[0].gaps == 1 && v.video >= 30, "one jump to the edge, playback continues");
        stop = true; ls.close(); stop = false; fake.missingBelow = -1;
    }
    // 3b. Live parada: o servidor só responde 204 ("ainda não") depois da última; o leitor termina limpo, não congela nem acusa falha.
    {
        fake.head = fake.count - 2; fake.t0 = std::chrono::steady_clock::now(); fake.tick = 100000; fake.stallAs204 = true;
        LiveSeq ls; ls.stallMs = 1500;
        ls.open("https://r1.googlevideo.com/videoplayback?live=1", "https://r1.googlevideo.com/videoplayback?live=1", true, &stop,
                std::make_unique<Proxy>(fake, true), std::make_unique<Proxy>(fake, false));
        auto t = std::chrono::steady_clock::now();
        Decoded v = decode(&ls.tracks[0], true, 100000);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count();
        std::printf("stalled live: %u frames, ended after %lld ms, failure %d, status %ld\n", v.video, (long long)ms, ls.failure(), ls.httpStatus());
        CHECK(v.err == AVERROR_EOF && ls.failure() == 0 && v.video > 0 && ms < 15000, "204 'not yet' is not a failure; a stopped live ends cleanly (err %d)", v.err);
        stop = true; ls.close(); stop = false; fake.stallAs204 = false;
    }
    // HLS: o atraso mínimo começa no segmento mais novo; o padrão três antes. UTC e duração vêm da playlist.
    {
        double f1, e1, d1, f3, e3, d3;
        HlsFake lists, media; std::atomic_bool stop2{false};
        auto begin = [&](int back, double& first, double& edge, double& dur) {
            LiveFile live(lists, media, stop2); live.startBack = back;
            bool ok = live.open("https://fixture.invalid/master.m3u8", 720);
            for (int i = 0; i < 400 && live.firstStamp.load() < 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            first = live.firstStamp; edge = live.edgeEnd; dur = live.segmentSec;
            return ok;
        };
        CHECK(begin(1, f1, e1, d1) && begin(3, f3, e3, d3), "HLS fixture opens");
        std::printf("HLS: low latency starts at stamp +%.0f s, default at +%.0f s, edge end +%.0f s, segment %.1f s\n", f1 - 1795000000.0 * 0, f3 - f1, e1 - f1, d1);
        CHECK(std::fabs((e1 - f1) - 2.0) < .01, "low latency starts on the newest segment: one segment to the edge (%.2f)", e1 - f1);
        CHECK(std::fabs((e3 - f3) - 6.0) < .01, "default starts three segments back (%.2f)", e3 - f3);
        CHECK(std::fabs(d1 - 2.0) < .01, "segment duration from the playlist (%.2f)", d1);
    }
    // 3c. Live que ainda não começou (ou parou antes da abertura): a borda responde 204. É "esperando", não falha de formato.
    {
        fake.headAs204 = true;
        LiveSeq ls;
        bool ok = ls.open("https://r1.googlevideo.com/videoplayback?live=1", "https://r1.googlevideo.com/videoplayback?live=1", true, &stop,
                          std::make_unique<Proxy>(fake, true), std::make_unique<Proxy>(fake, false));
        CHECK(!ok && ls.error == "waiting", "204 at the edge is 'waiting' (%s), so the player shows the live-not-started message and does not step down the ladder", ls.error.c_str());
        fake.headAs204 = false; stop = true; ls.close(); stop = false;
    }
    // 4. Cancelar enquanto espera o long-poll destrava a leitura sem demora.
    {
        fake.head = 1; fake.t0 = std::chrono::steady_clock::now(); fake.tick = 1000000;
        LiveSeq ls;
        ls.open("https://r1.googlevideo.com/videoplayback?live=1", "https://r1.googlevideo.com/videoplayback?live=1", true, &stop,
                std::make_unique<Proxy>(fake, true), std::make_unique<Proxy>(fake, false));
        std::thread canceller([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); stop = true; });
        auto t = std::chrono::steady_clock::now();
        Decoded v = decode(&ls.tracks[0], true, 100000);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count();
        canceller.join(); ls.close(); stop = false;
        std::printf("cancel while waiting on the next segment: %u frames, returned after %lld ms\n", v.video, (long long)ms);
        CHECK(ms < 2500 && v.err < 0, "read returned promptly after cancel (%lld ms)", (long long)ms);
    }
    std::puts(failures ? "RESULT FAILED" : "RESULT OK");
    return failures ? 1 : 0;
}
