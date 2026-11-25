// MPEG-TS chega pelo anel do player. Os dois codecs compartilham o mesmo cursor e a mesma origem de PTS.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>
#include "decoded_video.hpp"
#include "h264_policy.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

namespace h264 {
inline char mappedFrameTag;
struct MappedOwner {
  std::shared_ptr<video::Surface> surface;
  std::atomic<int> references{1};
  explicit MappedOwner(std::shared_ptr<video::Surface> value) : surface(std::move(value)) {}
  void release() { if (references.fetch_sub(1,std::memory_order_acq_rel)==1) delete this; }
};
inline int mappedBuffer(AVCodecContext* ctx, AVFrame* frame, int flags) {
  frame->opaque = nullptr;
  if (!ctx->opaque || (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P) ||
      !(ctx->codec->capabilities & AV_CODEC_CAP_DR1)) return avcodec_default_get_buffer2(ctx,frame,flags);
  int w = frame->width, h = frame->height, alignment[AV_NUM_DATA_POINTERS] = {};
  avcodec_align_dimensions2(ctx,&w,&h,alignment);
  int visibleW = ctx->width > 0 ? std::min(ctx->width,frame->width) : frame->width;
  int visibleH = ctx->height > 0 ? std::min(ctx->height,frame->height) : frame->height;
  auto surface = static_cast<video::Allocator*>(ctx->opaque)->acquire(visibleW,visibleH,w,h,alignment);
  if (!surface) return avcodec_default_get_buffer2(ctx,frame,flags);
  auto* owner = new MappedOwner(std::move(surface));
  // buf[] exige que cada data[] pertença ao intervalo da sua alocação Vulkan.
  for (int i = 0; i < 3; ++i) {
    size_t bytes = size_t(owner->surface->stride[i]) * (i ? (h+1)/2 : h) + 64;
    owner->references.fetch_add(1,std::memory_order_relaxed);
    frame->buf[i] = av_buffer_create(owner->surface->data[i],bytes,[](void* p,uint8_t*) {
      static_cast<MappedOwner*>(p)->release();
    },owner,0);
    if (!frame->buf[i]) { owner->release(); av_frame_unref(frame); owner->release(); return AVERROR(ENOMEM); }
    frame->data[i] = owner->surface->data[i]; frame->linesize[i] = owner->surface->stride[i];
  }
  owner->release();
  frame->extended_data = frame->data;
  frame->opaque = &mappedFrameTag;
  return 0;
}
// O buffer padrão também tem opaque; só o quadro marcado pertence ao pool Vulkan.
inline std::shared_ptr<video::Surface> mappedSurface(const AVFrame* frame) {
  if (frame->opaque != &mappedFrameTag || !frame->buf[0]) return {};
  auto* owner = static_cast<MappedOwner*>(av_buffer_get_opaque(frame->buf[0]));
  if (!owner) return {};
  for (int i = 0; i < 3; ++i)
    if (frame->data[i] != owner->surface->data[i] || frame->linesize[i] != owner->surface->stride[i]) return {};
  return owner->surface;
}
inline bool dropAtRate(int64_t frame, double rate, double target) {
  if (rate <= target+.1 || !std::isfinite(rate)) return false;
  if (target==30 && rate >= 59 && rate <= 60.1) return (frame & 1) != 0;
  return std::floor(frame * target / rate + .00001) == std::floor((frame - 1) * target / rate + .00001);
}
inline bool dropAt30(int64_t frame,double rate) { return dropAtRate(frame,rate,30); }
#ifdef __XSDK__
extern "C" void ffmpeg_workers(int active);
extern "C" void ffmpeg_work_metrics(unsigned* us, unsigned* frames);
#else
inline void ffmpeg_workers(int) {}
inline void ffmpeg_work_metrics(unsigned* us,unsigned* frames) { *us = *frames = 0; }
#endif

struct Stream {
  video::Allocator* videoBuffers = nullptr;
  int threadType = FF_THREAD_FRAME;
  int sourceHeight = 0;
  double sourceRate = 0;
  bool fullLoopFilter = false;
  bool enable1080Profile = false;
  playback::H264Policy policy{4,false,false};
  void source(int height,double fps,bool full) { sourceHeight=height;sourceRate=fps;fullLoopFilter=full;enable1080Profile=true; }
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
  void close() {
    av_channel_layout_uninit(&layout);
    swr_free(&swr);
    av_frame_free(&frame); av_packet_free(&pkt);
    avcodec_free_context(&audio); avcodec_free_context(&video);
    avformat_close_input(&fmt);
    if (io) av_freep(&io->buffer);
    avio_context_free(&io);
    vi = ai = -1; inputRate = 0; inputFormat = -1;
  }
  ~Stream() { close(); }

