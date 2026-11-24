#pragma once
namespace player {
inline void Player::directNestLoop() {
  while (!stop && !castMediaReady && state != FAILED) SDL_Delay(10);
  if (stop || state == FAILED) return;
  if (previousSession > 0) {
    while (!stop) {
      net::Response reply;
      if (net::get(srv,"/api/nest/local?sid="+std::to_string(previousSession),reply,900,1024) &&
          std::string(reply.body.begin(),reply.body.end()).find("QUIET\t") == 0) break;
      SDL_Delay(100);
    }
    previousSession = 0;
    if (stop) return;
  }
  // HLS não oferece um relógio comum entre duas aberturas independentes do manifesto.
  const auto& audioUrl = media.speakerAudio.url;
  bool supported = speakerRequested && !live && !audioUrl.empty();
  if (!supported) {
    directSpeaker.release(); output = RELEASING;
    while (!stop && !directSpeaker.sample().quiet) SDL_Delay(20);
    if (!stop) {
      output = LOCAL;
      if (speakerRequested) { nestFailed = true; state = FAILED; }
    }
    return;
  }
  while (!stop && (!pictureReady || !deviceDrained())) SDL_Delay(10);
  if (stop) return;
  uint64_t generation = directSpeaker.play(audioUrl,id,startAt,media.duration,!paused);
  uint64_t requested = SDL_GetTicks(), localSince = 0, lastAttempt = requested;
  bool starting = false;
  int last = 0, expected = 0;
  auto release = [&]() {
    directSpeaker.release(); output = RELEASING; armed = false;
    SDL_LockMutex(nmu); nestState = 0; SDL_UnlockMutex(nmu);
  };
  while (!stop) {
    uint64_t now = SDL_GetTicks();
    auto sample = directSpeaker.sample();
    if (output == RELEASING) {
      if (sample.quiet) {
        armed = mutedForCast = false; cueMs = -1;
        output = LOCAL; localSince = now; last = expected = 0;
        LOG("som: Cast confirmou silêncio");
      }
      SDL_Delay(20); continue;
    }
    if (output == LOCAL) {
      if (!paused && localPlaying && now-localSince > 1200 && now-lastAttempt > 5000 && speakerTargetMs >= 0) {
        cueMs = int(std::lround(std::round((speakerTargetMs+2000)*.030)*(1000.0/30)));
        output = PRIMING; armed = mutedForCast = false; starting = false;
        generation = directSpeaker.play(audioUrl,id,startAt+(cueMs-wantedOffsetMs)/1000.0,media.duration,false);
        requested = lastAttempt = now;
      }
      SDL_Delay(20); continue;
    }
    int cmd = nestCmd.exchange(0);
    if (cmd && output == CAST) { directSpeaker.pause(cmd == 1); expected = cmd == 1 ? 2 : 1; }
    if (sample.generation != generation) { SDL_Delay(20); continue; }
    int st = sample.state == nest::PLAYING ? 1 : sample.state == nest::PAUSED ? 2 : sample.state == nest::IDLE ? 3 :
             sample.state == nest::ERROR || sample.state == nest::STALLED ? 4 : sample.state == nest::BUFFERING ? 5 : 0;
    SDL_LockMutex(nmu);
    nestState = st; nestPos = sample.position-startAt; nestSeenMs = nestStampMs = sample.updated;
    SDL_UnlockMutex(nmu);
    if (output == PRIMING) {
      if (st == 2) armed = true;
      if (mutedForCast && armed && !starting) { directSpeaker.pause(false); starting = true; requested = now; }
      if (starting && st == 1) { output = CAST; last = 1; expected = 0; }
      else if (paused || now-requested > 4500 || st == 3 || st == 4) release();
    } else {
      if (st == 1 || st == 2) {
        if (expected == st) expected = 0;
        else if (!expected && last && last != st) speakerPause = st == 2 ? 1 : 2;
        last = st;
      }
      bool expired = sample.updated > 0 && now-sample.updated > 8000;
      bool neverStarted = !sample.updated && now-requested > 5000;
      if (st == 3 || st == 4 || expired || neverStarted) release();
    }
    SDL_Delay(20);
  }
}
}
