// Implementação de Player para o stream negociado com codec=h264.
#pragma once
#include "quality_recovery.hpp"
#include <array>
#include "yt/live_seq.hpp"
namespace player {

inline bool Player::directFailed(bool fatal) {
  if(playback::manual(videoMode)){if(fatal){error="manual_quality_failed";state=FAILED;}return false;}
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
  const int requestedThreads=threads;
  h264::Stream s, separate;
  s.videoBuffers = videoBuffers; s.threadType = h264ThreadType;
  const bool fullFilter=h264FullFilter || h264LoopFilter==0;
  s.source(0,sourceFpsHint,fullFilter);
  unsigned workUs = 0, workFrames = 0;
  h264::ffmpeg_work_metrics(&workUs,&workFrames);
  std::unique_ptr<yt::RangeFile> videoFile, audioFile;
  std::vector<yt::Format> qualityLadder;
  std::unique_ptr<yt::Transport> liveTransport, playlistTransport;
  std::unique_ptr<yt::LiveFile> broadcast;
  std::unique_ptr<yt::LiveSeq> liveSeq;
  bool seqLive = false;
  uint64_t seqBytesSeen = 0;
  double aheadMin = 1e9; std::array<double,6> aheadHistory; int aheadSamples = 0;   // menor reserva (s) por segundo, últimos 6 s
  struct LiveInput { Player* player; yt::LiveFile* file; std::deque<std::string> audio; } source{this,nullptr,{}};
  yt::PackedAudio packed;
  SDL_Thread* reader = nullptr;
  double origin = 0;
  int r = 0;
  auto openRange = [&](h264::Stream& stream,yt::RangeFile& file,bool sound,bool picture,double seconds) {
    file.aheadEnabled=false;
    uint64_t began=SDL_GetTicks();
    int result=stream.open(&file,yt::RangeFile::pull,picture ? requestedThreads : 1,sound,picture,yt::RangeFile::seek);
    uint64_t headerMs=SDL_GetTicks()-began;
    if(result>=0)result=stream.seekTime(seconds);
    LOG("cabecalho: %s resultado=%d tamanho=%lld bytes=%llu pedidos=%u header_ms=%llu seek_ms=%llu HTTP=%ld erro=%s",
        picture ? "video" : "audio",result,(long long)file.length,(unsigned long long)file.bytes.load(),file.requests.load(),
        (unsigned long long)headerMs,(unsigned long long)(SDL_GetTicks()-began-headerMs),file.failed ? file.status : file.current.status,file.error.c_str());
    file.aheadEnabled=true;
    return result;
  };
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
    qualityAvailable=media.qualityModes;qualityModesReady=true;
    if (media.status != "OK") {
      LOG("console: player %s, classe %s, cliente %s, %u pedidos: %s", media.status.c_str(), yt::failureName(media.failure),
          media.playerClient.c_str(), media.playerAttempts, media.reason.c_str());
      if (media.reason == "members") { error = "Vídeo só para membros do canal"; state = FAILED; return; }
      directFailed(); return;
    }
    LOG("console: player OK, cliente %s, %u pedidos: %s resolucao_ms=%llu",media.playerClient.c_str(),media.playerAttempts,media.playerTrace.c_str(),
        (unsigned long long)(SDL_GetTicks()-openedMs));
    if (memberVideo && media.hls.empty() && media.video.url.empty()) {
      LOG("console: video de membros sem HLS ou URL de video; encerrando sem renovar URL");
      error = "members_unsupported"; renew = retryConverted = false; state = FAILED; return;
    }
    live = !media.hls.empty() && (media.live || media.video.url.empty() || !media.duration);
    // Live de membros sem HLS: segmentos &sq=N, uma faixa de vídeo e uma de áudio, `n` já resolvido pelo cliente.
    seqLive = media.liveSeq && media.hls.empty() && !media.video.url.empty() && !media.audio.url.empty();
    if (seqLive) live = true;   // os degraus (lowerVideo) ficam: servem de reserva se a abertura da fonte escolhida falhar
    if (stop) return;
    if (live) { speed=1; loop=false; tempo.reset(1); }
    castMediaReady = true;
    state = BUFFERING;
    mediaReady = true;
    if(playback::manual(videoMode) && (media.video.url.empty() || !(media.qualityModes&(1u<<videoMode))) && !live){error="manual_quality_unavailable";state=FAILED;return;}
    if (seqLive) {
      qualityAvailable=1;qualityModesReady=true;
      s.wrap33 = separate.wrap33 = false;
      auto openSeq=[&](const yt::Format& format) {
        liveSeq = std::make_unique<yt::LiveSeq>();
        s.source(format.height,format.fps,fullFilter);
        bool opened = liveSeq->open(format.url,media.audio.url,liveLowLatency,&stop,yt::https(&stop),yt::https(&stop));
        LOG("live-seq: abertura %s %dp, borda sq=%lld, inicio sq=%lld, segmento %.1f s, atraso minimo %d, erro=%s HTTP=%ld",opened?"ok":"falhou",
            format.height,liveSeq->headSeq(),liveSeq->first,liveSeq->segSec(),int(liveLowLatency),liveSeq->error.c_str(),liveSeq->httpStatus());
        int result = opened ? 0 : AVERROR_INVALIDDATA;
        if (result >= 0) result = s.open(&liveSeq->tracks[0],yt::LiveSeq::pull,requestedThreads,false,true,nullptr,"mov");
        if (result >= 0 && audio) result = separate.open(&liveSeq->tracks[1],yt::LiveSeq::pull,1,true,false,nullptr,"mov");
        return result;
      };
      r = openSeq(media.video);
      // Fonte escolhida não abriu (inclusive 403: há lives em que o degrau mais alto responde 403 e os outros não): desce um degrau, como no VOD.
      // Se todos derem 403 sobra o caminho do 403 de membros. Troca de degrau com a live em curso não existe.
      if (r < 0 && !stop && liveSeq->error == "waiting") {
        // Live que não começou ou parou: descer degrau não resolve. Mensagem própria, sem renovar nem cair no servidor.
        LOG("live-seq: borda sem segmento novo (204 ou tempo esgotado); live nao comecou ou esta parada");
        error = "live_waiting"; renew = retryConverted = false; state = FAILED; return;
      }
      while (r < 0 && !stop && !playback::manual(videoMode) && liveSeq->error != "waiting" && !media.lowerVideo.empty()) {
        auto next = media.lowerVideo.front(); media.lowerVideo.erase(media.lowerVideo.begin());
        LOG("qualidade: live por segmentos %dp%.3f -> %dp%.3f, motivo=abertura anterior falhou",media.video.height,media.video.fps,next.height,next.fps);
        s.reset(); separate.reset(); s.wrap33 = separate.wrap33 = false;
        media.video = next; media.fallbackVideo = {};
        r = openSeq(next);
      }
      actualHeight = media.video.height;
      origin = NAN;
    } else if (live) {
      liveTransport = yt::https(&endLiveReader);
      playlistTransport = yt::https(&endLiveReader);
      broadcast = std::make_unique<yt::LiveFile>(*playlistTransport, *liveTransport, endLiveReader);
      broadcast->startBack = liveLowLatency ? 1 : yt::LiveFile::reserve;
      bool opened=broadcast->open(media.hls,playback::manual(videoMode)?playback::fixedHeight(videoMode):quality,videoMode==playback::FAST,media.selectedAudio.empty() ? audioSelection : media.selectedAudio,playback::manual(videoMode));
      qualityAvailable=broadcast->qualityModes;qualityModesReady=true;
      if (!opened) {r = AVERROR_INVALIDDATA;if(playback::manual(videoMode) && broadcast->status!=403){error="manual_quality_unavailable";state=FAILED;return;}}
      else {
        source.file = broadcast.get();
        reader = yt::worker([](void* data) {
          auto& source = *static_cast<LiveInput*>(data);
          auto& p = *source.player;
          moveToHardwareThread(3);
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
        s.source(broadcast->height,broadcast->frameRate,fullFilter);
        r = reader ? s.open(this, readOriginal, requestedThreads, audio && broadcast->audioPlaylist.empty()) : AVERROR(ENOMEM);
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
      s.source(media.video.height,media.video.fps,fullFilter);
      r = media.video.url.empty() ? AVERROR_STREAM_NOT_FOUND :
          openRange(s,*videoFile,media.muxed && audio,true,startAt);
      while (r<0 && !playback::manual(videoMode) && !stop && !media.muxed && videoFile->status!=403 && !media.lowerVideo.empty()) {
        auto next=media.lowerVideo.front(); media.lowerVideo.erase(media.lowerVideo.begin());
        LOG("qualidade: abertura direta %dp%.3f -> %dp%.3f, motivo=abertura anterior falhou",media.video.height,media.video.fps,next.height,next.fps);
        s.reset(); videoFile=std::make_unique<yt::RangeFile>(yt::https(&stop),next.url,&netBytes,yt::https(&stop));
        s.source(next.height,next.fps,fullFilter);
        r=openRange(s,*videoFile,false,true,startAt);
        media.video=next; presentFps=30; adapted=true;
      }
      if (r >= 0 && audio && !media.muxed) {
        audioFile = std::make_unique<yt::RangeFile>(yt::https(&stop), media.audio.url, &netBytes);
        r = media.audio.url.empty() ? AVERROR_STREAM_NOT_FOUND :
            openRange(separate,*audioFile,true,false,startAt);
      }
      actualHeight = media.video.height;
      origin = startAt;
      if (quality >= 720 && media.highFrameRate720 && media.video.height < 720)
        LOG("console: %s tem 720p60, selecionado %dp %.2f fps", id.c_str(), media.video.height, media.video.fps);
    }
    LOG("console: %s %dp, itags %d/%d, início %.3f abertura_ms=%llu", live ? (seqLive ? "SEG" : "HLS") : "MP4", actualHeight.load(), media.video.itag, media.audio.itag, startAt,
        (unsigned long long)(SDL_GetTicks()-openedMs));
    LOG("qualidade: selecao %s fixa=%d fonte=%dp itag=%d disponiveis=%x",playback::modeName(videoMode),playback::manual(videoMode),actualHeight.load(),media.video.itag,qualityAvailable.load());
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
  } else r = s.open(this, readOriginal, requestedThreads, audio != nullptr);
  hasAudio = (s.audio || separate.audio) && r >= 0;
  double last = -1, intervalPts = -1, nextAudioPts = NAN;
  unsigned pauseEpoch = pauseChanges.load();
  int count = 0, slow = 0, packets = 0, decoded = 0, filtered = 0;
  int64_t previousShown = 0, previousLost = 0, phase = AV_NOPTS_VALUE;
  double sourceRate = broadcast ? broadcast->frameRate : direct ? media.video.fps : sourceFpsHint;
  if (r >= 0 && s.video && sourceRate <= 0) sourceRate = av_q2d(s.fmt->streams[s.vi]->avg_frame_rate);
  if (!std::isfinite(sourceRate) || sourceRate < 0) sourceRate = 0;
  auto publishPolicy=[&]() {
    if (!s.video) return;
    threads=s.video->thread_count;
    activeH264Threads=threads;activeLoopFilter=int(s.video->skip_loop_filter);activeH264Spread=s.policy.spread;
    activeVideoItag=direct && !broadcast ? media.video.itag : 0;
    LOG("decod-config: threads %d, tipo solicitado %d ativo %d, buffers %s, loop %d spread %d politica=abertura",
        threads,s.video->thread_type,s.video->active_thread_type,videoBuffers ? "diretos" : "copiados",
        int(s.video->skip_loop_filter),int(s.policy.spread));
  };
  publishPolicy();
  if (sourceRate > 0) presentFps=std::min(presentFps.load(),std::max(1,(int)std::lround(sourceRate)));
  auto alternate = [&](int64_t pts, AVRational base) {
    double target=speed==1 ? double(presentFps.load()) : std::min(30,presentFps.load())/speed.load();
    if (sourceRate <= target + .1 || pts == AV_NOPTS_VALUE || phase == AV_NOPTS_VALUE) return false;
    return h264::dropAtRate(std::llround((pts - phase) * av_q2d(base) * sourceRate),sourceRate,speed==1 ? 30 : target);
  };
  uint64_t ns = 0, copyNs = 0, flushNs = 0, since = SDL_GetTicks();
  int directFrames = 0, skippedLoopPackets = 0;
  int queueHeight = 0;
  size_t memoryFloor = SIZE_MAX;
  double videoSeconds = 0, handoffFloor = -1;
  uint64_t previousWait = 0;
  bool reserveChecked = false;
  unsigned liveSwitches = 0;
  // A troca pode precisar de índice + seek pela rede. 6 s de PCM estéreo custam cerca de 2 MiB.
  const double audioReserve=direct && !broadcast && !seqLive && !speakerRequested && !media.muxed ? 6.0 : 0.65;
  // Reserva da live: mídia já entregue ao demuxer menos a mídia em tela (segmentos: fim do último entregue; HLS: UTC planejado).
  auto liveAhead = [&]() -> double {
    const int c = masterMs.load();
    if (c < 0 || !std::isfinite(origin)) return -1;
    if (liveSeq) { const double end = liveSeq->pushedEndSec(); return end >= 0 ? end - (origin + c / 1000.0) : -1; }
    if (broadcast && broadcast->firstStamp.load() >= 0 && broadcast->fetchedEnd.load() >= 0) return broadcast->fetchedEnd.load() - (broadcast->firstStamp.load() + c / 1000.0);
    return -1;
  };
  auto decode = [&](h264::Stream& input, AVCodecContext* codec, AVPacket* packet) -> int {
    if (!codec) return 0;
    bool video = codec == s.video;
    if (video && packet) {
      ++packets;
      if (codec->skip_loop_filter==AVDISCARD_ALL) ++skippedLoopPackets;
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
        if (queueHeight != f->height) {
          queueHeight = f->height;
          size_t available = video::titleFreeBytes();
          auto budget = video::decodeQueue(f->height,sourceRate,available);
          SDL_LockMutex(mu);
          maxFrames = budget.capacity; startupFrames = budget.startup; queueMin = (int)frames.size();
          spare.reserve(size_t(maxFrames)+2);
          SDL_UnlockMutex(mu);
          LOG("decod-fila: %dp %.3f fps, capacidade %d inicio %d, livres %u MiB, extra ate 32 MiB (pool 160 MiB)",
              f->height,sourceRate,budget.capacity,budget.startup,(unsigned)(available>>20));
        }
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
        if (live) { const double a = liveAhead(); if (a >= 0) aheadMin = std::min(aheadMin, a); }
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
  auto& recovery=qualityRecovery;
  recovery.goodMs=0;recovery.holdUntil=std::max(recovery.holdUntil,SDL_GetTicks()+30000);
  if(!recovery.failed.empty())LOG("qualidade: historico preservado na sessao, %u degraus rejeitados",unsigned(recovery.failed.size()));
  playback::QualityPressure pressure(SDL_GetTicks());
  uint64_t manualWarnAt=0;
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
    s.source(next.height,next.fps,fullFilter);
    uint64_t began=SDL_GetTicks();
    r=openRange(s,*videoFile,false,true,resume);
    media.video=next;
    publishPolicy();
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
    if (playback::manual(videoMode) || !direct || broadcast || liveSeq || media.muxed || (audio && !separate.audio) || media.lowerVideo.empty() || stop) return false;
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
      if (liveSeq) { const uint64_t total = liveSeq->bytes.load(); SDL_AddAtomicInt(&netBytes, int(total - seqBytesSeen)); seqBytesSeen = total; }
      LOG("decod: H.264 %dt, %d quadros, %.2f ms/quadro, fila mín %d, perdidos %lld, PCM %.0f ms, rede %.2f Mbit/s",
          threads, count, avg, minimum, lost, audioQueuedSec() * 1000, SDL_SetAtomicInt(&netBytes, 0) * 0.008 / (now - since));
      double displayRate = (shown - previousShown) * 1000.0 / (now - since);
      LOG("qualidade: %s %dp %.3f fps, alvo %d, %.1f fps na TV, pacotes %d, decodificados %d, filtrados %d, decode %.1f ms/s de video, copia %.2f ms/quadro, perdidos +%lld",
          broadcast || liveSeq ? "live" : "VOD", actualHeight.load(), sourceRate, presentFps.load(), displayRate, packets, decoded, filtered,
          videoSeconds > 0 ? ns / 1e6 / videoSeconds : 0, count ? copyNs / 1e6 / count : 0, lost - previousLost);
      LOG("decod-memoria: direto %d/%d, flush %.3f ms/quadro, loop ALL %d/%d pacotes, tipo %d, workers %d",
          directFrames,count,count ? flushNs/1e6/count : 0,skippedLoopPackets,packets,s.video ? s.video->active_thread_type : 0,threads);
#ifdef __XSDK__
      size_t freeMemory = video::titleFreeBytes(); memoryFloor = std::min(memoryFloor,freeMemory);
      LOG("decod-orcamento: livres %u MiB, minimo amostrado %u MiB, fila teto %d",
          (unsigned)(freeMemory>>20),(unsigned)(memoryFloor>>20),maxFrames);
#else
      (void)memoryFloor;
#endif
      h264::ffmpeg_work_metrics(&workUs,&workFrames);
      if (workFrames) LOG("decod-workers: %u quadros, soma parede %.2f ms, %.2f ms/quadro (paralelos, inclui dependencia)",
                          workFrames,workUs/1000.0,workUs/1000.0/workFrames);
      int clock = masterMs.load();
      if ((liveSeq || broadcast) && clock >= 0 && std::isfinite(origin)) {
        // Atraso da imagem em tela. Segmentos: relógio do servidor (idade do segmento incluída); HLS: fim do segmento mais novo pelo UTC da
        // playlist. O alcance a 1,05x (WSOLA, sem mudar o tom) liga bem acima do piso e só desliga perto dele.
        double latency = -1, ahead = 0, segment = 0;
        if (liveSeq) {
          const double head = liveSeq->headStartSec(), end = liveSeq->pushedEndSec(), shown = origin + clock / 1000.0;
          if (head >= 0) { latency = liveSeq->latencySec(shown); ahead = end >= 0 ? end - shown : 0; segment = liveSeq->segSec(); }
        } else if (broadcast->firstStamp.load() >= 0 && broadcast->edgeEnd.load() >= 0 && broadcast->segmentSec.load() > 0) {
          const double shown = broadcast->firstStamp.load() + clock / 1000.0;
          latency = broadcast->edgeEnd.load() - shown; ahead = broadcast->fetchedEnd.load() - shown; segment = broadcast->segmentSec.load();
        }
        if (aheadMin < 1e8) { aheadHistory[size_t(aheadSamples++ % 6)] = aheadMin; aheadMin = 1e9; }
        if (latency >= 0 && segment > 0 && aheadSamples >= 3) {
          double low = 1e9; for (int i = 0; i < std::min(aheadSamples, 6); ++i) low = std::min(low, aheadHistory[size_t(i)]);
          const double now = liveRate.load();
          const double want = yt::seq::speedFor(latency,low,segment,now,liveLowLatency);
          if (want != now) { LOG("live: velocidade %.2fx -> %.2fx (WSOLA), atraso %.2f s, reserva minima %.2f s, segmento %.1f s",now,want,latency,low,segment); setLiveSpeed(want); }
          if (liveSeq) LOG("live-seq: atraso %.2f s (segmento %.1f s), reserva minima %.2f s, velocidade %.2fx, segmentos v%u a%u, lacunas %u/%u, borda sq=%lld",
              latency,segment,low,liveRate.load(),liveSeq->tracks[0].segments.load(),liveSeq->tracks[1].segments.load(),
              liveSeq->tracks[0].gaps.load(),liveSeq->tracks[1].gaps.load(),liveSeq->headSeq());
          else LOG("live-hls: atraso %.2f s (segmento %.1f s), reserva minima %.2f s, velocidade %.2fx, inicio %d segmentos antes da borda",latency,segment,low,liveRate.load(),broadcast->startBack);
        }
      }
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
      bool fixed=playback::manual(videoMode);
      bool canRaise=!fixed && videoFile && !media.muxed && !broadcast && index>0 && index<qualityLadder.size();
      bool raisePresentation=!fixed && videoFile && !broadcast && sourceRate>30.1 && presentFps<=30 && playback::rate(videoMode)>30 && !canRaise;
      double scale=1,nextMbps=0;
      double measuredRate=std::min(sourceRate,double(presentFps.load()));
      int nextItag=media.video.itag,nextFps=int(std::lround(sourceRate));
      if(videoFile) {
        uint64_t transferred=videoFile->transferMs.load(),bytes=videoFile->bytes.load();
        if(transferred>previousTransfer && bytes>previousBytes) {
          networkMbps=(bytes-previousBytes)*.008/(transferred-previousTransfer);networkAt=now;
        }
        previousTransfer=transferred;previousBytes=bytes;
        if(canRaise) {
          const auto& next=qualityLadder[index-1];
          scale=playback::qualityWorkScale(media.video.width,media.video.height,measuredRate,next.width,next.height,next.fps);
          nextItag=next.itag;nextFps=int(std::lround(next.fps));
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
      bool measuredPressure=(vod || fixed) && pressure.sample(now,now-since,running,videoSeconds>=.25,starvation,overloaded);
      bool down=!fixed && measuredPressure;
      if(fixed && measuredPressure && now>=manualWarnAt){manualQualityWarning=true;manualWarnAt=now+30000;LOG("qualidade: manual %dp itag %d sob pressao sustentada; avisar sem trocar fonte",media.video.height,media.video.itag);}
      bool settled=!vod || pressure.settled(now,now-since);
      // Mede por quadro entregue, sem diluir custo por durações de pacotes/preroll.
      double measuredDecode=playback::qualityDecodeFraction(avg,measuredRate);
      if (s.policy.skipLoop && count>=5 && videoSeconds>=.25)
        observed1080Ms=observed1080Ms>0 ? .75*observed1080Ms+.25*avg : avg;
      bool nextFast1080=canRaise && !fullFilter && playback::source1080p30(qualityLadder[index-1].height,qualityLadder[index-1].fps);
      double projected=playback::projectedDecodeFraction(measuredDecode,scale,nextFast1080,
          canRaise ? qualityLadder[index-1].fps : sourceRate,observed1080Ms);
      double cpuScale=measuredDecode>0 ? projected/measuredDecode : 0;
      uint64_t retryAt=recovery.retryAfter(nextItag,nextFps);
      bool raise=recovery.sample(now,now-since,running && speed==1 && settled,
          minimum,behind,lost-previousLost,measuredDecode,cpuScale,networkMbps,nextMbps,networkFresh,retryAt);
      if(canRaise || raisePresentation)LOG("qualidade: subida %s itag %d/%dfps, decode medido %.2f ms/quadro, escala pixels/fps %.3f, projetado %.1f%%, bloqueio %llu ms, modelo=%s",
          now<retryAt?"aguardar degrau que falhou":projected>=.70?"sem folga de CPU":raise?"permitida":"medindo",
          nextItag,nextFps,avg,scale,projected*100,(unsigned long long)(now<retryAt?retryAt-now:0),
          nextFast1080 ? observed1080Ms>0 ? "1080p30-sessao" : "1080p30-F39" : "pixels/fps");
      slow = !fixed && running && starvation && (!vod || (settled && overloaded)) ? slow + 1 : 0;
      if(vod)LOG("qualidade: decisao %s, pressao %llu ms/%u janelas, decode %.1f%% copia %.1f%%, rede medida %.2f/necessaria %.2f Mbit/s, fila %d",
          !settled?"assentando":down?"descer":overloaded && starvation?"medindo pressao":"manter",
          (unsigned long long)pressure.pressureMs,pressure.windows,decodeFraction*100,copyFraction*100,networkMbps,currentMbps,minimum);
      previousShown = shown; previousLost = lost;
      intervalPts = last; pauseEpoch = epoch;
      if(down && (presentFps>30 || !media.lowerVideo.empty())) {
        bool cpu=!networkLimited;
        recovery.rejected(now,media.video.itag,presentFps,cpu);
        LOG("qualidade: degrau rejeitado itag %d/%dfps, decode %.2f ms/quadro, motivo=%s, nova tentativa em %llu ms",
            media.video.itag,presentFps.load(),avg,cpu?"CPU/copia/apresentacao":"rede",
            (unsigned long long)(recovery.retryAfter(media.video.itag,presentFps)-now));
      }
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
            (unsigned long long)recovery.goodMs,projected*100,networkMbps,nextMbps);
        auto previous=index;
        switchVideo(index-1,"20s de folga medida em decode e rede",true);
        if(r<0){LOG("qualidade: subida falhou; restaurando fonte anterior");recovery.rejected(SDL_GetTicks(),nextItag,nextFps,false);switchVideo(previous,"falha ao subir",false);}
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
      since = SDL_GetTicks(); count = packets = decoded = filtered = directFrames = skippedLoopPackets = 0; ns = copyNs = flushNs = 0; videoSeconds = 0;
    }
  }
  if (reader) {
    endLiveReader = true;
    SDL_LockMutex(mu); SDL_BroadcastCondition(cv); SDL_UnlockMutex(mu);
    SDL_WaitThread(reader,nullptr);
    if (netErr && r >= 0) r = AVERROR(EIO);
  }
  if (stop) return;
  bool forbidden = (videoFile && videoFile->status == 403) || (audioFile && audioFile->status == 403) ||
                   (broadcast && broadcast->status == 403) || (liveSeq && liveSeq->httpStatus() == 403);
  if (direct && memberVideo && forbidden && media.nsigSolved && !refreshed) {
    // Com o `n` resolvido o 403 pode ser passageiro (rede, borda do CDN) ou URL vencida. Espera 2,5 s e testa a MESMA URL com 1 byte:
    // se responde, repete a URL; se não, renova pelo /player. Um segundo 403 depois disso vira a mensagem de membros.
    for (int waited = 0; waited < 2500 && !stop; waited += 50) SDL_Delay(50);
    if (stop) return;
    auto probeNet = yt::https(&stop);
    yt::Request probe; probe.url = media.video.url; probe.headers = {"Range: bytes=0-0"}; probe.media = true; probe.timeoutMs = 8000;
    auto answer = probeNet->send(probe);
    sameUrlRetry = answer.error.empty() && (answer.status == 200 || answer.status == 206);
    LOG("console: video de membros HTTP=403 com n resolvido; sonda da mesma URL HTTP=%ld, %s", answer.status, sameUrlRetry ? "repete a URL" : "renova pelo /player");
    renew = true; return;
  }
  if (direct && memberVideo && forbidden) {
    LOG("console: video de membros HTTP=403; encerrando sem renovar URL");
    error = "members_unsupported"; renew = retryConverted = false; state = FAILED; return;
  }
  if (direct && (forbidden || ((media.ttl || media.expiresAt) && SDL_GetTicks() >= validUntil))) { renew = true; return; }
  if (r < 0) {
    LOG("decod: falha H.264 %d, último PTS %.3f", r, last);
    for (const auto* file : {videoFile.get(),audioFile.get()}) if (file && file->failed)
      LOG("rede: falha Range classe %s HTTP %ld, pedidos %u, retomadas %u: %s",yt::failureName(file->failure),file->status,
          file->requests.load(),file->resumes.load(),file->error.c_str());
    if (direct) { if (!retryConverted) directFailed(); } else retryConverted=true;
  }
  else { SDL_LockMutex(mu); decEnded = true; decDone = true; SDL_UnlockMutex(mu); }
}

}  // namespace player