  void reset() {
    close();
    fmt = avformat_alloc_context(); pkt = av_packet_alloc(); frame = av_frame_alloc();
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
    if (id==AV_CODEC_ID_H264) {
      int height=ctx->height>0 ? ctx->height : sourceHeight;
      double fps=av_q2d(stream->avg_frame_rate);
      if (!(fps>0)) fps=sourceRate;
      policy=enable1080Profile ? playback::h264Policy(height,fps,fullLoopFilter,threads) : playback::H264Policy{threads,false,false};
      threads=policy.threads;
      ctx->skip_loop_filter=policy.skipLoop ? AVDISCARD_ALL : AVDISCARD_DEFAULT;
    }
    ctx->thread_count = threads;
    ctx->thread_type = id==AV_CODEC_ID_H264 && policy.spread ? FF_THREAD_FRAME : threadType;
    if (id == AV_CODEC_ID_H264 && videoBuffers) { ctx->opaque = videoBuffers; ctx->get_buffer2 = mappedBuffer; }
    ctx->max_pixels = 1920 * 1088;  // 1080p completa o último macrobloco; a saída da TV permanece 720p.
    ffmpeg_workers(id==AV_CODEC_ID_H264 ? (policy.spread ? 2 : 1) : 0);
    r = avcodec_open2(ctx, codec, nullptr);
    ffmpeg_workers(0);
    return r;
  }

  int open(void* user, int (*read)(void*, uint8_t*, int), int threads, bool wantAudio, bool wantVideo = true,
           int64_t (*seek)(void*,int64_t,int) = nullptr) {
    if (!fmt || !pkt || !frame) return AVERROR(ENOMEM);
    auto* buffer = (uint8_t*)av_malloc(32768);
    if (!buffer) return AVERROR(ENOMEM);
    io = avio_alloc_context(buffer, 32768, 0, user, read, nullptr, seek);
    if (!io) { av_free(buffer); return AVERROR(ENOMEM); }
    fmt->pb = io;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    // PAT/PMT identificam os codecs. find_stream_info abriria outro H.264 só para sondagem.
    int r = avformat_open_input(&fmt, nullptr, av_find_input_format(seek ? "mov" : "mpegts"), nullptr);
    if (r < 0) return r;
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
      auto id = fmt->streams[i]->codecpar->codec_id;
      if (id == AV_CODEC_ID_H264) vi = (int)i;
      if (id == AV_CODEC_ID_AAC) ai = (int)i;
    }
    if ((wantVideo && vi < 0) || (wantAudio && ai < 0)) return AVERROR_STREAM_NOT_FOUND;
    if (wantVideo && (r = track(vi, AV_CODEC_ID_H264, video, threads)) < 0) return r;
    return wantAudio ? track(ai, AV_CODEC_ID_AAC, audio, 1) : 0;
  }

  int openPackedAudio() {
    if (!fmt || !pkt || !frame) return AVERROR(ENOMEM);
    auto* stream = avformat_new_stream(fmt,nullptr);
    if (!stream) return AVERROR(ENOMEM);
    stream->time_base = {1,90000}; stream->codecpar->codec_id = AV_CODEC_ID_AAC;
    ai = stream->index;
    return track(ai,AV_CODEC_ID_AAC,audio,1);
  }

  int seekTime(double seconds) {
    if (seconds <= 0) return 0;
    int index = video ? vi : ai;
    return av_seek_frame(fmt, index, int64_t(seconds / av_q2d(fmt->streams[index]->time_base)), AVSEEK_FLAG_BACKWARD);
  }

  double pts(int stream) const {
    int64_t ts = frame->best_effort_timestamp;
    // Na borda do seek os PTS negativos do pré-roll podem vir módulo 2^33 no TS.
    if (ts == AV_NOPTS_VALUE) return NAN;
    if ((!io || !io->seekable) && ts > (1LL << 32)) ts -= 1LL << 33;
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
