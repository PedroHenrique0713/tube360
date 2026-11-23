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
  h264::Stream s, separate;
  std::unique_ptr<yt::RangeFile> videoFile, audioFile;
  std::unique_ptr<yt::Transport> liveTransport, playlistTransport;
  std::unique_ptr<yt::LiveFile> broadcast;
  struct LiveInput { Player* player; yt::LiveFile* file; std::deque<std::string> audio; } source{this,nullptr,{}};
  yt::PackedAudio packed;
  SDL_Thread* reader = nullptr;
  double origin = 0;
  int r = 0;
  if (direct) {
    if (!youtube) {
      transport = yt::https(&stop);
      youtube = std::make_unique<yt::Client>(*transport, cacheBase);
    }
    if (media.id != id || quality != cachedQuality || SDL_GetTicks() >= validUntil) {
      media = youtube->player(id, quality);
      validUntil = SDL_GetTicks() + 1000ULL * (media.ttl - std::min(30,media.ttl/10));
      cachedQuality = quality;
    }
    if (stop) return;
    if (media.status != "OK") {
      LOG("console: player %s", media.status.c_str()); retryConverted = true; return;
    }
    live = !media.hls.empty() && (media.live || media.video.url.empty() || !media.duration);
    state = BUFFERING;
    mediaReady = true;
    if (live) {
      liveTransport = yt::https(&endLiveReader);
      playlistTransport = yt::https(&endLiveReader);
      broadcast = std::make_unique<yt::LiveFile>(*playlistTransport, *liveTransport, endLiveReader);
      if (!broadcast->open(media.hls, quality)) r = AVERROR_INVALIDDATA;
      else {
        source.file = broadcast.get();
        reader = yt::worker([](void* data) {
          auto& source = *static_cast<LiveInput*>(data);
          auto& p = *source.player;
          moveToHardwareThread(4);
          int result = 0;
          while (!p.stop && !p.endLiveReader && (result = source.file->refill()) >= 0) {
            if (source.file->segments == 1 || source.file->segments % 3 == 0)
              LOG("HLS: %u segmentos, %u conexoes novas, transferencia %.2f Mbit/s",
                  source.file->segments,source.file->connections,source.file->bytes * .008 / std::max<uint64_t>(1,source.file->transferMs));
            if (p.audio && !source.file->sound.empty()) {
              SDL_LockMutex(p.mu);
              while (source.audio.size() >= 3 && !p.stop && !p.endLiveReader) SDL_WaitCondition(p.cv,p.mu);
              SDL_AddAtomicInt(&p.netBytes,int(source.file->sound.size()));
              source.audio.push_back(std::move(source.file->sound));
              SDL_BroadcastCondition(p.cv); SDL_UnlockMutex(p.mu);
            }
            const auto& bytes = source.file->segment;
            p.putBytes(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size());
          }
          SDL_LockMutex(p.mu); p.netEof = true; p.netErr = !p.endLiveReader && result < 0 && result != AVERROR_EOF;
          SDL_BroadcastCondition(p.cv); SDL_UnlockMutex(p.mu);
          return 0;
        }, "hls", &source);
        r = reader ? s.open(this, readOriginal, threads, audio && broadcast->audioPlaylist.empty()) : AVERROR(ENOMEM);
      }
      if (r >= 0 && audio && !broadcast->audioPlaylist.empty()) r = separate.openPackedAudio();
      actualHeight = broadcast->height;
      if (r >= 0) LOG("console: live %s, master %dp %.3f fps", id.c_str(), broadcast->height, broadcast->frameRate);
      origin = NAN;
    } else {
      videoFile = std::make_unique<yt::RangeFile>(yt::https(&stop), media.video.url, &netBytes);
      r = media.video.url.empty() ? AVERROR_STREAM_NOT_FOUND :
          s.open(videoFile.get(), yt::RangeFile::pull, threads, media.muxed && audio, true, yt::RangeFile::seek);
      if (r >= 0) r = s.seekTime(startAt);
      if (r >= 0 && audio && !media.muxed) {
        audioFile = std::make_unique<yt::RangeFile>(yt::https(&stop), media.audio.url, &netBytes);
        r = media.audio.url.empty() ? AVERROR_STREAM_NOT_FOUND :
            separate.open(audioFile.get(), yt::RangeFile::pull, 1, true, false, yt::RangeFile::seek);
        if (r >= 0) r = separate.seekTime(startAt);
      }
      actualHeight = media.video.height;
      origin = startAt;
      if (quality >= 720 && media.highFrameRate720 && media.video.height < 720)
        LOG("console: %s tem 720p60, selecionado %dp %.2f fps", id.c_str(), media.video.height, media.video.fps);
    }
    LOG("console: %s %dp, itags %d/%d, início %.3f", live ? "HLS" : "MP4", actualHeight.load(), media.video.itag, media.audio.itag, startAt);
  } else r = s.open(this, readOriginal, threads, audio != nullptr);
  hasAudio = (s.audio || separate.audio) && r >= 0;
  double last = -1, intervalPts = -1, nextAudioPts = NAN;
  unsigned pauseEpoch = pauseChanges.load();
  int count = 0, slow = 0, packets = 0, decoded = 0, filtered = 0;
  int64_t previousShown = 0, previousLost = 0, phase = AV_NOPTS_VALUE;
  double sourceRate = broadcast ? broadcast->frameRate : 0;
  auto alternate = [&](int64_t pts, AVRational base) {
    if (sourceRate <= 30 || pts == AV_NOPTS_VALUE || phase == AV_NOPTS_VALUE) return false;
    return (std::llround((pts - phase) * av_q2d(base) * sourceRate) & 1) != 0;
  };
  uint64_t ns = 0, since = SDL_GetTicks();
  auto decode = [&](h264::Stream& input, AVCodecContext* codec, AVPacket* packet) -> int {
    if (!codec) return 0;
    bool video = codec == s.video;
    if (video && broadcast && packet) {
      ++packets;
      AVRational base = input.fmt->streams[input.vi]->time_base;
      if (packet->duration > 0) {
        double measured = 1.0 / (packet->duration * av_q2d(base));
        if (sourceRate <= 30 && measured > 30.1 && measured <= 60.1) sourceRate = measured;
      }
      if (phase == AV_NOPTS_VALUE) phase = packet->pts;
      // Um quadro referencial permanece no DPB mesmo quando a TV não precisa exibi-lo.
      codec->skip_frame = alternate(packet->pts, base) ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
    }
    uint64_t begin = SDL_GetTicksNS();
    int err = avcodec_send_packet(codec, packet);
    if (video) ns += SDL_GetTicksNS() - begin;
    if (err < 0) return err;
    while (!stop) {
      begin = SDL_GetTicksNS();
      err = avcodec_receive_frame(codec, input.frame);
      if (video) ns += SDL_GetTicksNS() - begin;
      if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) return 0;
      if (err < 0) return err;
      AVFrame* f = input.frame;
      double pts = input.pts(video ? input.vi : input.ai);
      // Um PES pode carregar vários AAC com PTS só no primeiro quadro.
      if (!video && !std::isfinite(pts)) pts = nextAudioPts;
      if (!video && f->sample_rate > 0) nextAudioPts = pts + double(f->nb_samples) / f->sample_rate;
      if (!std::isfinite(origin)) origin = pts;
      pts -= origin;
      if (live && direct) pts = std::remainder(pts, double(1LL<<33)/90000);
      if (!std::isfinite(pts) || f->decode_error_flags) {
        LOG("decod: quadro %s invalido, pts %.3f, flags %d", video ? "AVC" : "AAC", pts, f->decode_error_flags);
        return AVERROR_INVALIDDATA;
      }
      if (video) {
        if (f->width > 1280 || f->height > 720 || (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P))
          return AVERROR_INVALIDDATA;
        ++decoded;
        if (broadcast && (alternate(f->best_effort_timestamp, input.fmt->streams[input.vi]->time_base) ||
                          (last >= 0 && pts - last < 1.0/30 - 0.00005))) { ++filtered; continue; }
        if (pts < -0.0001) continue;
        pts = std::max(0.0, pts);
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
        if (!putOriginalAudio(input, input.convert(f), pts)) return AVERROR_INVALIDDATA;
      }
      trimReserve();
    }
    return AVERROR_EXIT;
  };
  bool videoEnd = false, audioEnd = !separate.audio;
  while (r >= 0 && !stop) {
    bool readAudio = !audioEnd && (videoEnd || audioQueuedSec() < 0.65);
    if (readAudio && broadcast && !videoEnd && packed.cursor == packed.bytes.size()) {
      SDL_LockMutex(mu);
      // O produtor pode estar enchendo o anel de vídeo antes de chegar ao próximo AAC.
      if (source.audio.empty() && rcount) readAudio = false;
      SDL_UnlockMutex(mu);
    }
    h264::Stream& input = readAudio ? separate : s;
    if (direct && (media.ttl || media.expiresAt) && SDL_GetTicks() >= validUntil) { r = AVERROR(ETIMEDOUT); break; }
    if (readAudio && broadcast) {
      if (packed.cursor == packed.bytes.size()) {
        SDL_LockMutex(mu);
        while (source.audio.empty() && !stop && !netEof && (videoEnd || !rcount)) SDL_WaitCondition(cv,mu);
        if (source.audio.empty() && !videoEnd && rcount && !stop) { SDL_UnlockMutex(mu); continue; }
        if (source.audio.empty()) r = netErr ? AVERROR(EIO) : AVERROR_EOF;
        else {
          auto segment = std::move(source.audio.front()); source.audio.pop_front();
          r = packed.begin(std::move(segment)) ? 0 : AVERROR_INVALIDDATA;
        }
        SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
      }
      if (r >= 0) r = packed.packet(input.pkt);
    } else r = av_read_frame(input.fmt, input.pkt);
    if (r == AVERROR_EOF && (!input.io || !input.io->error || input.io->error == AVERROR_EOF)) {
      r = decode(input, input.video, nullptr);
      if (r >= 0) r = decode(input, input.audio, nullptr);
      if (r >= 0 && input.audio && !putOriginalAudio(input, input.convert(nullptr), last)) r = AVERROR_INVALIDDATA;
      if (readAudio) audioEnd = true; else videoEnd = true;
      if (videoEnd && audioEnd) {
        double expected = direct ? media.duration : duration;
        if (last < 0 || (!live && expected > 0 && startAt + last + 1.25 < expected)) r = AVERROR_INVALIDDATA;
        break;
      }
    } else {
      if (r < 0) break;
      if (input.pkt->stream_index == input.vi) r = decode(input, input.video, input.pkt);
      else if (input.pkt->stream_index == input.ai) r = decode(input, input.audio, input.pkt);
      av_packet_unref(input.pkt);
    }
    uint64_t now = SDL_GetTicks();
    if (now - since >= 5000) {
      SDL_LockMutex(mu);
      int minimum = queueMin; queueMin = (int)frames.size(); long long lost = droppedFrames, shown = shownFrames;
      SDL_UnlockMutex(mu);
      double avg = count ? ns / 1e6 / count : 0.0;
      LOG("decod: H.264 %dt, %d quadros, %.2f ms/quadro, fila mín %d, perdidos %lld, PCM %.0f ms, rede %.2f Mbit/s",
          threads, count, avg, minimum, lost, audioQueuedSec() * 1000, SDL_SetAtomicInt(&netBytes, 0) * 0.008 / (now - since));
      double displayRate = (shown - previousShown) * 1000.0 / (now - since);
      if (broadcast) LOG("live: %.1f fps na TV, pacotes %d, decodificados %d, filtrados %d", displayRate, packets, decoded, filtered);
      int clock = masterMs.load();
      unsigned epoch = pauseChanges.load();
      bool behind = clock >= 0 && last < clock / 1000.0 - .25;
      bool insufficient = intervalPts >= 0 && last - intervalPts < (now - since) * .00090;
      bool losing = broadcast && lost - previousLost > 10 && displayRate < std::min(30.0, sourceRate) * .85;
      slow = !paused && pauseEpoch == epoch && ((minimum < 2 && (behind || insufficient)) || losing) ? slow + 1 : 0;
      previousShown = shown; previousLost = lost;
      intervalPts = last; pauseEpoch = epoch;
      if (slow >= 2 && (!direct || (net::serverEnabled && (broadcast || avg > 33.34)))) { r = AVERROR(ETIMEDOUT); break; }
      since = now; count = packets = decoded = filtered = 0; ns = 0;
    }
  }
  if (reader) {
    endLiveReader = true;
    SDL_LockMutex(mu); SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
    SDL_WaitThread(reader,nullptr);
    if (netErr && r >= 0) r = AVERROR(EIO);
  }
  if (stop) return;
  if (direct && (((media.ttl || media.expiresAt) && SDL_GetTicks() >= validUntil) || (videoFile && videoFile->status == 403) || (audioFile && audioFile->status == 403) ||
                 (broadcast && broadcast->status == 403))) { renew = true; return; }
  if (r < 0) { LOG("decod: falha H.264 %d, último PTS %.3f", r, last); retryConverted = true; }
  else { SDL_LockMutex(mu); decEnded = true; SDL_UnlockMutex(mu); }
}

}  // namespace player
