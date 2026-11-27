#pragma once
#include "player.hpp"
#include "yt/catalog_replay.hpp"

namespace music {
// O adaptador mantém a negociação VISIONOS e deixa o seletor existente escolher AAC adaptativo.
class AudioTransport final : public yt::Transport {
  yt::Transport& source;
public:
  explicit AudioTransport(yt::Transport& net) : source(net) {}
  yt::Response request(const std::string& url, const std::string& body = {}, bool form = false) override {
    yt::Request q; q.url = url; q.body = body; q.form = form; return send(q);
  }
  static std::string audioResponse(std::string body) {
    yt::catalog::J data;
    if (yt::catalog::parse(body,data)) {
      auto root = data.get<yt::catalog::Object>();
      const auto& streams = yt::catalog::get(data,"streamingData");
      if (streams.is<yt::catalog::Object>()) {
        auto sd = streams.get<yt::catalog::Object>(); sd.erase("formats");
        root["streamingData"] = yt::catalog::J(sd); body = yt::catalog::J(root).serialize();
      }
    }
    return body;
  }
  yt::Response send(const yt::Request& q) override {
    auto r = source.send(q);
    if (q.url.find("/youtubei/v1/player?") == std::string::npos || r.status != 200) return r;
    r.body = audioResponse(std::move(r.body));
    return r;
  }
};

struct Player : player::Player {
  bool audioOnly = false, playingAudio = false, serverAudio = false;
  uint64_t drainAt = 0;
  std::atomic<bool> audioDone{false};
  std::atomic<int> audioFailure{0};
  std::atomic<double> audioTail{0};

