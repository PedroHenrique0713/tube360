// MPEG-TS chega pelo anel do player. Os dois codecs compartilham o mesmo cursor e a mesma origem de PTS.
#pragma once
#include <cmath>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

namespace h264 {
#ifdef __XSDK__
extern "C" void ffmpeg_workers(int active);
#else
inline void ffmpeg_workers(int) {}
#endif

struct Stream {
  AVFormatContext* fmt = avformat_alloc_context();
  AVIOContext* io = nullptr;
  AVCodecContext *video = nullptr, *audio = nullptr;
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  SwrContext* swr = nullptr;
  AVChannelLayout layout{};
  int inputRate = 0, inputFormat = -1;
  std::vector<float> pcm;
  Stream() = default;
  Stream(const Stream&) = delete;
  Stream& operator=(const Stream&) = delete;
  int vi = -1, ai = -1;
  ~Stream() {
    av_channel_layout_uninit(&layout);
    swr_free(&swr);
    av_frame_free(&frame); av_packet_free(&pkt);
    avcodec_free_context(&audio); avcodec_free_context(&video);
    avformat_close_input(&fmt);
    if (io) av_freep(&io->buffer);
    avio_context_free(&io);
  }

  int track(int index, AVCodecID id, AVCodecContext*& ctx, int threads) {
    auto* stream = fmt->streams[index];
    if (stream->codecpar->codec_id != id) return AVERROR_INVALIDDATA;
    const AVCodec* codec = avcodec_find_decoder(id);
    if (!codec) return AVERROR_DECODER_NOT_FOUND;
    ctx = avcodec_alloc_context3(codec);
    if (!ctx) return AVERROR(ENOMEM);
    int r = avcodec_parameters_to_context(ctx, stream->codecpar);
    if (r < 0) return r;
    ctx->pkt_timebase = stream->time_base;
    ctx->thread_count = threads;
    ctx->thread_type = FF_THREAD_FRAME;
    ctx->max_pixels = 1280 * 736;  // H.264 pode completar o último macrobloco.
    ffmpeg_workers(id == AV_CODEC_ID_H264);
    r = avcodec_open2(ctx, codec, nullptr);
    ffmpeg_workers(0);
    return r;
  }

  int open(void* user, int (*read)(void*, uint8_t*, int), int threads, bool wantAudio) {
    if (!fmt || !pkt || !frame) return AVERROR(ENOMEM);
    auto* buffer = (uint8_t*)av_malloc(32768);
    if (!buffer) return AVERROR(ENOMEM);
    io = avio_alloc_context(buffer, 32768, 0, user, read, nullptr, nullptr);
    if (!io) { av_free(buffer); return AVERROR(ENOMEM); }
    fmt->pb = io;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    // PAT/PMT identificam os codecs. find_stream_info abriria outro H.264 só para sondagem.
    int r = avformat_open_input(&fmt, nullptr, av_find_input_format("mpegts"), nullptr);
    if (r < 0) return r;
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
      auto id = fmt->streams[i]->codecpar->codec_id;
      if (id == AV_CODEC_ID_H264) vi = (int)i;
      if (id == AV_CODEC_ID_AAC) ai = (int)i;
    }
    if (vi < 0 || (wantAudio && ai < 0)) return AVERROR_STREAM_NOT_FOUND;
    if ((r = track(vi, AV_CODEC_ID_H264, video, threads)) < 0) return r;
    return wantAudio ? track(ai, AV_CODEC_ID_AAC, audio, 1) : 0;
  }

  double pts(int stream) const {
    int64_t ts = frame->best_effort_timestamp;
    // Na borda do seek os PTS negativos do pré-roll podem vir módulo 2^33 no TS.
    if (ts == AV_NOPTS_VALUE) return NAN;
    if (ts > (1LL << 32)) ts -= 1LL << 33;
    return ts * av_q2d(fmt->streams[stream]->time_base);
  }

  int convert(const AVFrame* f) {
    int r = 0;
    if (!swr) {
      if (!f) return 0;
      inputRate = f->sample_rate; inputFormat = f->format;
      if ((r = av_channel_layout_copy(&layout, &f->ch_layout)) < 0) return r;
      AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
      r = swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, 44100, &f->ch_layout,
                              (AVSampleFormat)f->format, f->sample_rate, 0, nullptr);
      if (r < 0 || (r = swr_init(swr)) < 0) return r;
    }
    if (f && (f->format != inputFormat || f->sample_rate != inputRate || av_channel_layout_compare(&layout, &f->ch_layout)))
      return AVERROR_INPUT_CHANGED;
    int count = f ? f->nb_samples : 0, capacity = swr_get_out_samples(swr, count);
    if (capacity < 0) return capacity;
    pcm.resize(size_t(capacity) * 2);
    uint8_t* out = (uint8_t*)pcm.data();
    return swr_convert(swr, &out, capacity, f ? (const uint8_t**)f->extended_data : nullptr, count);
  }
};
}
