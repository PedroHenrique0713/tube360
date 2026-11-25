#pragma once
namespace player {
inline void Player::directNestLoop() {
  // Nest desligado ou fora da rede nunca confirma silêncio: depois de 10 s sem contato a TV volta sem a confirmação.
  constexpr uint64_t speakerGoneMs = 10000;
  while (!stop && !castMediaReady && state != FAILED) SDL_Delay(10);
  if (stop || state == FAILED) return;
  if (net::serverEnabled && speakerRequested) {
    uint64_t deadline = SDL_GetTicks()+3500;
    bool claimed = false;
    LOG("Nest: dono=console; solicitando silencio e fechamento do servidor");
    while (!stop && SDL_GetTicks() < deadline) {
      net::Response reply;
      bool answered = net::get(srv,"/api/nest/owner?owner=console",reply,700,1024);
      if (answered &&
          std::string(reply.body.begin(),reply.body.end()).find("QUIET\t") == 0) { claimed = true; break; }
      if (!answered && !reply.status && previousSession == 0) {
        LOG("Nest: servidor ausente, sem sessao de reserva anterior; console direto");
        claimed = true; break;
      }
      if (reply.status == 404) {
        LOG("Nest: servidor sem controle de posse (versao antiga); console direto");
        claimed = true; break;
      }
      SDL_Delay(100);
    }
    previousSession = 0;
    if (stop) return;
    // O Nest nunca depende do PC: sem confirmação, o console assume (o LAUNCH dele encerra a sessão do servidor)
    // e a escolha da TV nunca fica presa esperando o servidor.
    if (!claimed) LOG("Nest: servidor nao confirmou em 3500 ms; console assume");
    LOG("Nest: dono=console autorizado");
  }
  // HLS não oferece um relógio comum entre duas aberturas independentes do manifesto.
  const auto& audioUrl = media.speakerAudio.url;
  bool supported = speakerRequested && !live && !audioUrl.empty();
  if (!supported) {
    if (speakerRequested && live) LOG("Nest: live na TV; HLS sem origem comum entre imagem e relogio Cast");
    directSpeaker.release(); output = RELEASING;
    uint64_t heard = SDL_GetTicks();
    while (!stop) {
      auto s = directSpeaker.sample();
      if (s.quiet) break;
      if (s.connected) heard = SDL_GetTicks();
      else if (SDL_GetTicks()-heard > speakerGoneMs) { LOG("som: Nest sem contato há %llu ms; volta para a TV", (unsigned long long)speakerGoneMs); break; }
      SDL_Delay(20);
    }
    if (!stop) {
      output = LOCAL;
      if (speakerRequested) speakerFallback = true;
    }
    return;
  }
  while (!stop && (!pictureReady || !deviceDrained())) SDL_Delay(10);
  if (stop) return;
  uint64_t generation = directSpeaker.play(audioUrl,id,startAt,media.duration,!paused);
  uint64_t requested = SDL_GetTicks(), localSince = 0, lastAttempt = requested;
  bool starting = false, failedStart = false;
  int last = 0, expected = 0;
  uint64_t heard = 0;
  auto release = [&]() {
    directSpeaker.release(); output = RELEASING; armed = false; heard = SDL_GetTicks();
    SDL_LockMutex(nmu); nestState = 0; SDL_UnlockMutex(nmu);
  };
  while (!stop) {
    uint64_t now = SDL_GetTicks();
    auto sample = directSpeaker.sample();
    if (output == RELEASING) {
      if (sample.connected) heard = now;
      bool gone = !sample.quiet && !sample.connected && now-heard > speakerGoneMs;
      if (sample.quiet || gone) {
        armed = mutedForCast = false; cueMs = -1;
        output = LOCAL; localSince = now; last = expected = 0;
        if (gone) LOG("som: Nest sem contato há %llu ms; volta para a TV", (unsigned long long)speakerGoneMs);
        else LOG("som: Cast confirmou silêncio");
        speakerFallback = true;
      }
      SDL_Delay(20); continue;
    }
    if (output == LOCAL) {
      if (!failedStart && !paused && localPlaying && now-localSince > 1200 && now-lastAttempt > 5000 && speakerTargetMs >= 0) {
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
      else if (paused || now-requested > 3500 || (!starting && speakerTargetMs > cueMs+40) || st == 3 || st == 4) release();
    } else {
      if (st == 1 || st == 2) {
        if (expected == st) expected = 0;
        else if (!expected && last && last != st) speakerPause = st == 2 ? 1 : 2;
        last = st;
      }
      bool expired = sample.observed > 0 && now-sample.observed > 25000;
      bool neverStarted = !sample.updated && now-requested > 25000;
      if (st == 3 || st == 4 || expired || neverStarted) {
        if (!last) failedStart = true;
        LOG("Nest: console erro/status=%d; liberando para TV (%s)",st,
            expired ? "sem noticia por 25 s" : neverStarted ? "sem relogio por 25 s" : "estado do receptor");
        release();
      }
    }
    SDL_Delay(20);
  }
}
}