  void start(const net::Server& server, const std::string& video, double at, const std::string& name, double length,
             int height, bool speaker, bool tryDirect = true, bool renewing = false, int mode = -1, const std::string& track = {}, bool members = false) {
    halt(!speaker);
    memberVideo = members || (video == id && memberVideo);
    playingAudio = audioOnly;
    serverAudio = !tryDirect || (speaker && !directSpeaker.configured());
    if (!playingAudio) { player::Player::start(server,video,at,name,length,height,speaker,tryDirect,renewing,mode,track,members); return; }
    if (mode >= 0) videoMode = mode;
    if (!track.empty()) audioSelection = track; else if (video != id) audioSelection = "original";
    audioTracks.clear(); selectedAudio.clear(); tracksReady = false; adapted = false;
    presentFps = playback::rate(videoMode); actualFps = 0;
    srv = server; id = video; startAt = std::max(0.0,at); lastPos = startAt; title = name; duration = length; quality = height;
    stop = false; paused = false; live = false; direct = !serverAudio; original = !serverAudio; state = player::CONNECTING;
    speakerRequested = speaker; nest = false; nestFailed = false; renew = retryConverted = mediaReady = false;
    castMediaReady = false; decEnded = false;
    validUntil = 0; cachedQuality = 0;
    actualHeight = 0; error.clear(); audioDone = false; audioFailure = 0; audioTail = 0; hasAudio = false; drainAt = 0;
    firstPts = -1; wallStart = pausedAt = pausedTotal = 0; refill = reservePaused = false; deviceDrained();
    openedMs = SDL_GetTicks(); droppedFrames = shownFrames = 0;
    output = speaker || !directSpeaker.sample().quiet ? CAST : LOCAL;
    readyMs = tuneAt = 0; screenSec = -1; tickMs = 0; tracking = false;
    masterMs = speakerTargetMs = trimMs = cueMs = -1;
    armed = mutedForCast = localPlaying = pictureReady = false; mutedAt = 0;
    speakerPause = nestCmd = 0;
    SDL_LockMutex(nmu); nestState = 0; nestPos = nestStampMs = 0; SDL_UnlockMutex(nmu);
    SDL_SetAtomicInt(&audioBaseMs,-1); SDL_SetAtomicInt(&pushed,0); SDL_SetAtomicInt(&netBytes,0);
    if (audio) { SDL_PauseAudioStreamDevice(audio); SDL_ClearAudioStream(audio); SDL_SetAudioStreamFrequencyRatio(audio,1); }
    tvSound(false);
    if (serverAudio) {
      allowOriginal = true; endLiveReader = false; netEof = netErr = false; rhead = rcount = 0;
      ring.assign(6u << 20,0);
      netThread = SDL_CreateThread(netMain,"music-server",this);
    }
    decThread = yt::worker([](void* p) {
      auto& player = *static_cast<Player*>(p);
      if (player.serverAudio) player.decodeServerAudio(); else player.decodeAudio();
      return 0;
    },"music-aac",this);
    if (!decThread) { error = "console"; state = player::FAILED; }
    nestThread = yt::worker(nestMain,"music-nest",this);
  }
  void decodeAudio() {
    player::moveToHardwareThread(2);
    auto net = yt::catalog::transport(&stop);
    AudioTransport audioNet(*net); yt::Client client(audioNet,cacheBase);
    uint64_t decoded = 0, work = 0;
    int result = 0;
    for (int attempt = 0; attempt < 2 && !stop; ++attempt) {
      client.setLanguage(requestLanguage);
      media = client.player(id,quality,nullptr,videoMode,audioSelection,AudioTransport::audioResponse(memberResponse(&client)));
      if (stop) return;
      if (media.status != "OK" || media.audio.url.empty()) {
        if (media.reason == "members") error = "Vídeo só para membros do canal";
        audioFailure = AVERROR_STREAM_NOT_FOUND; return;
      }
      if (!attempt) { audioTracks = media.audioTracks; selectedAudio = media.selectedAudio; tracksReady = true; }
      mediaReady = true; castMediaReady = true;
      yt::RangeFile file(yt::catalog::transport(&stop),media.audio.url,&netBytes);
      h264::Stream stream;
      result = audio ? stream.open(&file,yt::RangeFile::pull,1,true,false,yt::RangeFile::seek) : AVERROR(ENODEV);
      if (result >= 0) result = stream.seekTime(startAt + audioTail.load());
      if (result >= 0) { hasAudio = true; state = player::BUFFERING; }
      uint64_t since = SDL_GetTicks();
      auto receive = [&](AVPacket* packet) {
        uint64_t begin = SDL_GetTicksNS();
        int r = avcodec_send_packet(stream.audio,packet);
        if (r < 0) return r;
        while (!stop) {
          r = avcodec_receive_frame(stream.audio,stream.frame);
          if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
          if (r < 0) return r;
          double pts = stream.pts(stream.ai)-startAt;
          int count = stream.convert(stream.frame);
          if (!std::isfinite(pts) || count < 0 || stream.frame->decode_error_flags) return AVERROR_INVALIDDATA;
          // A retomada após URL vencida descarta apenas o pré-roll já entregue ao SDL.
          double tail = audioTail.load();
          int skip = int(std::min<double>(count,std::max(0.0,std::ceil((tail-pts)*44100))));
          if (skip < count) {
            if (skip) std::memmove(stream.pcm.data(),stream.pcm.data()+2*skip,size_t(count-skip)*2*sizeof(float));
            pts += skip/44100.0; count -= skip;
            if (!putOriginalAudio(stream,count,pts)) return AVERROR_INVALIDDATA;
            audioTail = pts + count/44100.0;
          }
          ++decoded;
          work += SDL_GetTicksNS()-begin;
          while (!stop && audioQueuedSec() > 2.0) { trimReserve(); SDL_Delay(8); }
          begin = SDL_GetTicksNS();
        }
        return r == AVERROR(EAGAIN) || r == AVERROR_EOF ? 0 : r;
      };
      while (result >= 0 && !stop) {
        result = av_read_frame(stream.fmt,stream.pkt);
        if (result == AVERROR_EOF && (!stream.io->error || stream.io->error == AVERROR_EOF)) {
          result = receive(nullptr);
          if (result >= 0) {
            double pts = audioTail.load(); int count = stream.convert(nullptr);
            if (count < 0 || !putOriginalAudio(stream,count,pts)) result = AVERROR_INVALIDDATA;
            else audioTail = pts+count/44100.0;
          }
          break;
        }
        if (result < 0) break;
        if (stream.pkt->stream_index == stream.ai) result = receive(stream.pkt);
        av_packet_unref(stream.pkt);
        if (SDL_GetTicks()-since >= 5000) {
          LOG("musica: AAC itag %d, video 0 quadros, CPU %.2f ms/AAC, PCM %.0f ms",media.audio.itag,decoded ? work/1e6/decoded : 0,audioQueuedSec()*1000);
          since = SDL_GetTicks();
        }
      }
      if (result >= 0 && media.duration > 0 && startAt+audioTail.load()+1.25 < media.duration) result = AVERROR_INVALIDDATA;
      if (file.status != 403 || attempt) break;
    }
    if (stop) return;
    LOG("musica: fim AAC %llu quadros, video 0, PCM %.3f s, resultado %d",(unsigned long long)decoded,audioTail.load(),result);
    if (result < 0) audioFailure = result;
    else audioDone = true;
  }
  void decodeServerAudio() {
    player::moveToHardwareThread(2);
    SDL_LockMutex(mu);
    while (!rcount && !netEof && !netErr && !stop) SDL_WaitCondition(cv,mu);
    SDL_UnlockMutex(mu);
    if (stop) return;
    int result = 0;
    if (original) {
      h264::Stream stream;
      result = stream.open(this,readOriginal,1,true,false);
      hasAudio = result >= 0;
      double origin = NAN;
      auto receive = [&](AVPacket* packet) {
        int r = avcodec_send_packet(stream.audio,packet);
        if (r < 0) return r;
        while (!stop) {
          r = avcodec_receive_frame(stream.audio,stream.frame);
          if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return 0;
          if (r < 0) return r;
          double pts = stream.pts(stream.ai);
          if (!std::isfinite(pts)) return AVERROR_INVALIDDATA;
          if (!std::isfinite(origin)) origin = pts;
          pts -= origin;
          int count = stream.convert(stream.frame);
          if (count < 0 || !putOriginalAudio(stream,count,pts)) return AVERROR_INVALIDDATA;
          audioTail = pts + count/44100.0;
          while (!stop && audioQueuedSec() > 2) SDL_Delay(8);
        }
        return AVERROR_EXIT;
      };
      while (result >= 0 && !stop) {
        result = av_read_frame(stream.fmt,stream.pkt);
        if (result == AVERROR_EOF && (!stream.io->error || stream.io->error == AVERROR_EOF)) {
          result = receive(nullptr);
          if (result >= 0) {
            double pts = audioTail.load(); int count = stream.convert(nullptr);
            if (count < 0 || !putOriginalAudio(stream,count,pts)) result = AVERROR_INVALIDDATA;
            else audioTail = pts+count/44100.0;
          }
          break;
        }
        if (result < 0) break;
        if (stream.pkt->stream_index == stream.ai) result = receive(stream.pkt);
        av_packet_unref(stream.pkt);
      }
    } else {
      auto* buffer = plm_buffer_create_with_capacity(512 << 10);
      plm_buffer_set_load_callback(buffer,loadCb,this);
      auto* decoder = plm_create_with_buffer(buffer,1);
      // A reserva MPEG-PS do PC também descarta a imagem antes de decodificar.
      plm_set_video_enabled(decoder,0);
      while (!stop && !plm_has_headers(decoder)) {
        SDL_LockMutex(mu); bool ended = (netEof || netErr) && !rcount; SDL_UnlockMutex(mu);
        if (ended) break;
        SDL_Delay(5);
      }
      hasAudio = audio && plm_get_num_audio_streams(decoder) > 0;
      result = hasAudio ? 0 : AVERROR_STREAM_NOT_FOUND;
      while (result >= 0 && !stop) {
        auto* samples = plm_decode_audio(decoder);
        if (!samples) {
          if (plm_has_ended(decoder)) break;
          SDL_Delay(2); continue;
        }
        SDL_LockAudioStream(audio);
        bool ok = SDL_PutAudioStreamData(audio,samples->interleaved,int(samples->count*2*sizeof(float)));
        if (SDL_GetAtomicInt(&audioBaseMs) < 0) SDL_SetAtomicInt(&audioBaseMs,int(std::lround(samples->time*1000)));
        SDL_AddAtomicInt(&pushed,int(samples->count)); SDL_UnlockAudioStream(audio);
        if (!ok) { result = AVERROR(EIO); break; }
        audioTail = samples->time + samples->count/44100.0;
        while (!stop && audioQueuedSec() > 2) SDL_Delay(8);
      }
      plm_destroy(decoder);
    }
    if (stop) return;
    if (result >= 0 && (audioTail <= 0 || (!live && duration > 0 && startAt+audioTail.load()+1.25 < duration))) result = AVERROR_INVALIDDATA;
    LOG("musica: reserva PC %s, video 0 quadros, PCM %.3f s, resultado %d",original ? "AAC" : "MP2",audioTail.load(),result);
    SDL_LockMutex(mu); bool failed = netErr; SDL_UnlockMutex(mu);
    if (result < 0 || failed) audioFailure = result < 0 ? result : AVERROR(EIO);
    else audioDone = true;
  }
  player::Frame* take() {
    if (!playingAudio) return player::Player::take();
    if (audioFailure && !serverAudio && net::serverEnabled && error.empty()) {
      auto server = srv; auto video = id, name = title;
      double at = position(), length = duration; int height = quality; bool hold = paused;
      LOG("musica: reserva no PC em %.3f s",at);
      start(server,video,at,name,length,height,false,false); setPaused(hold); return nullptr;
    }
    if (audioFailure) { if (error.empty()) error = "console"; state = player::FAILED; if (audio) SDL_PauseAudioStreamDevice(audio); return nullptr; }
    int external = speakerPause.exchange(0);
    if (external) setPaused(external == 1,false);
    if (paused || state == player::FAILED || state == player::ENDED) return nullptr;
    uint64_t now = SDL_GetTicks();
    wantedOffsetMs = int(std::lround(nestOffset*1000));
    double queued = audioQueuedSec();
    double speaker = -1;
    auto route = output.load();
    if (speakerRequested && route == CAST) {
      SDL_LockMutex(nmu);
      if (nestState == 1 && now-nestStampMs < (direct ? 8000 : 650)) speaker = nestPos+(now-nestStampMs)/1000;
      SDL_UnlockMutex(nmu);
    }
    if (firstPts < 0 && (queued >= .12 || (audioDone && queued > 0))) {
      pictureReady = true;
      if (!readyMs) readyMs = now;
      if (!speakerRequested || speaker >= 0 || now-readyMs >= 1400) {
        firstPts = 0; wallStart = now; tickMs = now;
        screenSec = speaker >= 0 ? std::max(0.0,speaker+nestOffset) : 0;
        SDL_ResumeAudioStreamDevice(audio); state = player::PLAYING;
        LOG("musica: primeiro audio em %llu ms, video 0 quadros",(unsigned long long)(wallStart-openedMs));
      }
    }
    if (firstPts >= 0) {
      double tv = tvClock(0,queued), dt = std::max(0.0,(now-tickMs)/1000);
      double predicted = screenSec+dt;
      bool follow = speaker >= 0 && route == CAST;
      bool local = route == LOCAL || route == PRIMING;
      double cue = cueMs/1000.0;
      if (route == PRIMING && (mutedAt || (audible ? tv : predicted) >= cue-deviceMs/1000.0)) {
        local = false; if (!mutedAt) mutedAt = now;
      } else if (route != PRIMING) mutedAt = 0;
      double target = follow ? speaker+nestOffset : audible ? tv : predicted;
      screenSec = speakerRequested && !audible ? predicted+std::clamp(target-predicted,-dt*.15,dt*.15) : tv;
      if (route == PRIMING && cue >= 0) screenSec = std::min(screenSec,std::max(cue,predicted-dt));
      if (follow && mutedForCast.exchange(false)) screenSec = std::max(screenSec,speaker+nestOffset);
      tickMs = now; speakerTargetMs = masterMs = int(std::lround(screenSec*1000));
      bool on = local && (audible || std::fabs(tv-screenSec) <= .022);
      if (route == PRIMING && armed && mutedAt && now-mutedAt >= uint64_t(deviceMs) && deviceDrained() &&
          std::fabs(screenSec-cue) < .020 && audioTail.load()-cue >= .18) mutedForCast = true;
      if (on != audible || follow != tracking) LOG("som: %s, Nest %.3f TV %.3f musica %.3f",on ? "TV" : follow ? "Nest" : "aguardando",speaker,tv,screenSec);
      tracking = follow; tvSound(on); localPlaying = audible.load(); nest = follow;
      if (!on && screenSec-tv > .025) trimMs = int(std::lround(screenSec*1000));
      bool hold = !on && tv-screenSec > (reservePaused ? .010 : .035);
      if (hold != reservePaused) {
        reservePaused = hold;
        if (hold) SDL_PauseAudioStreamDevice(audio); else SDL_ResumeAudioStreamDevice(audio);
      }
      if (now >= tuneAt) {
        tuneAt = now+120;
        SDL_SetAudioStreamFrequencyRatio(audio,float(speakerRequested && !on ? 1+std::clamp((screenSec-tv)*1.2,-.35,.08) : 1));
      }
      lastPos = startAt+std::min(audioTail.load(),screenSec);
    }
    if (audioDone && queued <= 0 && firstPts >= 0 && (!speakerRequested || screenSec >= audioTail.load()-.02)) {
      if (!drainAt) drainAt = SDL_GetTicks();
      if (SDL_GetTicks()-drainAt >= uint64_t(deviceMs+10)) { lastPos = startAt+audioTail.load(); state = player::ENDED; }
    }
    return nullptr;
  }
  void halt(bool stopSpeaker = true) {
    player::Player::halt(stopSpeaker); playingAudio = false;
  }
};
}
