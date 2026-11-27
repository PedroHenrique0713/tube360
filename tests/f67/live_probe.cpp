// Sonda real (PC): lê o JSON do /player do TVHTML5 de uma live pública (tests/f67/live_probe.py grava), resolve o `n`
// com o serviço do app, abre a live por segmentos e mede o atraso. Só rede pública, nenhuma conta.
//   f67-live-probe <player.json> <pasta de cache> [lowlatency 0|1] [segundos de reprodução=40]
#include "yt/live_seq.hpp"
#include "yt/nsig_service.hpp"
#include "update/rules.hpp"
#include "picojson.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
using namespace yt;
static double nowSec() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf();
    picojson::value j; std::string err = picojson::parse(j, ss.str());
    if (!err.empty()) { std::printf("json: %s\n", err.c_str()); return 2; }
    const bool low = argc > 3 && std::atoi(argv[3]) == 1; const double play = argc > 4 ? std::atof(argv[4]) : 40;
    string video, audio; int vh = 0;
    for (auto& fm : j.get("streamingData").get("adaptiveFormats").get<picojson::array>()) {
        string mime = fm.get("mimeType").to_str(), url = fm.contains("url") ? fm.get("url").to_str() : "";
        int h = fm.contains("height") ? int(fm.get("height").get<double>()) : 0;
        if (mime.find("video/mp4") == 0 && mime.find("avc1") != string::npos && h <= 720 && h > vh && !url.empty()) { video = url; vh = h; }
        if (mime.find("audio/mp4") == 0 && fm.get("itag").get<double>() == 140 && !url.empty()) audio = url;
    }
    std::printf("video %dp, audio %s\n", vh, audio.empty() ? "?" : "140");
    if (video.empty() || audio.empty()) return 3;
    auto rules = updates::rules();
    auto net = yt::https(nullptr);
    double t0 = nowSec();
    auto e = nsig::rewrite(*net, *rules, argv[2], {&video, &audio});
    std::printf("nsig: %s em %.0f ms, sts %d, contadores downloads=%u bytecode=%u solves=%u\n", e.empty() ? "ok" : e.c_str(), (nowSec() - t0) * 1000,
                nsig::timestamp(*net, *rules, argv[2]), nsig::counters().downloads, nsig::counters().bytecodeLoads, nsig::counters().solves);
    if (!e.empty()) return 4;
    std::atomic_bool stop{false};
    LiveSeq ls;
    t0 = nowSec();
    if (!ls.open(video, audio, low, &stop, yt::https(&stop), yt::https(&stop))) { std::printf("open: %s HTTP %ld\n", ls.error.c_str(), ls.httpStatus()); return 5; }
    std::printf("modo %s: borda sq=%lld, inicio sq=%lld, X-Head-Time %.1f, aberto em %.0f ms\n", low ? "atraso minimo" : "padrao", ls.headSeq(), ls.first, ls.headStartSec(), (nowSec() - t0) * 1000);
    AVFormatContext* fmt = avformat_alloc_context();
    auto* buf = (uint8_t*)av_malloc(32768);
    fmt->pb = avio_alloc_context(buf, 32768, 0, &ls.tracks[0], LiveSeq::pull, nullptr, nullptr);
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&fmt, nullptr, av_find_input_format("mov"), nullptr) < 0) { std::puts("mov open failed"); return 6; }
    const AVCodec* codec = avcodec_find_decoder(fmt->streams[0]->codecpar->codec_id);
    AVCodecContext* ctx = avcodec_alloc_context3(codec); avcodec_parameters_to_context(ctx, fmt->streams[0]->codecpar); avcodec_open2(ctx, codec, nullptr);
    AVPacket* pkt = av_packet_alloc(); AVFrame* fr = av_frame_alloc();
    // Reprodução simulada em tempo real com o mesmo controlador de velocidade do player (seq::speedFor): quadro tardio = travada.
    double first = -1, last = -1, prev = -1, maxGap = 0, latMin = 1e9, latMax = -1, nextSample = 0, speed = 1, mAnchor = 0, wAnchor = 0, wallFirst = 0, lagFirst = 0;
    double stalled = 0, aheadMin = 1e9, history[6]; int samples = 0; double nextTick = 0; unsigned frames = 0, stalls = 0, changes = 0;
    while (av_read_frame(fmt, pkt) >= 0) {
        if (avcodec_send_packet(ctx, pkt) >= 0) while (avcodec_receive_frame(ctx, fr) >= 0) {
            double pts = fr->best_effort_timestamp * av_q2d(fmt->streams[0]->time_base);
            if (first < 0) { first = pts; wallFirst = wAnchor = nowSec(); mAnchor = 0; lagFirst = ls.latencySec(pts); std::printf("1o quadro %.2f (%dx%d) apos %.0f ms, atraso inicial %.2f s\n", pts, fr->width, fr->height, (wallFirst - t0) * 1000, lagFirst); }
            double due = wAnchor + ((pts - first) - mAnchor) / speed, now = nowSec();
            if (due > now) { struct timespec d{(time_t)(due - now), (long)(((due - now) - (time_t)(due - now)) * 1e9)}; nanosleep(&d, nullptr); }
            else if (now - due > 0.15) { stalled += now - due; ++stalls; wAnchor = now; mAnchor = pts - first; }   // travou: re-ancora
            if (prev >= 0) maxGap = std::max(maxGap, pts - prev);
            prev = last = pts; ++frames;
            double ahead = ls.pushedEndSec() - pts; if (ahead >= 0) aheadMin = std::min(aheadMin, ahead);
            double wall = nowSec() - wallFirst;
            if (wall >= nextTick + 1.0) {
                nextTick = wall;
                double l = ls.latencySec(pts); if (wall > 8 && l > 0) { latMin = std::min(latMin, l); latMax = std::max(latMax, l); }
                if (aheadMin < 1e8) { history[samples++ % 6] = aheadMin; aheadMin = 1e9; }
                if (low && samples >= 3) {
                    double lo = 1e9; for (int i = 0; i < std::min(samples, 6); ++i) lo = std::min(lo, history[i]);
                    double want = seq::speedFor(l, lo, ls.segSec(), speed, true);
                    if (want != speed) { mAnchor = pts - first; wAnchor = nowSec(); speed = want; ++changes; std::printf("  t=%.0fs atraso %.2f reserva minima %.2f -> velocidade %.2fx\n", wall, l, lo, want); }
                }
            }
        }
        av_packet_unref(pkt);
        if (first >= 0 && last - first >= play) break;
    }
    double wallPlayed = nowSec() - wallFirst;
    double shownLag = ls.latencySec(last);
    std::printf("atraso real a cada 1 s (depois dos 8 s): min %.2f s, max %.2f s; travadas %u (%.1f s no total); mudancas de velocidade %u\n", latMin, latMax, stalls, stalled, changes);
    std::printf("RESULT quadros=%u reproduzidos=%.1f s em %.1f s, maior intervalo %.3f s, atraso final %.2f s, segmentos v%u a%u, lacunas %u, borda sq=%lld\n", frames, last - first,
                wallPlayed, maxGap, shownLag, ls.tracks[0].segments.load(), ls.tracks[1].segments.load(), ls.tracks[0].gaps.load(), ls.headSeq());
    stop = true; ls.close();
    return frames > 30 ? 0 : 1;
}
