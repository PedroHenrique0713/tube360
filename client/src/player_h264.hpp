// Implementação de Player para o stream negociado com codec=h264.
#pragma once
#include "quality_recovery.hpp"
namespace player {

inline bool Player::directFailed(bool fatal) {
  // Esta sonda roda no worker, depois das opções locais. Nunca bloqueia o desenho nem acorda o PC.
  net::Response response;
  directReserveReady = !stop && net::serverEnabled && srv.ip != "0.0.0.0" &&
      net::get(srv,"/api/remote/state",response,700,8192);
  if (!stop) {
    LOG("console: opcoes diretas esgotadas, servidor %s", directReserveReady ? "respondendo" : "indisponivel");
    if (fatal || directReserveReady) retryConverted = true;
  }
  return directReserveReady;
}

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
  return putAudio(s.pcm.data()+2*skip,count-skip,pts+skip/44100.0);
}

inline void Player::decodeOriginal() {
  moveToHardwareThread(1);
  int threads = h264Threads;
#ifndef __XSDK__
  if (const char* v = std::getenv("XT_H264_THREADS")) threads = std::clamp(std::atoi(v), 1, 6);
  if (const char* v = std::getenv("XT_H264_THREAD_TYPE")) h264ThreadType = std::atoi(v) == 2 ? FF_THREAD_SLICE : FF_THREAD_FRAME;
  if (const char* v = std::getenv("XT_H264_LOOP_FILTER")) h264LoopFilter = std::atoi(v);
#endif
  h264::Stream s, separate;
  s.videoBuffers = videoBuffers; s.threadType = h264ThreadType;
  unsigned workUs = 0, workFrames = 0;
  h264::ffmpeg_work_metrics(&workUs,&workFrames);
  std::unique_ptr<yt::RangeFile> videoFile, audioFile;
  std::vector<yt::Format> qualityLadder;
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
    if (media.id != id || quality != cachedQuality || videoMode != cachedVideoMode || cachedAudio != audioSelection || SDL_GetTicks() >= validUntil) {
      youtube->setLanguage(requestLanguage);
      media = youtube->player(id, quality, nullptr,videoMode,audioSelection,memberResponse());
      validUntil = SDL_GetTicks() + 1000ULL * (media.ttl - std::min(30,media.ttl/10));
      cachedQuality = quality;
      cachedVideoMode=videoMode; cachedAudio=audioSelection;
    }
    if (stop) return;
    if (media.status == "ERROR" && !stop && (media.failure==yt::Failure::NETWORK || media.failure==yt::Failure::SESSION)) {
      // Falha de rede ou sessão (não é recusa do YouTube): tenta de novo pelo console com conexão e sessão novas.
      LOG("console: player ERROR: %s; nova tentativa direta", media.reason.c_str());
      youtube.reset(); transport.reset();
      transport = yt::https(&stop);
      youtube = std::make_unique<yt::Client>(*transport, cacheBase);
      youtube->setLanguage(requestLanguage);
      media = youtube->player(id, quality, nullptr,videoMode,audioSelection,memberResponse());
      validUntil = SDL_GetTicks() + 1000ULL * (media.ttl - std::min(30,media.ttl/10));
    }
    if (stop) return;
    if (media.status != "OK") {
      LOG("console: player %s, classe %s, cliente %s, %u pedidos: %s", media.status.c_str(), yt::failureName(media.failure),
          media.playerClient.c_str(), media.playerAttempts, media.reason.c_str());
      if (media.reason == "members") { error = "Vídeo só para membros do canal"; state = FAILED; return; }
      directFailed(); return;
    }
    LOG("console: player OK, cliente %s, %u pedidos: %s",media.playerClient.c_str(),media.playerAttempts,media.playerTrace.c_str());
    live = !media.hls.empty() && (media.live || media.video.url.empty() || !media.duration);
    if (live) { speed=1; loop=false; tempo.reset(1); }
    castMediaReady = true;
    state = BUFFERING;
    mediaReady = true;
    if (live) {
      liveTransport = yt::https(&endLiveReader);
      playlistTransport = yt::https(&endLiveReader);
      broadcast = std::make_unique<yt::LiveFile>(*playlistTransport, *liveTransport, endLiveReader);
      if (!broadcast->open(media.hls, quality,videoMode==playback::FAST,media.selectedAudio.empty() ? audioSelection : media.selectedAudio)) r = AVERROR_INVALIDDATA;
      else {
        source.file = broadcast.get();
        reader = yt::worker([](void* data) {
          auto& source = *static_cast<LiveInput*>(data);
          auto& p = *source.player;
          moveToHardwareThread(1);
          int result = 0;
          std::deque<uint64_t> starts;
          uint64_t written = 0;
          while (!p.stop && !p.endLiveReader && (result = source.file->refill()) >= 0) {
            if (p.audio && !source.file->sound.empty()) {
              SDL_LockMutex(p.mu);
              while (source.audio.size() >= 3 && !p.stop && !p.endLiveReader) SDL_WaitCondition(p.cv,p.mu);
              SDL_AddAtomicInt(&p.netBytes,int(source.file->sound.size()));
              source.audio.push_back(std::move(source.file->sound));
              SDL_BroadcastCondition(p.cv); SDL_UnlockMutex(p.mu);
            }
            const auto& bytes = source.file->segment;
            p.putBytes(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size());
            if (p.endLiveReader) break;
            starts.push_back(written); written += bytes.size();
            SDL_LockMutex(p.mu); size_t buffered = p.rcount; SDL_UnlockMutex(p.mu);
            while (!starts.empty() && starts.front() < written - buffered) starts.pop_front();
            if (source.file->segments == 1 || source.file->segments % 3 == 0)
              LOG("HLS: %u segmentos, %u conexoes novas, transferencia %.2f Mbit/s, reserva %u segmentos completos",
                  source.file->segments,source.file->connections,source.file->bytes * .008 / std::max<uint64_t>(1,source.file->transferMs),
                  unsigned(starts.size()));
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
      // A lista original sobrevive ao cache de Media: seek/pausa não podem apagar o caminho de volta.
      qualityLadder=media.videoLadder;
      if(qualityLadder.empty()) {qualityLadder.push_back(media.video);qualityLadder.insert(qualityLadder.end(),media.lowerVideo.begin(),media.lowerVideo.end());}
      videoFile = std::make_unique<yt::RangeFile>(yt::https(&stop), media.video.url, &netBytes, yt::https(&stop));
      videoFile->aheadEnabled=false;
      r = media.video.url.empty() ? AVERROR_STREAM_NOT_FOUND :
          s.open(videoFile.get(), yt::RangeFile::pull, threads, media.muxed && audio, true, yt::RangeFile::seek);
      if (r >= 0) r = s.seekTime(startAt);
      videoFile->aheadEnabled=true;
      while (r<0 && !stop && !media.muxed && videoFile->status!=403 && !media.lowerVideo.empty()) {
        auto next=media.lowerVideo.front(); media.lowerVideo.erase(media.lowerVideo.begin());
        LOG("qualidade: abertura direta %dp%.3f -> %dp%.3f, motivo=abertura anterior falhou",media.video.height,media.video.fps,next.height,next.fps);
        s.reset(); videoFile=std::make_unique<yt::RangeFile>(yt::https(&stop),next.url,&netBytes,yt::https(&stop));
        videoFile->aheadEnabled=false;
        r=s.open(videoFile.get(),yt::RangeFile::pull,threads,false,true,yt::RangeFile::seek);
        if (r>=0) r=s.seekTime(startAt);
        videoFile->aheadEnabled=true;
        media.video=next; presentFps=30; adapted=true;
      }
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
    audioTracks=broadcast ? broadcast->audioTracks : media.audioTracks;
    selectedAudio=broadcast ? broadcast->selectedAudio : media.selectedAudio;
    if (broadcast) for (auto& track : audioTracks) {
      auto found=std::find_if(media.audioTracks.begin(),media.audioTracks.end(),[&](const yt::AudioTrack& t){ return t.language==track.language; });
      if (found!=media.audioTracks.end() && std::count_if(media.audioTracks.begin(),media.audioTracks.end(),[&](const yt::AudioTrack& t){ return t.language==track.language; })==1) {
        if (track.code==selectedAudio) selectedAudio=found->code;
        track.code=found->code;track.original=found->original;track.automatic=found->automatic;
      }
    }
    LOG("audio: selecionada %s, %u faixas",selectedAudio.empty() ? "padrao do stream" : selectedAudio.c_str(),(unsigned)audioTracks.size());
    tracksReady=true;
  } else r = s.open(this, readOriginal, threads, audio != nullptr);
  hasAudio = (s.audio || separate.audio) && r >= 0;
  double last = -1, intervalPts = -1, nextAudioPts = NAN;
  unsigned pauseEpoch = pauseChanges.load();
  int count = 0, slow = 0, packets = 0, decoded = 0, filtered = 0;
  int64_t previousShown = 0, previousLost = 0, phase = AV_NOPTS_VALUE;
  double sourceRate = broadcast ? broadcast->frameRate : direct ? media.video.fps : sourceFpsHint;
  if (r >= 0 && s.video && sourceRate <= 0) sourceRate = av_q2d(s.fmt->streams[s.vi]->avg_frame_rate);
  if (!std::isfinite(sourceRate) || sourceRate < 0) sourceRate = 0;
  if (s.video) LOG("decod-config: threads %d, tipo solicitado %d ativo %d, buffers %s, loop %d",s.video->thread_count,
                 s.video->thread_type,s.video->active_thread_type,videoBuffers ? "diretos" : "copiados",h264LoopFilter);
  if (sourceRate > 0) presentFps=std::min(presentFps.load(),std::max(1,(int)std::lround(sourceRate)));
  auto alternate = [&](int64_t pts, AVRational base) {
    double target=speed==1 ? double(presentFps.load()) : std::min(30,presentFps.load())/speed.load();
    if (sourceRate <= target + .1 || pts == AV_NOPTS_VALUE || phase == AV_NOPTS_VALUE) return false;
    return h264::dropAtRate(std::llround((pts - phase) * av_q2d(base) * sourceRate),sourceRate,speed==1 ? 30 : target);
  };
  uint64_t ns = 0, copyNs = 0, flushNs = 0, since = SDL_GetTicks();
  int directFrames = 0, nonrefPackets = 0;
  double videoSeconds = 0, handoffFloor = -1;
  uint64_t previousWait = 0;
  bool reserveChecked = false;
  unsigned liveSwitches = 0;
  // A troca pode precisar de índice + seek pela rede. 6 s de PCM estéreo custam cerca de 2 MiB.
  const double audioReserve=direct && !broadcast && !speakerRequested && !media.muxed ? 6.0 : 0.65;
  auto decode = [&](h264::Stream& input, AVCodecContext* codec, AVPacket* packet) -> int {
    if (!codec) return 0;
    bool video = codec == s.video;
    if (video && packet) {
      ++packets;
      SDL_LockMutex(mu); int ready = (int)frames.size(); SDL_UnlockMutex(mu);
      bool relax = h264LoopFilter < 0 ? actualHeight >= 1080 || codec->height >= 1080 || (state == PLAYING && !paused && ready < 2) : h264LoopFilter != 0;
      codec->skip_loop_filter = relax ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
      if (relax) ++nonrefPackets;
      AVRational base = input.fmt->streams[input.vi]->time_base;
      if (packet->duration > 0) {
        double measured = 1.0 / (packet->duration * av_q2d(base));
        if ((sourceRate<=0 || (sourceRate<=30 && measured>30.1)) && measured>0 && measured<=60.1) sourceRate = measured;
      }
      if (phase == AV_NOPTS_VALUE && packet->pts != AV_NOPTS_VALUE) phase = packet->pts;
      videoSeconds += packet->duration > 0 ? packet->duration * av_q2d(base) : sourceRate > 0 ? 1 / sourceRate : 0;
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
        if (f->width > 1920 || f->height > 1080 || (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P))
          return AVERROR_INVALIDDATA;
        ++decoded;
        if (alternate(f->best_effort_timestamp, input.fmt->streams[input.vi]->time_base) ||
            (last >= 0 && pts <= last + .00001) || pts < handoffFloor - .00001) { ++filtered; continue; }
        if (pts < -0.0001) continue;
        pts = std::max(0.0, pts);
        SDL_LockMutex(mu);
        while ((int)frames.size() >= maxFrames && !stop) SDL_WaitCondition(cv, mu);
        SDL_UnlockMutex(mu);
        if (stop) return AVERROR_EXIT;
        bool bt709 = f->colorspace == AVCOL_SPC_BT709 || (f->colorspace == AVCOL_SPC_UNSPECIFIED && f->height > 576);
        auto surface = codec->get_buffer2 == h264::mappedBuffer ? h264::mappedSurface(f) : std::shared_ptr<video::Surface>{};
        if (surface) {
          begin = SDL_GetTicksNS(); surface->publish(); flushNs += SDL_GetTicksNS() - begin; ++directFrames;
        }
        begin = SDL_GetTicksNS();
        enqueuePlanes(f->width, f->height, pts, f->data, f->linesize, bt709,
                      f->color_range == AVCOL_RANGE_JPEG || f->format == AV_PIX_FMT_YUVJ420P, (int)std::lround(sourceRate),std::move(surface));
        copyNs += SDL_GetTicksNS() - begin; last = pts; count++;
        if (videoMetrics) {
          SDL_LockMutex(mu); int queued = (int)frames.size(); SDL_UnlockMutex(mu);
          LOG("decode-quadro: pts %.6f, %dx%d, fonte %.3f fps, fila %d, copia %.3f ms",pts,f->width,f->height,sourceRate,queued,(SDL_GetTicksNS()-begin) / 1e6);
        }
      } else {
        // Limita AAC adiantado inclusive quando a pessoa deixa um VOD pausado.
        while (!stop && audioQueuedSec() > std::max(2.0,audioReserve+.35)) { trimReserve(); SDL_Delay(8); }
        if (!putOriginalAudio(input, input.convert(f), pts)) return AVERROR_INVALIDDATA;
      }
      trimReserve();
    }
    return AVERROR_EXIT;
  };
  playback::QualityRecovery recovery(SDL_GetTicks());
  playback::QualityPressure pressure(SDL_GetTicks());
  uint64_t previousTransfer=0,previousBytes=0,networkAt=0;
  double networkMbps=0;
  auto ladderIndex=[&](){size_t i=0;while(i<qualityLadder.size() && qualityLadder[i].itag!=media.video.itag)++i;return i;};
  auto switchVideo = [&](size_t index,const char* reason,bool up) {
    if(index>=qualityLadder.size() || stop)return false;
    auto next=qualityLadder[index];
    int clock=masterMs.load();
    handoffFloor=std::max(last,std::max(0,clock)/1000.0);
    double resume=origin+std::max(0.0,handoffFloor);
    LOG("qualidade: fonte %dp%.3f -> %dp%.3f, itag %d -> %d em %.3f; motivo=%s; mantendo AAC e fila",
        media.video.height,media.video.fps,next.height,next.fps,media.video.itag,next.itag,resume,reason);
    s.reset();
    videoFile=std::make_unique<yt::RangeFile>(yt::https(&stop),next.url,&netBytes,yt::https(&stop));
    videoFile->aheadEnabled=false;
    uint64_t began=SDL_GetTicks();
    r=s.open(videoFile.get(),yt::RangeFile::pull,threads,false,true,yt::RangeFile::seek);
    if(r>=0)r=s.seekTime(resume);
    videoFile->aheadEnabled=true;
    media.video=next;
    media.lowerVideo.assign(qualityLadder.begin()+index+1,qualityLadder.end());
    media.fallbackVideo=media.lowerVideo.empty()?yt::Format{}:media.lowerVideo.front();
    actualHeight=next.height;sourceRate=next.fps;phase=AV_NOPTS_VALUE;
    presentFps=up?std::min(playback::rate(videoMode),std::max(1,int(std::lround(sourceRate)))):30;
    adapted=index!=0;slow=0;previousWait=previousTransfer=previousBytes=networkAt=0;networkMbps=0;
    recovery.changed(SDL_GetTicks(),up);
    pressure.changed(SDL_GetTicks());
    SDL_LockMutex(mu);previousShown=shownFrames;previousLost=droppedFrames;queueMin=int(frames.size());SDL_UnlockMutex(mu);
    intervalPts=last;pauseEpoch=pauseChanges.load();
    LOG("qualidade: video reaberto em %llu ms, resultado %d",(unsigned long long)(SDL_GetTicks()-began),r);
    return true;
  };
  auto lowerVideo = [&](const char* reason) {
    if (!direct || broadcast || media.muxed || (audio && !separate.audio) || media.lowerVideo.empty() || stop) return false;
    return switchVideo(ladderIndex()+1,reason,false);
  };
  bool videoEnd = false, audioEnd = !separate.audio;
  while (r >= 0 && !stop) {
    bool readAudio = !audioEnd && (videoEnd || audioQueuedSec() < audioReserve);
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
      if (r >= 0 && input.audio && !finishAudio()) r = AVERROR_INVALIDDATA;
      if (readAudio) audioEnd = true; else videoEnd = true;
      if (videoEnd && audioEnd) {
        double expected = direct ? media.duration : duration;
        if (last < 0 || (!live && expected > 0 && startAt + last + 1.25 < expected)) r = AVERROR_INVALIDDATA;
        break;
      }
    } else {
      if (r < 0) {
        if (!readAudio && videoFile && videoFile->status!=403) {
          while (lowerVideo("leitura da fonte falhou") && r<0) {}
          if (r>=0) { videoEnd=false; continue; }
        }
        break;
      }
      if (input.pkt->stream_index == input.vi) r = decode(input, input.video, input.pkt);
      else if (input.pkt->stream_index == input.ai) r = decode(input, input.audio, input.pkt);
      av_packet_unref(input.pkt);
    }
    uint64_t now = SDL_GetTicks();
    if (now - since >= 1000) {
      SDL_LockMutex(mu);
      int minimum = queueMin; queueMin = (int)frames.size(); long long lost = droppedFrames, shown = shownFrames;
      SDL_UnlockMutex(mu);
      double avg = count ? ns / 1e6 / count : 0.0;
      LOG("decod: H.264 %dt, %d quadros, %.2f ms/quadro, fila mín %d, perdidos %lld, PCM %.0f ms, rede %.2f Mbit/s",
          threads, count, avg, minimum, lost, audioQueuedSec() * 1000, SDL_SetAtomicInt(&netBytes, 0) * 0.008 / (now - since));
      double displayRate = (shown - previousShown) * 1000.0 / (now - since);
      LOG("qualidade: %s %dp %.3f fps, alvo %d, %.1f fps na TV, pacotes %d, decodificados %d, filtrados %d, decode %.1f ms/s de video, copia %.2f ms/quadro, perdidos +%lld",
          broadcast ? "live" : "VOD", actualHeight.load(), sourceRate, presentFps.load(), displayRate, packets, decoded, filtered,
          videoSeconds > 0 ? ns / 1e6 / videoSeconds : 0, count ? copyNs / 1e6 / count : 0, lost - previousLost);
      LOG("decod-memoria: direto %d/%d, flush %.3f ms/quadro, loop NONREF %d/%d pacotes, tipo %d, workers %d",
          directFrames,count,count ? flushNs/1e6/count : 0,nonrefPackets,packets,s.video ? s.video->active_thread_type : 0,threads);
      h264::ffmpeg_work_metrics(&workUs,&workFrames);
      if (workFrames) LOG("decod-workers: %u quadros, soma parede %.2f ms, %.2f ms/quadro (paralelos, inclui dependencia)",
                          workFrames,workUs/1000.0,workUs/1000.0/workFrames);
      int clock = masterMs.load();
      unsigned epoch = pauseChanges.load();
      bool behind = clock >= 0 && last < clock / 1000.0 - .25;
      bool insufficient = intervalPts >= 0 && last - intervalPts < (now - since) * .00090;
      double targetRate = std::min(double(presentFps.load()), sourceRate);
      // A TV pode ficar sem PCM com a fila de imagem cheia: ambos vêm do mesmo demuxer.
      bool slowLive = broadcast && displayRate < targetRate * .85 &&
                      (insufficient || behind || lost - previousLost > 10);
      bool slowUpload = original && displayRate < targetRate * .90 && lost - previousLost > 3;
      uint64_t waited=videoFile ? videoFile->waitMs : 0;
      bool networkPressure=waited>previousWait+(now-since)/6 && insufficient;
      previousWait=waited;
      auto rate=[](const yt::RangeFile::Lane& lane) { auto ms=lane.transferMs.load(); return ms ? lane.bytes.load()*.008/ms : 0; };
      if (videoFile) LOG("rede: Range %d KiB, %d TLS video, pedidos %u, retomadas %u, conexoes %u, espera %llu ms, TCP %d/%d KiB, TLS %.2f/%.2f Mbit/s",
          yt::RangeFile::chunk/1024,videoFile->laneCount,videoFile->requests.load(),videoFile->resumes.load(),
          videoFile->connections.load(),(unsigned long long)waited,videoFile->receiveBuffer.load()/1024,videoFile->sendBuffer.load()/1024,
          rate(videoFile->lanes[0]),rate(videoFile->lanes[1]));
      if (broadcast && broadcast->switches!=liveSwitches) {
        liveSwitches=broadcast->switches;
        double fps=broadcast->selectedRate/1000.0;
        if (fps>0) sourceRate=fps;
        LOG("qualidade: HLS fonte alinhada por UTC, %dp %.3f fps, mantendo AAC, troca %u",
            broadcast->selectedHeight.load(),fps,liveSwitches);
      }
      size_t index=ladderIndex();
      bool canRaise=videoFile && !media.muxed && !broadcast && index>0 && index<qualityLadder.size();
      bool raisePresentation=videoFile && !broadcast && sourceRate>30.1 && presentFps<=30 && playback::rate(videoMode)>30 && !canRaise;
      double scale=1,nextMbps=0;
      if(videoFile) {
        uint64_t transferred=videoFile->transferMs.load(),bytes=videoFile->bytes.load();
        if(transferred>previousTransfer && bytes>previousBytes) {
          networkMbps=(bytes-previousBytes)*.008/(transferred-previousTransfer);networkAt=now;
        }
        previousTransfer=transferred;previousBytes=bytes;
        if(canRaise) {
          const auto& next=qualityLadder[index-1];
          double fromRate=std::min(sourceRate,double(presentFps.load()));
          scale=double(next.height)*next.height*next.fps/std::max(1.0,double(media.video.height)*media.video.height*fromRate);
          double currentMbps=media.video.bitrate>0?media.video.bitrate/1e6:videoFile->length>0 && media.duration>0?videoFile->length*.000008/media.duration:0;
          nextMbps=next.bitrate>0?next.bitrate/1e6:currentMbps*scale*1.35;
        } else if(raisePresentation){scale=sourceRate/30;nextMbps=media.video.bitrate/1e6;}
        nextMbps+=media.audio.bitrate/1e6;
      }
      bool running=state==PLAYING && !paused && pauseEpoch==epoch;
      bool starvation=(minimum<2 && (behind || insufficient)) || slowLive || slowUpload || networkPressure;
      double decodeFraction=videoSeconds>0?ns/1e9/videoSeconds:0;
      double copyFraction=videoSeconds>0?copyNs/1e9/videoSeconds:0;
      double currentMbps=videoFile && media.video.bitrate>0 ? media.video.bitrate/1e6 :
          videoFile && videoFile->length>0 && media.duration>0 ? videoFile->length*.000008/media.duration : 0;
      bool networkFresh=networkAt && now-networkAt<=10000;
      bool videoPresentationPressure=playback::presentationPressure(media.video.height,presentFps,slowUpload,copyFraction);
      playback::QualityLoad load(decodeFraction,copyFraction,videoPresentationPressure,networkPressure,networkFresh,networkMbps,currentMbps);
      bool networkLimited=load.networkLimited,overloaded=load.overloaded;
      bool vod=videoFile && !broadcast && !media.muxed;
      bool down=vod && pressure.sample(now,now-since,running,videoSeconds>=.25,starvation,overloaded);
      bool settled=!vod || pressure.settled(now,now-since);
      bool raise=recovery.sample(now,now-since,running && speed==1 && settled,
          minimum,behind,lost-previousLost,videoSeconds>0?ns/1e9/videoSeconds:0,scale,networkMbps,nextMbps,networkAt && now-networkAt<=10000);
      slow = running && starvation && (!vod || (settled && overloaded)) ? slow + 1 : 0;
      if(vod)LOG("qualidade: decisao %s, pressao %llu ms/%u janelas, decode %.1f%% copia %.1f%%, rede medida %.2f/necessaria %.2f Mbit/s, fila %d",
          !settled?"assentando":down?"descer":overloaded && starvation?"medindo pressao":"manter",
          (unsigned long long)pressure.pressureMs,pressure.windows,decodeFraction*100,copyFraction*100,networkMbps,currentMbps,minimum);
      previousShown = shown; previousLost = lost;
      intervalPts = last; pauseEpoch = epoch;
      // A queda de fonte vem antes do filtro de apresentação: ela também reduz o tráfego.
      if (slow>=3 && broadcast && broadcast->requestLower()) {
        presentFps=30; adapted=true; slow=0;
        LOG("qualidade: HLS solicitando fonte menor na mesma linha do tempo");
      } else if (down && playback::cadenceBeforeResolution(media.video.height,sourceRate,presentFps,
                 media.lowerVideo.empty()?0:media.lowerVideo.front().height)) {
        presentFps=30;adapted=true;slow=0;pressure.changed(now);recovery.changed(now,false);
        LOG("qualidade: fonte %dp%.3f mantida, apresentacao 60 -> 30; motivo=sem fonte 720p30 distinta; medir este degrau antes de reduzir resolucao; mesma rede",media.video.height,sourceRate);
      } else if ((vod?down:slow>=3) && lowerVideo(networkLimited?"rede medida insuficiente apos estabilizacao":slowUpload?"apresentacao sob pressao sustentada":"decodificacao/fila sob pressao sustentada")) {
        videoEnd=false;
        while (r<0 && lowerVideo("abertura da fonte falhou")) {}
      } else if ((vod?down:slow>=3) && presentFps>30) {
        presentFps=30; adapted=true; slow=0;
        recovery.changed(now,false);
        pressure.changed(now);
        LOG("qualidade: sem fonte menor; apresentacao 60 -> 30, motivo=pressao sustentada, mesma rede");
      }
      else if(slow==0 && raise && canRaise) {
        LOG("qualidade: folga estavel %llu ms, decode projetado %.1f%%, rede %.2f Mbit/s, proxima %.2f Mbit/s",
            (unsigned long long)recovery.goodMs,videoSeconds>0?ns/1e7/videoSeconds*scale:0,networkMbps,nextMbps);
        auto previous=index;
        switchVideo(index-1,"20s de folga medida em decode e rede",true);
        if(r<0){LOG("qualidade: subida falhou; restaurando fonte anterior");switchVideo(previous,"falha ao subir",false);}
        videoEnd=false;
      } else if(slow==0 && raise && raisePresentation) {
        presentFps=std::min(playback::rate(videoMode),int(std::lround(sourceRate)));adapted=false;recovery.changed(now,true);
        LOG("qualidade: apresentacao 30 -> %d, motivo=20s de folga medida em decode e rede",presentFps.load());
      }
      if (slow>=5 && (!direct || broadcast || media.lowerVideo.empty())) {
        LOG("qualidade: opcoes locais esgotadas sob pressao; %d janelas, rede %d, fila %d",slow,(int)networkPressure,minimum);
        if (!direct || (!reserveChecked && net::serverEnabled && directFailed(false))) { r=AVERROR(ETIMEDOUT); break; }
        reserveChecked=true; slow=0;
      }
      since = SDL_GetTicks(); count = packets = decoded = filtered = directFrames = nonrefPackets = 0; ns = copyNs = flushNs = 0; videoSeconds = 0;
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
  if (r < 0) {
    LOG("decod: falha H.264 %d, último PTS %.3f", r, last);
    for (const auto* file : {videoFile.get(),audioFile.get()}) if (file && file->failed)
      LOG("rede: falha Range classe %s HTTP %ld, pedidos %u, retomadas %u: %s",yt::failureName(file->failure),file->status,
          file->requests.load(),file->resumes.load(),file->error.c_str());
    if (direct) { if (!retryConverted) directFailed(); } else retryConverted=true;
  }
  else { SDL_LockMutex(mu); decEnded = true; SDL_UnlockMutex(mu); }
}

}  // namespace player
