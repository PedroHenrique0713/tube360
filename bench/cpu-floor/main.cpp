// Sonda isolada: H.264 da configuração do app, sem apresentação, rede ou áudio.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/md5.h>
#include <libavutil/opt.h>
}
#include "f30_stats.h"
#include "pipeline_layout.hpp"
#ifdef __XSDK__
#include <xsdk/log.h>
#include <xsdk/thread.h>
extern "C" void ffmpeg_workers(int active);
#define LOG(...) xsdk_log_write(__VA_ARGS__)
static constexpr const char* PLATFORM = "xbox";
#else
#define LOG(...) do { std::printf(__VA_ARGS__); std::puts(""); std::fflush(stdout); } while (0)
static constexpr const char* PLATFORM = "pc";
static void ffmpeg_workers(int) {}
#endif

static double nowMs() {
    timespec t{};
    if (clock_gettime(CLOCK_MONOTONIC, &t)) std::exit(2);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
static void fail(const char* what, int error = 0) {
    char detail[AV_ERROR_MAX_STRING_SIZE]{};
    if (error) av_strerror(error, detail, sizeof(detail));
    LOG("F30 FAIL %s error=%d %s", what, error, detail);
    std::exit(1);
}
struct Spec { const char* name; int w, h, fps, frames, kbps, level; double actualFps = 0; };
static constexpr Spec SPECS[] = {
    {"720p30-4000", 1280, 720, 30, 180, 4000, 31},
    {"1080p30-2500", 1920, 1080, 30, 180, 2500, 40},
    {"1080p30-5500", 1920, 1080, 30, 180, 5500, 40},
    {"1080p60-2500", 1920, 1080, 60, 360, 2500, 42},
    {"1080p60-5500", 1920, 1080, 60, 360, 5500, 42},
};
static constexpr const char* MODES[] = {"full", "no_loop_filter", "entropy"};
static std::string joined(const std::string& directory, const std::string& file) {
    return directory + (directory.empty() || (directory.back() != '/' && directory.back() != '\\') ? "/" : "") + file;
}
struct Memory {
    const std::vector<uint8_t>& data;
    size_t pos = 0;
    static int read(void* ptr, uint8_t* dst, int size) {
        auto& m = *static_cast<Memory*>(ptr);
        size_t n = std::min(size_t(size), m.data.size() - m.pos);
        if (!n) return AVERROR_EOF;
        std::memcpy(dst, m.data.data() + m.pos, n); m.pos += n;
        return int(n);
    }
    static int64_t seek(void* ptr, int64_t offset, int whence) {
        auto& m = *static_cast<Memory*>(ptr);
        if (whence == AVSEEK_SIZE) return int64_t(m.data.size());
        whence &= ~AVSEEK_FORCE;
        int64_t pos = offset;
        if (whence == SEEK_CUR) pos += int64_t(m.pos);
        else if (whence == SEEK_END) pos += int64_t(m.data.size());
        else if (whence != SEEK_SET) return AVERROR(EINVAL);
        if (pos < 0 || uint64_t(pos) > m.data.size()) return AVERROR(EINVAL);
        m.pos = size_t(pos); return pos;
    }
};
struct Clip {
    AVCodecParameters* params = avcodec_parameters_alloc();
    AVRational timebase{};
    std::vector<AVPacket*> packets;
    ~Clip() {
        for (auto* p : packets) av_packet_free(&p);
        avcodec_parameters_free(&params);
    }
    void load(const std::string& path, const Spec& spec) {
        FILE* file = std::fopen(path.c_str(), "rb");
        if (!file) fail(path.c_str());
        if (std::fseek(file, 0, SEEK_END)) fail("seek fixture");
        long size = std::ftell(file); std::rewind(file);
        if (size <= 0 || size > 16L * 1024 * 1024) fail("fixture size (max 16 MiB)");
        std::vector<uint8_t> bytes(size);
        if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) fail("read fixture");
        std::fclose(file);
        Memory memory{bytes};
        auto* buffer = static_cast<uint8_t*>(av_malloc(32768));
        if (!buffer) fail("avio allocation");
        AVIOContext* io = avio_alloc_context(buffer, 32768, 0, &memory, Memory::read, nullptr, Memory::seek);
        AVFormatContext* fmt = avformat_alloc_context();
        if (!io || !fmt || !params) fail("demux allocation");
        fmt->pb = io; fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
        int r = avformat_open_input(&fmt, nullptr, av_find_input_format("mov"), nullptr);
        if (r < 0) fail("open MP4", r);
        if (fmt->nb_streams != 1) fail("fixture must contain exactly one video stream");
        auto* stream = fmt->streams[0];
        r = avcodec_parameters_copy(params, stream->codecpar);
        if (r < 0) fail("codec parameters", r);
        timebase = stream->time_base;
        if (params->codec_id != AV_CODEC_ID_H264 || params->width != spec.w || params->height != spec.h)
            fail("fixture codec/dimensions");
        AVPacket* packet = av_packet_alloc();
        if (!packet) fail("packet allocation");
        packets.reserve(spec.frames);
        while ((r = av_read_frame(fmt, packet)) >= 0) {
            double rate = spec.actualFps > 0 ? spec.actualFps : spec.fps;
            // O 137 preservado tem durações quantizadas em 33/34 ms, sem recodificação.
            double tolerance = spec.actualFps > 0 ? .001 : .00001;
            if (std::fabs(packet->duration * av_q2d(timebase) - 1.0 / rate) > tolerance)
                fail("fixture frame rate");
            AVPacket* copy = av_packet_clone(packet);
            if (!copy) fail("packet clone");
            packets.push_back(copy); av_packet_unref(packet);
        }
        if (r != AVERROR_EOF) fail("read packets", r);
        av_packet_free(&packet); avformat_close_input(&fmt);
        av_freep(&io->buffer); avio_context_free(&io);
        if (packets.size() != size_t(spec.frames)) fail("fixture packet count");
        LOG("F30 CLIP name=%s packets=%u bytes=%ld width=%d height=%d fps=%d kbps=%d level=%d", spec.name,
            unsigned(packets.size()), size, spec.w, spec.h, spec.fps, spec.kbps, spec.level);
    }
};
static int getBuffer(AVCodecContext* ctx, AVFrame* f, int flags) {
    int r = avcodec_default_get_buffer2(ctx, f, flags);
    if (r < 0) return r;
    f->opaque_ref = av_buffer_allocz(sizeof(F30Stats));
    if (!f->opaque_ref) return AVERROR(ENOMEM);
    auto* stats = reinterpret_cast<F30Stats*>(f->opaque_ref->data);
    stats->magic = F30_MAGIC; stats->syntax_hash = UINT64_C(14695981039346656037);
    stats->nnz_hash = stats->coeff_hash = stats->prediction_hash = stats->syntax_hash;
    return 0;
}
struct Record {
    int64_t pts = 0;
    F30Stats stats{};
    double at = 0;
    char type = '?';
    std::array<char, 33> pixels{};
};
struct Result {
    std::vector<Record> frames;
    double totalMs = 0;
};
static void pixelMd5(const AVFrame* frame, char* out) {
    auto* md5 = av_md5_alloc(); if (!md5) fail("MD5 allocation");
    av_md5_init(md5);
    for (int p = 0; p < 3; p++) {
        int w = p ? (frame->width + 1) / 2 : frame->width;
        int h = p ? (frame->height + 1) / 2 : frame->height;
        for (int y = 0; y < h; y++) av_md5_update(md5, frame->data[p] + y * frame->linesize[p], w);
    }
    uint8_t digest[16]; av_md5_final(md5, digest); av_free(md5);
    for (int i = 0; i < 16; i++) std::snprintf(out + 2*i, 3, "%02x", digest[i]);
}
static Result decode(const Clip& clip, const Spec& spec, int mode, int threads, bool audit,
                     AVDiscard fullFilter = AVDISCARD_DEFAULT) {
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) fail("H.264 decoder missing");
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    AVFrame* frame = av_frame_alloc();
    if (!ctx || !frame) fail("decoder allocation");
    int r = avcodec_parameters_to_context(ctx, clip.params);
    if (r < 0) fail("decoder parameters", r);
    ctx->thread_count = threads; ctx->thread_type = FF_THREAD_FRAME;
    ctx->pkt_timebase = clip.timebase; ctx->max_pixels = 1920 * 1088;
    ctx->get_buffer2 = getBuffer;
    ctx->skip_loop_filter = mode == 0 ? fullFilter : AVDISCARD_ALL;
    ctx->err_recognition = AV_EF_EXPLODE | AV_EF_CAREFUL;
    for (auto option : {std::pair<const char*, int>{"f30_entropy_only", mode == 2}, {"f30_audit", audit}, {"enable_er", 0}}) {
        r = av_opt_set_int(ctx->priv_data, option.first, option.second, 0);
        if (r < 0) fail(option.first, r);
    }
    ffmpeg_workers(1);
    r = avcodec_open2(ctx, codec, nullptr);
    ffmpeg_workers(0);
    if (r < 0) fail("open decoder", r);
    if (ctx->thread_count != threads || (threads > 1 && ctx->active_thread_type != FF_THREAD_FRAME)) fail("thread configuration");
    Result result; result.frames.reserve(spec.frames);
    auto receive = [&]() {
        int status;
        while ((status = avcodec_receive_frame(ctx, frame)) >= 0) {
            double at = nowMs();
            if (frame->format != AV_PIX_FMT_YUV420P || frame->width != spec.w || frame->height != spec.h ||
                frame->decode_error_flags || (frame->flags & AV_FRAME_FLAG_CORRUPT)) fail("corrupt/unsupported output");
            if (!frame->opaque_ref || frame->opaque_ref->size != sizeof(F30Stats)) fail("missing instrumentation");
            Record record; record.pts = frame->best_effort_timestamp; record.at = at;
            record.stats = *reinterpret_cast<const F30Stats*>(frame->opaque_ref->data);
            record.type = av_get_picture_type_char(frame->pict_type);
            unsigned mbs = unsigned((spec.w+15)/16 * ((spec.h+15)/16));
            if (record.stats.magic != F30_MAGIC || record.stats.parsed != mbs || record.stats.slices != 1 ||
                record.stats.reconstructed != (mode == 2 ? 0 : mbs)) fail("macroblock/slice accounting");
            if (!result.frames.empty() && record.pts <= result.frames.back().pts) fail("PTS order");
            if (audit && mode != 2) pixelMd5(frame, record.pixels.data());
            result.frames.push_back(record); av_frame_unref(frame);
        }
        if (status != AVERROR(EAGAIN) && status != AVERROR_EOF) fail("receive frame", status);
        return status;
    };
    double begin = nowMs();
    for (auto* packet : clip.packets) {
        r = avcodec_send_packet(ctx, packet);
        if (r == AVERROR(EAGAIN)) { receive(); r = avcodec_send_packet(ctx, packet); }
        if (r < 0) fail("send packet", r);
        receive();
    }
    r = avcodec_send_packet(ctx, nullptr);
    if (r < 0) fail("flush decoder", r);
    if (receive() != AVERROR_EOF) fail("flush did not reach EOF");
    result.totalMs = nowMs() - begin;
    if (ctx->profile != AV_PROFILE_H264_HIGH || ctx->level != spec.level) fail("decoded SPS profile/level");
    av_frame_free(&frame); avcodec_free_context(&ctx);
    if (result.frames.size() != size_t(spec.frames)) fail("decoded frame count");
    return result;
}
static double percentile(std::vector<double> values, double quantile) {
    if (values.empty()) fail("no timing samples");
    std::sort(values.begin(), values.end());
    return values[size_t(std::ceil(quantile * values.size())) - 1];
}
static void report(const Result& result, const Spec& spec, int mode, int repeat, int threads) {
    // Aquecimento e cauda (6 workers + reorder B) fora do throughput em regime permanente.
    constexpr size_t WARM = 30, TAIL = 12, BLOCK = 30;
    size_t end = result.frames.size() - TAIL;
    std::vector<double> service, completion, blocks;
    for (size_t i = WARM; i < end; i++) {
        service.push_back(result.frames[i].stats.slice_ns / 1e6);
        completion.push_back(result.frames[i].at - result.frames[i-1].at);
    }
    for (size_t i = WARM; i + BLOCK <= end; i += BLOCK)
        blocks.push_back((result.frames[i+BLOCK-1].at - result.frames[i-1].at) / BLOCK);
    double mean = (result.frames[end-1].at - result.frames[WARM-1].at) / (end - WARM);
    LOG("F30 RESULT platform=%s clip=%s mode=%s repeat=%d threads=%d frames=%u samples=%u "
        "mean_ms=%.6f completion_p50_ms=%.6f completion_p95_ms=%.6f "
        "slice_p50_ms=%.6f slice_p95_ms=%.6f block30_p50_ms=%.6f block30_p95_ms=%.6f "
        "total_ms=%.3f PASS", PLATFORM, spec.name, MODES[mode], repeat, threads,
        unsigned(result.frames.size()), unsigned(service.size()), mean,
        percentile(completion,.50), percentile(completion,.95), percentile(service,.50), percentile(service,.95),
        percentile(blocks,.50), percentile(blocks,.95), result.totalMs);
}
static void trace(const Result& result, const std::string& directory, const Spec& spec, int mode) {
    if (directory.empty()) return;
    std::string path = directory + "/" + spec.name + "-" + MODES[mode] + ".tsv";
    FILE* file = std::fopen(path.c_str(), "w"); if (!file) fail("open trace output");
    std::fprintf(file, "pts\ttype\tparsed\treconstructed\tsyntax_hash\tnnz_hash\tcoeff_hash\tprediction_hash\tpixel_md5\n");
    for (const auto& f : result.frames)
        std::fprintf(file, "%lld\t%c\t%u\t%u\t%016llx\t%016llx\t%016llx\t%016llx\t%s\n", (long long)f.pts, f.type,
            f.stats.parsed, f.stats.reconstructed, (unsigned long long)f.stats.syntax_hash,
            (unsigned long long)f.stats.nnz_hash, (unsigned long long)f.stats.coeff_hash,
            (unsigned long long)f.stats.prediction_hash, f.pixels.data());
    if (std::fclose(file)) fail("close trace output");
}
int main(int argc, char** argv) {
    std::string directory, traceDir;
    int repeats = 3, threads = 6, only = -1;
    bool audit = true;
#ifdef __XSDK__
    directory = "game:\\";
    NTSTATUS placed = xsdk_thread_move(1);
    if (placed < 0) fail("main affinity", int(placed));
#else
    directory = "bench/cpu-floor/media";
#endif
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (!std::strcmp(arg,"--no-audit")) audit = false;
        else if (i+1 < argc && !std::strcmp(arg,"--media")) directory = argv[++i];
        else if (i+1 < argc && !std::strcmp(arg,"--trace")) traceDir = argv[++i];
        else if (i+1 < argc && !std::strcmp(arg,"--repeats")) repeats = std::atoi(argv[++i]);
        else if (i+1 < argc && !std::strcmp(arg,"--threads")) threads = std::atoi(argv[++i]);
        else if (i+1 < argc && !std::strcmp(arg,"--clip")) only = std::atoi(argv[++i]);
        else fail("usage: cpu-floor [--media DIR] [--trace DIR] [--repeats 0..5] [--threads 1..6] [--clip 0..4] [--no-audit]");
    }
    if (repeats < 0 || repeats > 5 || threads < 1 || threads > 6 || only < -1 || only > 4 ||
        (!audit && !repeats)) fail("invalid arguments");
    av_log_set_level(AV_LOG_ERROR);
    LOG("F30 START schema=1 platform=%s ffmpeg=%s threads=%d repeats=%d audit=%d", PLATFORM,
        av_version_info(), threads, repeats, int(audit));
    LOG("F30 CONFIG %s", avcodec_configuration());
    LOG("F30 LICENSE %s", avcodec_license());
    if (std::strcmp(av_version_info(), "7.1.5") || std::strstr(avcodec_configuration(), "--enable-gpl") ||
        !std::strstr(avcodec_license(), "LGPL")) fail("unexpected FFmpeg build");
    unsigned audits = 0, results = 0;
    for (size_t index = 0; index < std::size(SPECS); index++) {
        if (only >= 0 && index != size_t(only)) continue;
        const auto& spec = SPECS[index];
        Clip clip; clip.load(joined(directory, std::string(spec.name) + ".mp4"), spec);
        if (audit) {
            std::vector<Record> expected;
            for (int mode = 0; mode < 3; mode++) {
                Result result = decode(clip, spec, mode, threads, true);
                if (!mode) expected = result.frames;
                trace(result, traceDir, spec, mode);
                for (size_t i = 0; i < expected.size(); i++) {
                    const auto& a = expected[i]; const auto& b = result.frames[i];
                    if (a.pts != b.pts || a.type != b.type || a.stats.syntax_hash != b.stats.syntax_hash ||
                        a.stats.nnz_hash != b.stats.nnz_hash || a.stats.coeff_hash != b.stats.coeff_hash ||
                        a.stats.prediction_hash != b.stats.prediction_hash) {
                        LOG("F30 MISMATCH clip=%s mode=%s frame=%u expected=%016llx actual=%016llx", spec.name,
                            MODES[mode], unsigned(i), (unsigned long long)a.stats.syntax_hash, (unsigned long long)b.stats.syntax_hash);
                        fail("parse audit mismatch");
                    }
                }
                LOG("F30 AUDIT clip=%s mode=%s frames=%u parsed_mb=%u reconstructed_mb=%u syntax_equal=1 PASS",
                    spec.name, MODES[mode], unsigned(result.frames.size()), result.frames[0].stats.parsed,
                    result.frames[0].stats.reconstructed);
                audits++;
            }
        }
        for (int repeat = 0; repeat < repeats; repeat++)
            for (int step = 0; step < 3; step++) {
                int mode = (repeat + step) % 3;
                Result result = decode(clip, spec, mode, threads, false);
                report(result, spec, mode, repeat + 1, threads); results++;
            }
    }
    LOG("F30 DONE PASS audits=%u results=%u", audits, results);
    return 0;
}
