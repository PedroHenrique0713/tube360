// Implementação de Player para o stream negociado com codec=h264.
#pragma once
namespace player {

inline int Player::readOriginal(void* user, uint8_t* dst, int size) {
  Player& p = *(Player*)user;
  SDL_LockMutex(p.mu);
  while (!p.rcount && !p.stop && !p.netEof && !p.netErr) SDL_WaitCondition(p.cv, p.mu);
  size_t got = std::min({p.rcount, size_t(size), p.ring.size() - p.rhead});
  if (got) {
    std::memcpy(dst, p.ring.data() + p.rhead, got);
    p.rhead = (p.rhead + got) % p.ring.size(); p.rcount -= got;
    SDL_BroadcastCondition(p.cv);
  }
  SDL_UnlockMutex(p.mu);
  return p.stop ? AVERROR_EXIT : got ? (int)got : AVERROR_EOF;
}

inline bool Player::putOriginalAudio(h264::Stream& s, int count, double pts) {
  if (count < 0 || !std::isfinite(pts)) return false;
  int skip = (int)std::min<double>(count, std::max(0.0, std::ceil(-pts * 44100)));
  if (skip == count) return true;
  SDL_LockAudioStream(audio);
  bool ok = SDL_PutAudioStreamData(audio, s.pcm.data() + 2 * skip, (count - skip) * 2 * sizeof(float));
  if (ok) {
    if (SDL_GetAtomicInt(&audioBaseMs) < 0)
      SDL_SetAtomicInt(&audioBaseMs, (int)std::lround((pts + skip / 44100.0) * 1000));
    SDL_AddAtomicInt(&pushed, count - skip);
  }
  SDL_UnlockAudioStream(audio);
  return ok;
}

inline void Player::decodeOriginal() {
  int threads = 6;
#ifndef __XSDK__
  if (const char* v = std::getenv("XT_H264_THREADS")) threads = std::clamp(std::atoi(v), 4, 6);
#endif
  h264::Stream s;
  int r = s.open(this, readOriginal, threads, audio != nullptr);
  hasAudio = s.audio && r >= 0;
  double last = -1, intervalPts = -1;
  unsigned pauseEpoch = pauseChanges.load();
  int count = 0, slow = 0;
  uint64_t ns = 0, since = SDL_GetTicks();
  auto decode = [&](AVCodecContext* codec, AVPacket* packet) -> int {
    if (!codec) return 0;
    bool video = codec == s.video;
    uint64_t begin = SDL_GetTicksNS();
    int err = avcodec_send_packet(codec, packet);
    if (video) ns += SDL_GetTicksNS() - begin;
    if (err < 0) return err;
    while (!stop) {
      begin = SDL_GetTicksNS();
      err = avcodec_receive_frame(codec, s.frame);
      if (video) ns += SDL_GetTicksNS() - begin;
      if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) return 0;
      if (err < 0) return err;
      AVFrame* f = s.frame;
      double pts = s.pts(video ? s.vi : s.ai);
      if (!std::isfinite(pts) || f->decode_error_flags) return AVERROR_INVALIDDATA;
      if (video) {
        if (f->width > 1280 || f->height > 720 || (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P))
          return AVERROR_INVALIDDATA;
        if (pts < 0) continue;
        SDL_LockMutex(mu);
        while ((int)frames.size() >= maxFrames && !stop) SDL_WaitCondition(cv, mu);
        SDL_UnlockMutex(mu);
        if (stop) return AVERROR_EXIT;
        bool bt709 = f->colorspace == AVCOL_SPC_BT709 || (f->colorspace == AVCOL_SPC_UNSPECIFIED && f->height > 576);
        begin = SDL_GetTicksNS();
        enqueuePlanes(f->width, f->height, pts, f->data, f->linesize, bt709,
                      f->color_range == AVCOL_RANGE_JPEG || f->format == AV_PIX_FMT_YUVJ420P);
        ns += SDL_GetTicksNS() - begin; last = pts; count++;
      } else {
        // Limita AAC adiantado inclusive quando a pessoa deixa um VOD pausado.
        while (!stop && audioQueuedSec() > 2.0) { trimReserve(); SDL_Delay(8); }
        if (!putOriginalAudio(s, s.convert(f), pts)) return AVERROR_INVALIDDATA;
      }
      trimReserve();
    }
    return AVERROR_EXIT;
  };
  while (r >= 0 && !stop) {
    r = av_read_frame(s.fmt, s.pkt);
    if (r == AVERROR_EOF) {
      r = decode(s.video, nullptr);
      if (r >= 0) r = decode(s.audio, nullptr);
      if (r >= 0 && hasAudio && !putOriginalAudio(s, s.convert(nullptr), last)) r = AVERROR_INVALIDDATA;
      if (last < 0 || (duration > 0 && startAt + last + 1.25 < duration)) r = AVERROR_INVALIDDATA;
      break;
    }
    if (r < 0) break;
    if (s.pkt->stream_index == s.vi) r = decode(s.video, s.pkt);
    else if (s.pkt->stream_index == s.ai) r = decode(s.audio, s.pkt);
    av_packet_unref(s.pkt);
    uint64_t now = SDL_GetTicks();
    if (now - since >= 5000) {
      SDL_LockMutex(mu);
      int minimum = queueMin; queueMin = (int)frames.size(); long long lost = droppedFrames;
      SDL_UnlockMutex(mu);
      double avg = count ? ns / 1e6 / count : 0.0;
      LOG("decod: H.264 %dt, %d quadros, %.2f ms/quadro, fila mín %d, perdidos %lld, PCM %.0f ms",
          threads, count, avg, minimum, lost, audioQueuedSec() * 1000);
      int clock = masterMs.load();
      unsigned epoch = pauseChanges.load();
      bool behind = clock >= 0 && last < clock / 1000.0 - .25;
      bool insufficient = intervalPts >= 0 && last - intervalPts < (now - since) * .00090;
      slow = !paused && pauseEpoch == epoch && minimum < 2 && (behind || insufficient) ? slow + 1 : 0;
      intervalPts = last; pauseEpoch = epoch;
      if (slow >= 2) { r = AVERROR(ETIMEDOUT); break; }
      since = now; count = 0; ns = 0;
    }
  }
  if (stop) return;
  if (r < 0) { LOG("decod: falha H.264 %d, último PTS %.3f", r, last); retryConverted = true; }
  else { SDL_LockMutex(mu); decEnded = true; SDL_UnlockMutex(mu); }
}

}  // namespace player
