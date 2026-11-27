// Player de vídeo: controles, barra de progresso, painéis (qualidade, legendas, comentários, velocidade, som), "a seguir",
// avaliação e as dicas por botão. Shorts abrem neste mesmo player.
#pragma once
#include "ui_context.hpp"

inline void UiContext::playerPanelInput(uint64_t now, const display::CommentsPanel& commentPanel) {
  if (in.hit(Input::B)) playerUI.panel = PlayerUI::NONE;
  else if (playerUI.panel == PlayerUI::COMMENTS_PANEL) {
    int count=(int)extras.page.items.size();
    if (in.hit(Input::DOWN)) {
      if (extras.selected<count && extras.line+commentPanel.lines<(int)commentLines.size()) ++extras.line;
      else if (extras.selected+1<count || !extras.page.continuation.empty()) { extras.selected=std::min(count,extras.selected+1); extras.line=0; }
    }
    if (in.hit(Input::UP)) {
      if (extras.line>0) --extras.line;
      else { extras.selected=std::max(0,extras.selected-1); extras.line=0; }
    }
    if (in.hit(Input::A) && !extras.loading && (!extras.page.error.empty() || extras.selected>=count)) extras.request(!extras.page.continuation.empty());
  } else if (playerUI.panel == PlayerUI::SPEED_PANEL) {
    if (in.hit(Input::UP) || in.hit(Input::LEFT)) playerUI.panelSel=(playerUI.panelSel+7)%8;
    if (in.hit(Input::DOWN) || in.hit(Input::RIGHT)) playerUI.panelSel=(playerUI.panelSel+1)%8;
    if (in.hit(Input::A) && !pl.live && !pl.waiting()) {
      double speed=audio::speeds[playerUI.panelSel], pos=pl.position(); bool paused=pl.paused;
      bool changed=pl.speed!=speed;
      if (changed) {
        pl.halt(); pl.speed=speed;
        startVideo(playing,pos,playerFromRemote); pl.setPaused(paused);
        LOG("velocidade: %.2fx em %.3f, som TV=%d",speed,pos,speed!=1);
        if (nestOut && speed!=1) { toast=tr(K::SpeedTV); toastUntil=now+5000; }
      }
      playerUI.panel=PlayerUI::NONE; playerUI.options=true;
    }
  } else if (playerUI.panel == PlayerUI::CAPTIONS) {
    if (in.hit(Input::UP)) playerUI.panelSel = 0;
    if (in.hit(Input::DOWN)) playerUI.panelSel = 1;
    int direction = in.hit(Input::LEFT) ? -1 : in.hit(Input::RIGHT) ? 1 : 0;
    if (captions.failed && in.hit(Input::A) && !captionLoader.loading)
      captionLoader.request(srv, playing.id, i18n::code(settings.lang), settings.captionLang, nullptr, pl.direct);
    else if (!playerUI.panelSel && !captions.cues.empty() && (direction || in.hit(Input::A))) {
      settings.captions = !settings.captions; saveSettings();
    } else if (playerUI.panelSel && !captions.tracks.empty() && !captionLoader.loading && (direction || in.hit(Input::A))) {
      int index = 0;
      for (size_t i = 0; i < captions.tracks.size(); i++) if (captions.tracks[i].code == captions.selected) index = (int)i;
      index = (index + (direction ? direction : 1) + (int)captions.tracks.size()) % (int)captions.tracks.size();
      settings.captionLang = captions.tracks[(size_t)index].code; settings.captions = true; saveSettings();
      captionLoader.request(srv, playing.id, i18n::code(settings.lang), settings.captionLang, nullptr, pl.direct);
      captions.cues.clear(); captionIndex = -1; captionLines.clear();
    }
  } else if (playerUI.panel == PlayerUI::SOUND) {
    SoundPanel sound{true, playerUI.panelSel};
    soundPanelInput(sound, now);
    playerUI.panelSel = sound.sel;
    if (!sound.open) { playerUI.options = true; playerUI.panel = PlayerUI::NONE; }
  } else {
    int count = playerUI.panel == PlayerUI::QUALITY ? offeredQualities.count : playerUI.panel==PlayerUI::TRACKS ? (int)audioTracks.size() : 2;
    if (in.hit(Input::UP) || in.hit(Input::LEFT)) playerUI.panelSel = (playerUI.panelSel + count - 1) % count;
    if (in.hit(Input::DOWN) || in.hit(Input::RIGHT)) playerUI.panelSel = (playerUI.panelSel + 1) % count;
    if (in.hit(Input::A)) {
      bool change = false;
      if (playerUI.panel == PlayerUI::QUALITY) {
        int mode=offeredQualities.modes[playerUI.panelSel];
        change = mode != settings.videoMode; settings.videoMode=mode;
      } else if (playerUI.panel==PlayerUI::TRACKS && playerUI.panelSel < (int)audioTracks.size()) {
        bool wasPaused=pl.paused;
        std::string track=audioTracks[playerUI.panelSel].code;
        double position=pl.position();
        startVideo(playing,pl.live ? 0 : position,playerFromRemote,track);
        if (wasPaused) pl.setPaused(true);
        LOG("audio: faixa %s em %.3f s",track.c_str(),position);
      } else if (playerUI.panelSel && pl.speed!=1) { toast=tr(K::SpeedNest); toastUntil=now+3500;
      } else if (!playerUI.panelSel || nestAvailable) {
        bool speaker = playerUI.panelSel == 1;
        change = speaker != pl.nest; nestOut = speaker; nestSkip = false;
      } else { toast = tr(K::NoSpeaker); toastUntil = now + 3000; }
      if (change) { bool paused=pl.paused;startVideo(playing, pl.live ? 0 : pl.position(), playerFromRemote);pl.setPaused(paused);saveSettings(); }
      playerUI.options = true; playerUI.panel = PlayerUI::NONE;
    }
  }
  playerUI.touch(now);
}

// Painel de som, compartilhado com o Tocando agora da Música: linhas TV / caixa (Nest) / volume. ↑↓ escolhem a linha, ←→ na linha de
// volume ajustam (TV: grava o salvo; Nest: a caixa ao vivo, sem gravar) e nas outras alternam TV/caixa, A aplica a saída, B fecha.
inline void UiContext::soundPanelInput(SoundPanel& p, uint64_t now) {
  int vertical = in.hit(Input::DOWN) ? 1 : in.hit(Input::UP) ? -1 : 0;
  int horizontal = in.hit(Input::LEFT) ? -1 : in.hit(Input::RIGHT) ? 1 : 0;
  if (g.rtlText) horizontal = -horizontal;
  if (vertical) p.sel = std::clamp(p.sel + vertical, 0, 2);
  if (horizontal) { if (p.sel == 2) adjustVolume(now, horizontal); else p.sel = 1 - p.sel; }
  if (in.hit(Input::B)) { p.open = false; return; }
  if (in.hit(Input::A) && p.sel < 2) {
    bool change = false;
    if (p.sel && pl.speed != 1) { toast = tr(K::SpeedNest); toastUntil = now + 3500; }
    else if (!p.sel || nestAvailable) { bool speaker = p.sel == 1; change = speaker != pl.nest; nestOut = speaker; nestSkip = false; }
    else { toast = tr(K::NoSpeaker); toastUntil = now + 3000; }
    if (change) { bool paused = pl.paused; startVideo(playing, pl.live ? 0 : pl.position(), playerFromRemote); pl.setPaused(paused); saveSettings(); }
    p.open = false;
  }
}

inline void UiContext::soundPanelDraw(const SoundPanel& p, uint64_t now) {
  (void)now;
  const float w = 720, h = 330, x = (ui::W - w) / 2, y = 190;
  gfx::rect(g, 0, 0, ui::W, ui::H, {0, 0, 0, 0.55f});
  gfx::shadow(g, x, y + 10, w, h, 24, 32, 0.6f);
  gfx::rect(g, x, y, w, h, ui::SURFACE, 24);
  gfx::text(g, ui::F_BIG, tr(K::Sound), x + 36, y + 28, ui::TEXT);
  for (int i = 0; i < 3; ++i) {
    float ry = y + 90 + (float)i * 58;
    bool focused = p.sel == i, disabled = i == 1 && !nestAvailable;
    gfx::rect(g, x + 30, ry, w - 60, 50, focused ? ui::TEXT : ui::SURFACE2, 10);
    Color ink = disabled ? ui::FAINT : focused ? ui::INK : ui::TEXT;
    if (i < 2) {
      gfx::text(g, ui::F_BODY, gfx::ellipsize(g, ui::F_BODY, i ? nestName : std::string(tr(K::TV)), w - 132), x + 50, ry + 14, ink);
      // a saída em uso fica marcada
      if ((i == 1) == pl.nest) gfx::rect(g, x + w - 62, ry + 19, 12, 12, rgb(0x22c55e), 6);
    } else {
      const bool speaker = (pl.nest || pl.speakerRequested || nestOut) && pl.directSpeaker.configured();
      int value = speaker ? std::max(0, (int)std::lround((pl.directSpeaker.sample().actual >= 0 ? pl.directSpeaker.sample().actual : pl.directSpeaker.sample().volume) * 100)) : settings.volume;
      gfx::text(g, ui::F_BODY, speaker ? gfx::ellipsize(g, ui::F_BODY, nestName, 190) : std::string(tr(K::SettingVolume)), x + 50, ry + 14, ink);
      const float bx = x + 230, bw = w - 60 - 200 - 90;
      gfx::rect(g, bx, ry + 22, bw, 6, focused ? Color{0, 0, 0, 0.25f} : Color{1, 1, 1, 0.25f}, 3);
      if (value > 0) gfx::rect(g, bx, ry + 22, bw * (float)value / 100.f, 6, focused ? ui::INK : ui::TEXT, 3);
      gfx::text(g, ui::F_BODY, std::to_string(value), x + w - 50, ry + 14, ink, 2);
    }
  }
  float hy = y + h - 42, hx = hint(g, x + 36, hy, tr(K::BtnVertical), XG, tr(K::ChooseRow));
  hx = hint(g, hx, hy, tr(K::BtnHorizontal), XG, tr(K::ChangeValue));
  hx = hint(g, hx, hy, "A", XA, tr(K::SleepApply));
  hint(g, hx, hy, "B", XB, tr(K::Back));
}

inline void UiContext::playerNextInput() {
  in.hit(Input::BACK);  // View não tem papel no cartão de próximo automático (mapa único #40)
  if (ctl::hit(in, ctl::Ctx::Player, ctl::Act::Options)) { nextAt = 0; playerUI.options = true; playerUI.touch(SDL_GetTicks()); }  // Menu = opções, não inicia o próximo
  else if (in.hit(Input::A)) { Video v = upNext; startVideo(v, 0, false); }
  else if (in.hit(Input::B)) { nextAt = 0; stopVideo(); }
}

inline void UiContext::playerInput(uint64_t now, int optionCount) {
  if (playing.shortForm) { shortsInput(now); return; }
  if(seekHold.active)for(int b=0;b<Input::N;++b)if(b!=Input::B && b!=Input::BACK)in.fired[b]=false;
  bool any = false;
  if (shelf.open) { playerShelfInput(now); return; }
  int horizontal = in.hit(Input::LEFT) ? -1 : in.hit(Input::RIGHT) ? 1 : 0;
  // Na opção 1 aprovada os ícones ficam ABAIXO da barra: ↓ leva aos ícones, ↑ volta para a trilha.
  int vertical = in.hit(Input::DOWN) ? 1 : in.hit(Input::UP) ? -1 : 0;
  double jump = 0;
  // ↓ na linha de ícones abre "a seguir e relacionados" sem parar o vídeo; sem lista, nada acontece.
  if (vertical > 0 && !horizontal && playerUI.visible(now) && playerUI.options && shelfFirstTab() >= 0 && !seekHold.active) {
    shelf.open = true; shelf.tab = shelfFirstTab(); shelf.sel = 0; vertical = 0; any = true; shelfAskChannel();
  }
  if (horizontal || vertical) { jump = playerUI.direction(now, horizontal, vertical, optionCount) * 10; any = true; }
  // Mapa único (#40): LB/RB = vídeo anterior/seguinte; avançar/voltar no tempo é LT/RT segurado.
  if (ctl::hit(in, ctl::Ctx::Player, ctl::Act::PrevItem) && playerStep(-1)) return;
  if (ctl::hit(in, ctl::Ctx::Player, ctl::Act::NextItem) && playerStep(1)) return;
  if (jump != 0) {
    if (pl.state == player::CONNECTING) {
      LOG("player: seek ignored: live status unknown (CONNECTING)");
      toast = tr(K::PlayerSeekWait); toastUntil = now + 2500;
    } else if (pl.live) {
      LOG("player: seek ignored: live");
      toast = tr(K::PlayerLiveSeek); toastUntil = now + 2500;
    } else if (pl.state != player::FAILED) {
      double to = std::clamp(pl.position() + jump, 0.0, pl.duration > 0 ? std::max(0.0, pl.duration - 3) : 1e9);
      startVideo(playing, to, playerFromRemote);
    }
    any = true;
  }
  bool pressA = in.hit(Input::A), open = pressA && playerUI.options;
  if (open && playerUI.selected != PlayerUI::TRANSPORT) {
    playerUI.panelSel = 0;
    if (playerUI.selected == PlayerUI::TIMER) { sleepPanel = true; playerUI.sleepRow = 0; }
    else if (playerUI.selected == PlayerUI::RATE || playerUI.selected == PlayerUI::DISLIKE) {
      if (account.signedIn()) {
        const char* want = playerUI.selected == PlayerUI::RATE ? "LIKE" : "DISLIKE";
        bool same = likeState == want;
        control.add({5, "/api/video/" + playing.id + "/rate?r=" + (same ? "none" : playerUI.selected == PlayerUI::RATE ? "like" : "dislike"), 0});
      } else { toast = tr(K::RateSignIn); toastUntil = now + 3500; }
    } else if (playerUI.selected == PlayerUI::CC) {
      if (captions.failed || captionLoader.loading || (!captions.live && !captions.tracks.empty())) playerUI.panel = PlayerUI::CAPTIONS;
    } else if (playerUI.selected == PlayerUI::VIDEO) {
      openQuality();
    } else if (playerUI.selected == PlayerUI::AUDIO) {
      playerUI.panel = PlayerUI::SOUND; playerUI.panelSel = pl.nest ? 1 : 0;
    } else if (playerUI.selected == PlayerUI::TRACK && audioTracks.size()>1) {
      playerUI.panel=PlayerUI::TRACKS;
      for (int i=0;i<(int)audioTracks.size();++i) if (audioTracks[i].code==selectedAudio) playerUI.panelSel=i;
    } else if (playerUI.selected == PlayerUI::COMMENTS) {
      playerUI.panel=PlayerUI::COMMENTS_PANEL;
      if (!extras.loaded) extras.request();
    } else if (playerUI.selected == PlayerUI::SPEED) {
      if (pl.live) { toast=tr(K::SpeedLive); toastUntil=now+3000; }
      else if (!pl.waiting()) { playerUI.panel=PlayerUI::SPEED_PANEL; playerUI.panelSel=audio::speedIndex(pl.speed); }
    } else if (playerUI.selected == PlayerUI::LOOP) {
      if (!pl.live) { pl.loop=!pl.loop; LOG("repetição: %d",pl.loop); }
      else { toast=tr(K::SpeedLive); toastUntil=now+3000; }
    } else if (playerUI.selected == PlayerUI::EDGE) {
      LOG("live: retorno manual à borda"); resumeLive();
    } else openPlayerChannel(captions.channelId, playing.id);
    any = true;
  }
  // Durante a abertura, A não cria uma pausa invisível atrás do carregamento.
  if (ctl::hit(in, ctl::Ctx::Player, ctl::Act::Options)) { playerUI.options = !playerUI.options || !playerUI.visible(now); any = true; }
  if ((pressA && (!open || playerUI.selected == PlayerUI::TRANSPORT)) && (!pl.waiting() || pl.paused)) {
    if (pl.live && pl.paused) resumeLive(); else pl.setPaused(!pl.paused);
    LOG("player: pause=%d pos=%.2f", pl.paused ? 1 : 0, pl.position());
    any = true;
  }
  if (ctl::hit(in, ctl::Ctx::Player, ctl::Act::Quality)) {
    auto action=(!pl.live && pl.state!=player::FAILED && (pl.state==player::PLAYING || sponsors.canUndo(now))) ? sponsors.press(pl.position(),pl.duration,now,settings.sponsorBlock) : sponsorblock::Action{};
    if (action) sponsorSeek(action);
    else { LOG("player: Y qualidade pos=%.3f modo=%d menu=1 SponsorBlock_ativo=0",pl.position(),settings.videoMode); openQuality(); }
    any=true;
  }
  if (ctl::hit(in, ctl::Ctx::Player, ctl::Act::WatchLater)) { playerWatchLater(now); any = true; }
  in.hit(Input::LT);in.hit(Input::RT);
  int trigger=in.held[Input::RT]==in.held[Input::LT]?0:in.held[Input::RT]?1:-1;
  if(trigger && (pl.live || pl.state==player::CONNECTING || pl.state==player::FAILED)) {
    if(pl.state!=player::FAILED){toast=tr(pl.live?K::PlayerLiveSeek:K::PlayerSeekWait);toastUntil=now+2500;}
  } else {
    bool wasActive=seekHold.active;
    bool commit=seekHold.update(now,trigger,pl.position(),pl.duration,pl.paused);
    if(!wasActive && seekHold.active){seekSecond=-1;pl.setPaused(true);LOG("busca: segurando %d desde %.3f",trigger,pl.position());}
    if(seekHold.active){playerUI.options=false;any=true;}
    if(commit){double to=seekHold.target;bool paused=seekHold.wasPaused;LOG("busca: soltou em %.3f",to);startVideo(playing,to,playerFromRemote);pl.setPaused(paused);any=true;}
  }
  if (any) playerUI.touch(now);
  in.hit(Input::BACK);  // View não tem papel no player (mapa único #40); sair é B
  if (in.hit(Input::B)) {
    if(seekHold.active){bool paused=seekHold.wasPaused;seekHold.cancel(true);pl.setPaused(paused);}
    else if(!playerUI.back(now)) {
    if (nav.musicSession) {
      double pos = pl.position(); bool paused = pl.paused;
      nav.musicNow = true; startVideo(playing,pos,playerFromRemote); pl.setPaused(paused);
    } else stopVideo();
    }
  }
  if (pl.state == player::FAILED && pl.nestFailed && !nestSkip) {
    nestSkip = true;
    toast = tr(K::SpeakerFallback); toastUntil = now + 5000;
    startVideo(playing, pl.position(), playerFromRemote);
  }
}

// X no player: salva o vídeo em "Assistir mais tarde" (na conta, se logada; senão na lista local), como nas demais telas.
inline void UiContext::playerWatchLater(uint64_t now) {
  if (playing.id.empty()) return;
  if (account.signedIn()) { nav.action(navWorker, "/api/nav/action?action=later&id=" + playing.id + "&lang=" + i18n::code(settings.lang)); return; }
  bool had = localUI.library.contains(playing.id);
  localUI.library.toggle(LocalLibraryUI::entry(playing));
  toast = tr(had ? K::PlayerLaterRemoved : K::PlayerLaterAdded); toastUntil = now + 2500;
}

// LB/RB: vídeo anterior/seguinte. Com fila local (playlist, Mais tarde, Biblioteca, Música em vídeo) andam na fila; sem fila, o
// "a seguir" e o histórico desta sessão. Sessão do celular: quem escolhe o próximo é o app do celular, então nada acontece.
// Devolve true se tratou o botão (mesmo sem trocar de vídeo, para não cair em outra ação).
inline bool UiContext::playerStep(int direction) {
  if (playerFromRemote || phonePlaybackSerial) return false;
  if (nav.localQueue) {
    size_t at = nav.queueIndex, count = nav.queue.size();
    if (direction > 0 && at + 1 < count) ++nav.queueIndex;
    else if (direction < 0 && at > 0) --nav.queueIndex;
    else { toast = tr(direction > 0 ? K::PlayerNoNext : K::PlayerNoPrev); toastUntil = SDL_GetTicks() + 2000; return true; }
    Video v = nav.queue[nav.queueIndex].video(); startVideo(v, 0, false);
    return true;
  }
  if (direction > 0) {
    const Video* next = !upNext.id.empty() ? &upNext : !related.empty() ? &related.front() : nullptr;
    if (!next) { toast = tr(K::PlayerNoNext); toastUntil = SDL_GetTicks() + 2000; return true; }
    Video v = *next; startVideo(v, 0, false);
    return true;
  }
  if (playedBefore.empty()) { toast = tr(K::PlayerNoPrev); toastUntil = SDL_GetTicks() + 2000; return true; }
  Video v = playedBefore.back(); playedBefore.pop_back();
  historyBack = true; startVideo(v, 0, false); historyBack = false;
  return true;
}

inline void UiContext::volumeHud(uint64_t now) {
  if (now < shelf.volumeUntil) {
    const float w = 300, h = 64, x = ui::W - w - 40, y = 36, frac = shelf.volumeShown / 100.f;
    gfx::rect(g, x, y, w, h, {0, 0, 0, 0.78f}, 14);
    gfx::text(g, ui::F_SMALL, shelf.volumeSpeaker ? gfx::ellipsize(g, ui::F_SMALL, nestName, 180) : std::string(tr(K::SettingVolume)), x + 20, y + 10, ui::MUTED);
    gfx::text(g, ui::F_SMALL, std::to_string(shelf.volumeShown), x + w - 20, y + 10, ui::TEXT, 2);
    gfx::rect(g, x + 20, y + 40, w - 40, 6, {1, 1, 1, 0.25f}, 3);
    if (frac > 0) gfx::rect(g, x + 20, y + 40, (w - 40) * frac, 6, ui::TEXT, 3);
  }
}

inline void UiContext::adjustVolume(uint64_t now, int direction) {
  if ((pl.nest || pl.speakerRequested || nestOut) && pl.directSpeaker.configured()) {
    // O som vai (ou vai) para a caixa: o passo é nela, a partir do que ela informou; sem status ainda, não mexe na TV por engano.
    int percent = pl.stepSpeaker(direction);
    if (percent >= 0) { shelf.volumeShown = percent; shelf.volumeSpeaker = true; shelf.volumeUntil = now + 2500; LOG("player: volume da caixa %d", percent); }
    return;
  }
  int percent = pl.stepVolume(direction);
  if (percent != settings.volume) { settings.volume = percent; volumeSaveAt = now + 1000; }
  shelf.volumeShown = percent; shelf.volumeSpeaker = false; shelf.volumeUntil = now + 2500;
  LOG("player: volume %d", percent);
}

// Abas da fileira (proposta A3): 0 A seguir, 1 Do canal, 2 Relacionados. Só aparecem as que têm (ou vão ter) conteúdo.
inline std::vector<Video> UiContext::shelfItems(int tab) {
  // Devolvida por valor: o desenho chama shelfTabShown() de outras abas no meio, e uma referência ao mesmo vetor viraria a lista errada.
  std::vector<Video> shelfScratch;
  if (tab == 0) {
    if (!upNext.id.empty()) shelfScratch.push_back(upNext);
    if (nav.localQueue) for (size_t i = nav.queueIndex + 1; i < nav.queue.size() && shelfScratch.size() < 24; ++i) shelfScratch.push_back(nav.queue[i].video());
  } else if (tab == 1) {
    for (const auto& it : shelfChannel.items) if (it.type == "video" && it.id != playing.id && shelfScratch.size() < 24) shelfScratch.push_back(it.video());
  } else {
    for (const auto& v : related) if (v.id != upNext.id && v.id != playing.id) shelfScratch.push_back(v);
  }
  return shelfScratch;
}

inline bool UiContext::shelfTabShown(int tab) {
  if (tab == 1) return !captions.channelId.empty() && shelfChannel.id == captions.channelId && (shelfChannel.loading || !shelfChannel.items.empty());
  return !shelfItems(tab).empty();
}

inline int UiContext::shelfFirstTab() {
  for (int t = 0; t < 3; ++t) if (shelfTabShown(t)) return t;
  return -1;
}

// "Do canal": a aba Vídeos do canal do vídeo, pelo mesmo caminho da página de canal; uma vez por vídeo.
inline void UiContext::shelfAskChannel() {
  const std::string& channel = captions.channelId;
  if (channel.empty() || shelfChannel.id == channel) return;
  shelfChannel = NavPage{}; shelfChannel.kind = "channel"; shelfChannel.id = channel; shelfChannel.tab = "videos"; shelfChannel.loading = true;
  shelfChannel.stamp = ++nav.serial;
  Job job{6, shelfChannel.path() + "&lang=" + i18n::code(settings.lang) + "&offset=0&fresh=0", shelfChannel.stamp};
  job.lang = settings.lang; navWorker.add(job, true);
}

inline void UiContext::playerShelfInput(uint64_t now) {
  const bool rtl = g.rtlText;
  int count = (int)shelfItems(shelf.tab).size();
  int horizontal = in.hit(Input::LEFT) ? -1 : in.hit(Input::RIGHT) ? 1 : 0;
  if (rtl) horizontal = -horizontal;  // espelhado: o que está à direita vem antes
  if (horizontal) shelf.sel = std::clamp(shelf.sel + horizontal, 0, std::max(0, count - 1));
  // LB/RB trocam de aba (contexto próprio da fileira)
  int tabStep = ctl::hit(in, ctl::Ctx::PlayerShelf, ctl::Act::PrevTab) ? -1 : ctl::hit(in, ctl::Ctx::PlayerShelf, ctl::Act::NextTab) ? 1 : 0;
  if (tabStep) for (int t = shelf.tab + tabStep; t >= 0 && t < 3; t += tabStep) if (shelfTabShown(t)) { shelf.tab = t; shelf.sel = 0; { auto l = shelfItems(t); LOG("player: fileira aba=%d itens=%zu primeiro=%s", t, l.size(), l.empty() ? "-" : l.front().id.c_str()); } break; }
  if (ctl::hit(in, ctl::Ctx::PlayerShelf, ctl::Act::Options)) { shelf.open = false; playerUI.options = true; }  // Menu = opções
  else if (ctl::hit(in, ctl::Ctx::PlayerShelf, ctl::Act::WatchLater)) {
    const auto& items = shelfItems(shelf.tab);
    if (shelf.sel < (int)items.size()) {
      const Video& v = items[(size_t)shelf.sel];
      if (account.signedIn()) nav.action(navWorker, "/api/nav/action?action=later&id=" + v.id + "&lang=" + i18n::code(settings.lang));
      else { bool had = localUI.library.contains(v.id); localUI.library.toggle(LocalLibraryUI::entry(v)); toast = tr(had ? K::PlayerLaterRemoved : K::PlayerLaterAdded); toastUntil = now + 2500; }
    }
  } else if (ctl::hit(in, ctl::Ctx::PlayerShelf, ctl::Act::Open)) {
    const auto& items = shelfItems(shelf.tab);
    if (shelf.sel >= 0 && shelf.sel < (int)items.size()) { Video v = items[(size_t)shelf.sel]; shelf.open = false; startVideo(v, 0, false); }
  } else if (ctl::hit(in, ctl::Ctx::PlayerShelf, ctl::Act::Back) || in.hit(Input::UP)) shelf.open = false;
  in.hit(Input::BACK); in.hit(Input::DOWN);
  playerUI.touch(now);
}

inline void UiContext::playerShelfDraw(uint64_t now) {
  (void)now;
  const bool rtl = g.rtlText;
  const auto& items = shelfItems(shelf.tab);
  const int count = (int)items.size(), cols = 5;
  const float gap = 18, left = 56, cw = std::floor((ui::W - left * 2 - gap * (cols - 1)) / cols), ch = std::floor(cw * 9 / 16);
  gfx::vgrad(g, 0, 180, ui::W, 540, {0, 0, 0, 0}, {0.043f, 0.043f, 0.055f, 0.95f});
  // abas
  const K names[3] = {K::PlayerShelfNext, K::PlayerShelfChannel, K::PlayerShelfRelated};
  float tx = rtl ? ui::W - left : left;
  for (int t = 0; t < 3; ++t) {
    if (!shelfTabShown(t)) continue;
    std::string name = tr(names[t]);
    float w = gfx::textWidth(g, ui::F_H2, name), x = rtl ? tx - w : tx;
    bool on = t == shelf.tab;
    gfx::text(g, ui::F_H2, name, x, 232, on ? ui::TEXT : ui::MUTED);
    if (on) gfx::rect(g, x, 232 + g.fonts[ui::F_H2].lineH + 4, w, 3, ui::A1, 1);
    tx += (rtl ? -1 : 1) * (w + 34);
  }
  if (!count) {
    gfx::text(g, ui::F_BODY, tr(K::Loading), rtl ? ui::W - left : left, 300, ui::MUTED, rtl ? 2 : 0);
  }
  {  // o que a fileira DESENHA (a lista da aba ativa): uma linha por mudança, para o teste e para o log do console
    std::string key = std::to_string(shelf.tab) + ":" + (items.empty() ? std::string("-") : items.front().id) + ":" + std::to_string(count);
    if (key != shelf.drawnKey) { shelf.drawnKey = key; LOG("player: fileira desenhou aba=%d itens=%d primeiro=%s", shelf.tab, count, items.empty() ? "-" : items.front().id.c_str()); }
  }
  int first = std::clamp(shelf.sel - cols + 1, 0, std::max(0, count - cols));
  for (int i = first; i < std::min(count, first + cols); ++i) {
    float slot = (float)(i - first) * (cw + gap), x = rtl ? ui::W - left - cw - slot : left + slot;
    homeCard(g, thumbs, thumbsWorker, frame, settings.lang, items[(size_t)i], pl.direct, items[(size_t)i].shortForm, i == shelf.sel, x, 286, cw, ch, i == shelf.sel ? 1.f : 0.f, nullptr);
  }
  float hy = 574, hx = rtl ? ui::W - left - 640 : left;
  hx = hint(g, hx, hy, tr(K::BtnHorizontal), XG, tr(K::ChooseRow));
  hx = ctlHint(g, hx, hy, ctl::Ctx::PlayerShelf, ctl::Act::Open, settings.lang, K::WatchNow);
  hx = ctlHint(g, hx, hy, ctl::Ctx::PlayerShelf, ctl::Act::WatchLater, settings.lang);
  hx = ctlHint(g, hx, hy, ctl::Ctx::PlayerShelf, ctl::Act::PrevTab, settings.lang);
  ctlHint(g, hx, hy, ctl::Ctx::PlayerShelf, ctl::Act::Back, settings.lang);
}

inline void UiContext::playerUpdate(uint64_t now) {
  if (shelf.video != playing.id || screen != PLAYER) { shelf.video = playing.id; shelf.open = false; shelf.sel = 0; shelf.shortsFocus = 0; shelf.tab = 0; shelf.shortOpenedAt = SDL_GetTicks(); if (shelfChannel.id != captions.channelId) shelfChannel = NavPage{}; }
  if(pl.qualityModesReady.exchange(false)) {
      qualityKnown=true;
      offeredQualities.assign(pl.qualityAvailable.load());
      if(playerUI.panel==PlayerUI::QUALITY)playerUI.panelSel=offeredQualities.index(settings.videoMode);
  }
  if (screen == PLAYER) {
    if(pl.manualQualityWarning.exchange(false)){manualNoticeUntil=now+10000;manualNoticeLines.clear();}
    if (pl.speakerFallback.exchange(false)) {
      toast = tr(nav.musicNow ? K::MusicSpeakerFallback : K::SpeakerFallback); toastUntil = now + 5000;
    }
    bool newTracks=pl.tracksReady.exchange(false);
    if (newTracks) { audioTracks=pl.audioTracks; selectedAudio=pl.selectedAudio; playerUI.tracks=audioTracks.size()>1; }
    if (newTracks || trackLang!=settings.lang) {
      trackLabels.clear(); selectedTrackLabel.clear(); trackLang=settings.lang;
      for (const auto& track : audioTracks) { trackLabels.push_back(audioTrackLabel(track,settings.lang)); if (track.code==selectedAudio) selectedTrackLabel=trackLabels.back(); }
    }
    if (pl.direct && pl.mediaReady.exchange(false)) {
      if (pl.title.empty()) pl.title = pl.media.title;
      pl.duration = pl.media.duration;
      if (!pl.playingAudio && (captionVideo != playing.id || captionInterface != i18n::code(settings.lang) || !captionLoader.native)) {
        captionVideo = playing.id; captionInterface = i18n::code(settings.lang);
        captions = {}; captionIndex = -1; captionLines.clear();
        captionLoader.request(srv, playing.id, i18n::code(settings.lang), settings.captionLang, &pl.media);
      }
    } else if (!pl.playingAudio && !pl.direct && (captionVideo != playing.id || captionInterface != i18n::code(settings.lang) || captionLoader.native)) {
      captionVideo = playing.id; captionInterface = i18n::code(settings.lang);
      captions = {}; captionIndex = -1; captionLines.clear();
      captionLoader.request(srv, playing.id, i18n::code(settings.lang), settings.captionLang);
    }
    if (captionLoader.pop(captions)) {
      captionIndex = -1; captionLines.clear();
      LOG("legendas: %s %s, %u frases, %u idiomas", captionVideo.c_str(), captions.selected.c_str(), (unsigned)captions.cues.size(), (unsigned)captions.tracks.size());
    }

  }
}

inline void UiContext::playerDraw(uint64_t now, double t, int optionCount, bool behindLive, const display::CommentsPanel& commentPanel,
                                  bool sleepWarning, double sleepLeft) {
  // Short: tela vertical própria; o painel de comentários (ou outro) desenha por cima dela, sem trocar para o player horizontal.
  const bool shortScene = playing.shortForm && !(ratePanel || sleepPanel || sleepWarning || shutdownAt || nextAt);
  if (shortScene) { shortsDraw(now, t); if (playerUI.panel == PlayerUI::NONE) return; }
  else {
    gfx::rect(g, 0, 0, ui::W, ui::H, rgb(0x000000), 0);
    float scale=g.scale, scaleX=g.scaleX, insetX=g.insetX, insetY=g.insetY;
    g.scaleX=g.scale=1; g.insetX=g.insetY=0;
    gfx::rect(g,0,0,g.extent.width,g.extent.height,rgb(0x000000));
    gfx::videoQuad(g,0,0,g.extent.width,g.extent.height,1,settings.videoFill);
    g.scale=scale; g.scaleX=scaleX; g.insetX=insetX; g.insetY=insetY;
  }
  bool panelOpen = ratePanel || sleepPanel || playerUI.panel != PlayerUI::NONE || sleepWarning || shutdownAt || nextAt || shelf.open;
  bool show = !shortScene && (playerUI.visible(now) || panelOpen);
  if (show) {
    const Color red = rgb(0xff0033), island = {0,0,0,0.63f};
    gfx::vgrad(g, 0, 420, ui::W, 300, {0,0,0,0}, {0,0,0,0.87f});
    bool options = playerUI.options && !panelOpen;
    bool transportFocus = options && playerUI.selected == PlayerUI::TRANSPORT;
    const std::string& title = !captions.title.empty() ? captions.title : pl.title.empty() ? playing.title : pl.title;
    const std::string& channel = captions.channel.empty() ? playing.channel : captions.channel;
    if (metaChannel != channel || metaDate != captions.date || metaLang != settings.lang) {
      metaChannel = channel; metaDate = captions.date; metaLang = settings.lang;
      std::string meta = channel;
      if (!metaDate.empty()) {
        if (!meta.empty()) meta += " · ";
        meta += i18n::shortDate(metaDate, metaLang);
      }
      overlayMeta.update(g, ui::F_META, meta.c_str(), ui::W-80);
    }
    overlayTitle.update(g, ui::F_VIDEO_TITLE, title.c_str(), options && !transportFocus ? ui::W-520 : ui::W-80);
    if(!panelOpen && pl.state!=player::FAILED) {
      float hy = ui::large ? 508 : 523, hx = g.rtlText ? ui::W - 40 - 640 : 40;
      hx = ctlHint(g, hx, hy, ctl::Ctx::Player, ctl::Act::WatchLater, settings.lang);
      hx = ctlHint(g, hx, hy, ctl::Ctx::Player, ctl::Act::Quality, settings.lang);
      if (!playerFromRemote && !phonePlaybackSerial) hx = ctlHint(g, hx, hy, ctl::Ctx::Player, ctl::Act::PrevItem, settings.lang);
      ctlHint(g, hx, hy, ctl::Ctx::Player, ctl::Act::Options, settings.lang);
    }
    if (!shelf.open) {
      gfx::textBox(g, ui::F_VIDEO_TITLE, overlayTitle.text, 40, ui::large ? 548 : 563, ui::W-80, ui::TEXT);
      gfx::textBox(g, ui::F_META, overlayMeta.text, 40, 591, ui::W-80, rgb(0xd4d4d8));
    }

    const bool live = pl.live;
    double pos = seekHold.active?seekHold.target:pl.position(), dur = pl.duration;
    const float bx = 40, by = 624, bw = ui::W-80;
    float frac = live ? 1.f : dur > 0 ? (float)std::clamp(pos / dur, 0.0, 1.0) : 0;
    gfx::rect(g, bx, by, bw, 4, {1,1,1,0.25f}, 2);
    if (frac > 0) gfx::rect(g, bx, by, bw * frac, 4, red, 2);
    if (!live) sponsorUI.timeline(g,sponsors,settings.sponsorBlock,dur,bx,by,bw);
    const cc::Chapter* currentChapter = nullptr;
    if (!live && dur > 0) {
      for (const auto& chapter : captions.chapters) {
        float x = bx + bw * (float)(chapter.start / dur);
        if (x > bx && x < bx + bw) gfx::rect(g, x - 1, by, 2, 4, island);
        if (chapter.start <= pos) currentChapter = &chapter;
      }
    }
    gfx::rect(g, bx + bw * frac - 7, by - 5, 14, 14, red, 7);

    if (!live && (clockSecond != (int)pos || clockDuration != (int)dur)) {
      clockSecond = (int)pos; clockDuration = (int)dur;
      clockLabel = fmtTime(pos);
      if (dur > 0) clockLabel += " / " + fmtTime(dur);
      clockWidth = gfx::textWidth(g, ui::F_CLOCK, clockLabel);
    }
    const char* liveLabel = tr(K::Live);
    float leftW = 78 + (live ? 14 + gfx::textWidth(g, ui::F_CLOCK, liveLabel) : clockWidth);
    gfx::playerIsland(g, 32, leftW);
    playerIcon(g, pl.paused ? icons::PLAY : icons::PAUSE, 60, transportFocus);
    if (live) {
      Color status = behindLive ? ui::MUTED : red;
      gfx::rect(g, 92, 662, 8, 8, status, 4);
      gfx::text(g, ui::F_CLOCK, liveLabel, 106, 657, status);
    } else gfx::text(g, ui::F_CLOCK, clockLabel, 92, 657, ui::TEXT);
    if (currentChapter && !options && !panelOpen) {
      overlayChapter.update(g, ui::F_META, currentChapter->title.c_str(), 430);
      gfx::text(g, ui::F_META, overlayChapter.text, 46 + leftW, 658, ui::TEXT);
    }

    if(seekHold.active) {
      if(seekSecond!=int(seekHold.target)){seekSecond=int(seekHold.target);seekLabel=std::string(tr(K::SeekTarget))+fmtTime(seekHold.target);}
      gfx::text(g,ui::F_H2,seekLabel,ui::W/2,490,ui::TEXT,1);
    }
    constexpr icons::PlayerIcon symbols[] = {icons::CLOSED_CAPTIONING, icons::GEAR, icons::SPEAKER_HIGH, icons::TRANSLATE_PLAYER,
                                             icons::MOON, icons::THUMBS_UP, icons::THUMBS_DOWN, icons::CHAT_CIRCLE_TEXT, icons::GAUGE, icons::REPEAT, icons::USER_CIRCLE, icons::BROADCAST};
    float rightW = 8 + (optionCount - 1 - (playerUI.tracks ? 0 : 1)) * 48, rightX = ui::W-32 - rightW;
    gfx::playerIsland(g, rightX, rightW);
    float focusX = 60;
    int slot = 0;
    for (int i = PlayerUI::CC; i < optionCount; i++) {
      if (i == PlayerUI::TRACK && !playerUI.tracks) continue;
      float cx = rightX + 28 + slot++ * 48;
      bool focused = options && playerUI.selected == i;
      // Avaliação já dada fica com o joinha cheio, como no YouTube.
      bool marked = (i == PlayerUI::RATE && likeState == "LIKE") || (i == PlayerUI::DISLIKE && likeState == "DISLIKE") || (i == PlayerUI::LOOP && pl.loop) || (i == PlayerUI::SPEED && pl.speed!=1);
      playerIcon(g, symbols[i - PlayerUI::CC], cx, focused, marked);
      if (focused) focusX = cx;
      if (i == PlayerUI::VIDEO) {
        int height=playback::labelHeight(videoTex.w,pl.actualHeight.load());
        bool full = height > 720;
        float badgeWidth = full ? 29 : 23;
        gfx::rect(g, cx - 3, 648, badgeWidth, 11, red, 3);
        const char* badge=height>=1080?"1080":height>=720?"720":height>=480?"480":height>=360?"360":height>=240?"240":height>0?"144":"?";
        gfx::text(g, ui::F_BADGE,badge, cx - 3 + badgeWidth / 2, 648, rgb(0xffffff), 1);
      } else if (i == PlayerUI::AUDIO && pl.nest) {
        gfx::rect(g, cx + 6, 652, 8, 8, rgb(0x22c55e), 4);
      }
    }
    if (options) {
      const K labels[] = {pl.paused ? K::Resume : K::Pause, K::Captions, K::PlayerQuality, K::Sound,
                          K::AudioTrack, K::SleepTimer, K::Like, K::Dislike, K::Comments, K::PlaybackSpeed, K::RepeatVideo, K::Channel, K::ChannelLive};
      const char* value = "";
      std::string timerValue;
      switch (playerUI.selected) {
        case PlayerUI::CC:
          value = tr(captionLoader.loading ? K::Loading : captions.failed ? K::CaptionsFailed :
                     captions.reason == "live_clock" ? K::CaptionsLiveClock : captions.tracks.empty() ?
                     (live ? K::CaptionsLiveNone : K::CaptionsNone) : settings.captions ? K::CaptionsOn : K::CaptionsOff);
          break;
        case PlayerUI::VIDEO: {
          int resolution = playback::labelHeight(videoTex.w,pl.actualHeight.load()), sourceFps = pl.actualFps.load();
          int targetFps = std::min(sourceFps, pl.presentFps.load());
          const char* mode = pl.waiting() ? nullptr : tr(pl.direct ? K::Console : pl.original ? K::ServerOriginal : K::ServerConverted);
          if (resolution != lastQuality || sourceFps != lastSourceFps || targetFps != lastTargetFps || lastOutput != (int)g.extent.height || mode != lastMode) {
            resolutionLabel = resolution ? std::to_string(resolution) + "p" : tr(K::Loading);
            if (sourceFps) resolutionLabel += std::to_string(sourceFps);
            if (sourceFps > targetFps) resolutionLabel += " -> " + std::to_string(targetFps) + " fps";
            qualityLabel = resolutionLabel + " · " + std::to_string(g.extent.height) + "p";
            if (mode) { qualityLabel += " · "; qualityLabel += mode; }
            lastQuality = resolution; lastSourceFps = sourceFps; lastTargetFps = targetFps;
            lastOutput = (int)g.extent.height; lastMode = mode;
          }
          value = qualityLabel.c_str(); break;
        }
        case PlayerUI::AUDIO: value = pl.nest ? nestName.c_str() : tr(K::TV); break;
        case PlayerUI::TRACK: value = selectedTrackLabel.c_str(); break;
        case PlayerUI::TIMER:
          if (sleep.active && !sleep.end) { timerValue = fmtTime(std::ceil(sleepLeft)); value = timerValue.c_str(); }
          else value = tr(sleep.active ? K::SleepEnd : K::SleepOff);
          break;
        case PlayerUI::RATE:
          if (extras.votes.available) value=extras.likesLabel.c_str();
          else if (likeState == "LIKE") value=tr(K::Liked);
          break;
        case PlayerUI::DISLIKE:
          if (extras.votes.available) value=extras.dislikesLabel.c_str();
          else if (likeState == "DISLIKE") value=tr(K::Disliked);
          break;
        case PlayerUI::COMMENTS: break;
        case PlayerUI::SPEED: value=tr(K::SpeedLive); if (!live) value=audio::speedLabels[audio::speedIndex(pl.speed)]; break;
        case PlayerUI::LOOP: value=tr(live ? K::SpeedLive : pl.loop ? K::RepeatOn : K::RepeatOff); break;
        case PlayerUI::CHANNEL: value = channel.c_str(); break;
        case PlayerUI::EDGE: value = tr(K::GoLive); break;
      }
      tipLabel.update(g, ui::F_CARD, tr(labels[playerUI.selected]), 310);
      tipValue.update(g, ui::F_DETAIL, value, 360);
      playerTip(g, focusX, tipLabel, tipValue, transportFocus);
      if (playerUI.selected==PlayerUI::DISLIKE && extras.votes.available)
        gfx::text(g,ui::F_DETAIL,tr(K::DislikeEstimate),40,533,ui::MUTED);
    }
  }
  if (shelf.open) playerShelfDraw(now);
  volumeHud(now);
  if (!panelOpen && !pl.live && pl.state==player::PLAYING)
    sponsorUI.hud(g,sponsors,settings.sponsorBlock,pl.position(),pl.duration,now,settings.lang);
  if (settings.captions && !captions.failed && !panelOpen && videoTex.ready) {
    int index = captions.at(pl.position());
    if (index != captionIndex) {
      captionIndex = index; captionLines.clear(); captionWidth = 0;
      if (index >= 0) {
        captionLines = gfx::wrap(g, ui::F_H2, captions.cues[(size_t)index].text, ui::W-120, 2);
        for (size_t i = 0; i < captionLines.size(); i++) {
          captionLineWidths[i] = gfx::textWidth(g, ui::F_H2, captionLines[i]);
          captionWidth = std::max(captionWidth, captionLineWidths[i]);
        }
      }
    }
    if (!captionLines.empty()) {
      float lineH=g.fonts[ui::F_H2].lineH+4;
      float height = (float)captionLines.size() * lineH + 16, bottom = show ? 482 : 674;
      gfx::rect(g, (ui::W - captionWidth) / 2 - 18, bottom - height, captionWidth + 36, height, {0, 0, 0, 0.78f}, 8);
      float cy = bottom - height + 8;
      for (size_t i = 0; i < captionLines.size(); i++) {
        gfx::text(g, ui::F_H2, captionLines[i], (ui::W - captionLineWidths[i]) / 2, cy, ui::TEXT); cy += lineH;
      }
    }
  }
  if (playerUI.panel==PlayerUI::COMMENTS_PANEL) {
    float x=commentPanel.x,y=commentPanel.y,w=commentPanel.w,h=commentPanel.h;
    gfx::rect(g,0,0,ui::W,ui::H,{0,0,0,.35f});
    gfx::shadow(g,x,y+10,w,h,24,32,.6f); gfx::rect(g,x,y,w,h,ui::SURFACE,24);
    gfx::text(g,ui::F_BIG,tr(K::Comments),x+36,y+26,ui::TEXT);
    int count=(int)extras.page.items.size();
    if (extras.loading || !extras.page.error.empty() || (!count && extras.page.continuation.empty())) {
      const char* message=tr(extras.loading ? K::Loading : !extras.page.error.empty() ? K::CommentsFailed : K::CommentsEmpty);
      static const char* previous=nullptr; static float previousWidth=0, previousLineH=0;
      static std::vector<std::string> lines;
      if (message!=previous || previousWidth!=w || previousLineH!=commentPanel.lineH) {
        lines=gfx::wrap(g,ui::F_BODY,message,w-72,3); previous=message; previousWidth=w; previousLineH=commentPanel.lineH;
      }
      for (size_t i=0;i<lines.size();++i) gfx::textBox(g,ui::F_BODY,lines[i],x+36,y+120+i*commentPanel.lineH,w-72,ui::MUTED);
    } else if (extras.selected<count) {
      const auto& comment=extras.page.items[extras.selected];
      if (commentIndex!=extras.selected) {
        commentIndex=extras.selected; commentLines=gfx::wrap(g,ui::F_BODY,comment.text,w-90,500);
        commentAuthor.update(g,ui::F_H2,comment.author.c_str(),w-100);
        commentMeta.update(g,ui::F_SMALL,(comment.time+"   ·   "+comment.likes+" "+tr(K::Like)).c_str(),w-100);
        commentPosition=std::to_string(extras.selected+1)+" / "+std::to_string(count);
      }
      gfx::textBox(g,ui::F_H2,commentAuthor.text,x+36,y+86,w-100,ui::TEXT);
      gfx::textBox(g,ui::F_SMALL,commentMeta.text,x+36,y+commentPanel.metaY,w-100,ui::MUTED);
      for (int i=extras.line;i<std::min((int)commentLines.size(),extras.line+commentPanel.lines);++i)
        gfx::textBox(g,ui::F_BODY,commentLines[i],x+36,y+commentPanel.bodyY+(i-extras.line)*commentPanel.lineH,w-90,ui::TEXT);
      gfx::text(g,ui::F_SMALL,commentPosition,x+w-36,y+36,ui::MUTED,2);
    } else gfx::text(g,ui::F_H2,tr(K::CommentsMore),x+36,y+130,ui::TEXT);
    float hx=hint(g,x+36,y+h-44,tr(K::BtnVertical),XG,tr(K::Scroll));
    if (!extras.page.error.empty() || extras.selected>=count) hx=hint(g,hx,y+h-44,"A",XA,tr(K::Open));
    hint(g,hx,y+h-44,"B",XB,tr(K::Back));
  } else if (playerUI.panel==PlayerUI::SPEED_PANEL) {
    float w=ui::large ? std::min(760.f,ui::W-80) : 580, x=(ui::W-w)/2, y=155, h=370, step=(w-72)/4;
    gfx::rect(g,0,0,ui::W,ui::H,{0,0,0,.55f}); gfx::shadow(g,x,y+10,w,h,24,32,.6f);
    gfx::rect(g,x,y,w,h,ui::SURFACE,24); gfx::text(g,ui::F_BIG,tr(K::PlaybackSpeed),x+36,y+26,ui::TEXT);
    for (int i=0;i<8;++i) {
      float bx=x+36+(i%4)*step,by=y+105+(i/4)*66; bool focus=playerUI.panelSel==i;
      gfx::rect(g,bx,by,step-10,54,focus ? ui::TEXT : ui::SURFACE2,10);
      gfx::text(g,ui::F_H2,audio::speedLabels[i],bx+(step-10)/2,by+(54-g.fonts[ui::F_H2].size)/2,focus ? ui::INK : ui::TEXT,1);
    }
    if (nestOut) gfx::text(g,ui::F_SMALL,tr(K::SpeedTV),x+36,y+254,ui::MUTED);
    float hx=hint(g,x+36,y+h-44,tr(K::BtnHorizontal),XG,tr(K::ChooseRow));
    hx=hint(g,hx,y+h-44,"A",XA,tr(K::SleepApply)); hint(g,hx,y+h-44,"B",XB,tr(K::Back));
  } else if (playerUI.panel == PlayerUI::SOUND) {
    soundPanelDraw(SoundPanel{true, playerUI.panelSel}, now);
  } else if (playerUI.panel != PlayerUI::NONE) {
    bool list=playerUI.panel==PlayerUI::TRACKS || playerUI.panel==PlayerUI::QUALITY;
    float w = 720, h = playerUI.panel==PlayerUI::QUALITY?490:list?430:330, x = (ui::W - w) / 2, y = playerUI.panel==PlayerUI::QUALITY?100:list?140:190;
    gfx::rect(g, 0, 0, ui::W, ui::H, {0, 0, 0, 0.55f});
    gfx::shadow(g, x, y + 10, w, h, 24, 32, 0.6f);
    gfx::rect(g, x, y, w, h, ui::SURFACE, 24);
    bool ccPanel = playerUI.panel == PlayerUI::CAPTIONS;
    gfx::text(g, ui::F_BIG, tr(ccPanel ? K::Captions : playerUI.panel == PlayerUI::QUALITY ? K::PlayerQuality : playerUI.panel==PlayerUI::TRACKS ? K::AudioTrack : K::Sound), x + 36, y + 28, ui::TEXT);
    if (playerUI.panel==PlayerUI::QUALITY && pl.actualHeight>0) {
      int source=playback::labelHeight(videoTex.w,pl.actualHeight.load());
      if (source!=lastQuality || pl.actualFps!=lastSourceFps) {
        resolutionLabel=std::to_string(source)+"p"+(pl.actualFps>0 ? std::to_string(pl.actualFps.load()) : "");
        lastQuality=source;lastSourceFps=pl.actualFps.load();lastOutput=-1;
      }
      gfx::text(g,ui::F_H2,resolutionLabel,x+w-36,y+35,ui::A2,2);
    }
    if (ccPanel && (captionLoader.loading || captions.failed || captions.tracks.empty())) {
      gfx::text(g, ui::F_BODY, tr(captionLoader.loading ? K::CaptionsLoading : captions.failed ? K::CaptionsFailed : captions.reason == "live_clock" ? K::CaptionsLiveClock : pl.live ? K::CaptionsLiveNone : K::CaptionsNone), x + 36, y + 110, ui::MUTED);
    } else {
      int count = ccPanel ? 2 : playerUI.panel == PlayerUI::QUALITY ? offeredQualities.count : playerUI.panel==PlayerUI::TRACKS ? (int)audioTracks.size() : 2;
      int first=list ? std::max(0,playerUI.panelSel-3) : 0;
      for (int i = first; i < std::min(count,first+4); i++) {
        float ry = y + 90 + (i-first) * 58;
        bool focused = playerUI.panelSel == i;
        bool disabled = playerUI.panel == PlayerUI::SOUND && i == 1 && !nestAvailable;
        gfx::rect(g, x + 30, ry, w - 60, 50, focused ? ui::TEXT : ui::SURFACE2, 10);
        std::string label;
        if (ccPanel && !i) label = tr(settings.captions ? K::CaptionsOn : K::CaptionsOff);
        else if (ccPanel) {
          label = tr(K::CaptionsLanguage);
          for (const auto& track : captions.tracks) if (track.code == captions.selected)
            label = track.name + " · " + tr(track.automatic ? K::CaptionsAuto : K::CaptionsManual);
        } else if (playerUI.panel == PlayerUI::QUALITY) label = tr(videoModeLabel(offeredQualities.modes[i]));
        else if (playerUI.panel==PlayerUI::TRACKS) label=trackLabels[i];
        else label = i ? nestName : tr(K::TV);
        gfx::text(g, ui::F_BODY, gfx::ellipsize(g, ui::F_BODY, label, w - 132), x + 50, ry + (playerUI.panel==PlayerUI::QUALITY?(50-g.fonts[ui::F_BODY].size)/2:14), disabled ? ui::FAINT : focused ? ui::INK : ui::TEXT);
      }
    }
    if(playerUI.panel==PlayerUI::QUALITY) {
      float hy=y+330;
      for(const auto& line:qualityHelpLines){gfx::textBox(g,ui::F_SMALL,line,x+36,hy,w-72,ui::MUTED);hy+=g.fonts[ui::F_SMALL].lineH+2;}
    }
    float hx = hint(g, x + 36, y + h - 42, tr(K::BtnVertical), XG, tr(K::ChooseRow));
    hx = hint(g, hx, y + h - 42, "A", XA, tr(K::SleepApply));
    hint(g, hx, y + h - 42, "B", XB, tr(K::Back));
  }
  if (ratePanel) {
    // dois botões redondos com o joinha, como no YouTube: aceso (cheio, na cor de destaque) o que está marcado
    float w = 560, h = 252, x = (ui::W - w) / 2, y = ui::H - 456;
    gfx::shadow(g, x, y + 10, w, h, 24, 32, 0.6f);
    gfx::rect(g, x, y, w, h, ui::SURFACE, 24);
    gfx::text(g, ui::F_H2, tr(K::RateTitle), x + 32, y + 26, ui::TEXT);
    const float d = 96, gap = 56;
    for (int i = 0; i < 2; i++) {
      float bx = x + w / 2 + (i == 0 ? -gap / 2 - d : gap / 2), bt = y + 78;
      bool sel = rateSel == i, on = likeState == (i == 0 ? "LIKE" : "DISLIKE");
      if (sel) gfx::rect(g, bx - 4, bt - 4, d + 8, d + 8, ui::alpha(rgb(0xffffff), 0.9f), d / 2 + 4);
      gfx::rect(g, bx, bt, d, d, on ? ui::A1 : sel ? ui::SURFACE2 : ui::alpha(rgb(0xffffff), 0.08f), d / 2);
      int which = i == 0 ? (on ? icons::THUMB_UP_FILL : icons::THUMB_UP) : (on ? icons::THUMB_DOWN_FILL : icons::THUMB_DOWN);
      glyph(g, which, true, bx + (d - 48) / 2, bt + (d - 48) / 2, on || sel ? rgb(0xffffff) : ui::MUTED);
    }
    float hx = hint(g, x + 32, y + 206, tr(K::BtnHorizontal), XG, tr(K::ChangeValue));
    hx = hint(g, hx, y + 206, "A", XA, tr(K::SleepApply));
    hint(g, hx, y + 206, "B", XB, tr(K::Close));
  }
  if (nextAt) {
    gfx::rect(g, 0, 0, ui::W, ui::H, {0, 0, 0, 0.6f});
    float w = 800, h = 300, x = (ui::W - w) / 2, y = (ui::H - h) / 2;
    gfx::shadow(g, x, y + 12, w, h, 24, 32, 0.6f);
    gfx::rect(g, x, y, w, h, ui::SURFACE, 24);
    double left = nextAt > now ? (nextAt - now) / 1000.0 : 0;
    gfx::text(g, ui::F_SMALL, tr(K::UpNextIn) + std::to_string((int)std::ceil(left)) + " s", x + 32, y + 26, ui::MUTED);
    gfx::rect(g, x + 32, y + 52, w - 64, 4, {1, 1, 1, 0.12f}, 2);
    gfx::hgrad(g, x + 32, y + 52, std::max(4.f, (w - 64) * (float)(1 - left / 8.0)), 4, ui::A1, ui::A2, 2);
    float tw = 256, th = 144, tx = x + 32, ty = y + 74;
    gfx::rect(g, tx, ty, tw, th, ui::SURFACE2, 12);
    if (const Thumbs::Slot* s = thumbs.get(thumbsWorker, upNext.id, frame, pl.direct)) gfx::image(g, s->tex, tx, ty, tw, th, 12);
    float cx = tx + tw + 28, cw = x + w - 32 - cx, cy = ty + 2;
    for (auto& line : gfx::wrap(g, ui::F_H2, upNext.title, cw, 2)) { gfx::textBox(g, ui::F_H2, line, cx, cy, cw, ui::TEXT); cy += 30; }
    if (!upNext.channel.empty()) gfx::text(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, upNext.channel, cw), cx, cy + 8, ui::MUTED);
    float hx = hint(g, x + 32, y + h - 52, "A", XA, tr(K::WatchNow));
    hint(g, hx, y + h - 52, "B", XB, tr(K::Cancel));
  }
  if (pl.waiting() && !panelOpen) {
    spinner(g, ui::W / 2, ui::H / 2, 26, t);
    gfx::text(g, ui::F_BODY, pl.state == player::CONNECTING ? tr(pl.direct ? K::Loading : K::RequestingVideo) : tr(K::Loading), ui::W / 2, ui::H / 2 + 46, ui::TEXT, 1);
  }
  if (pl.state == player::FAILED && !panelOpen) {
    float w=std::min(900.f,ui::W-100), y=ui::H/2-95;
    gfx::rect(g,(ui::W-w)/2,y,w,190,ui::SURFACE,22);
    gfx::text(g,ui::F_H2,tr(K::VideoFailed),ui::W/2,y+24,ui::TEXT,1);
    float lineY=y+76;
    for (const auto& line:gfx::wrap(g,ui::F_BODY,tr(i18n::playerError(pl.error)),w-64,3)) {
      gfx::text(g,ui::F_BODY,line,ui::W/2,lineY,ui::MUTED,1); lineY+=g.fonts[ui::F_BODY].lineH+4;
    }
    if(playback::manual(settings.videoMode))hint(g,(ui::W-w)/2+32,y+152,"Y",XY,tr(K::PlayerQuality));
  }
}

inline void UiContext::playerNoticeDraw(uint64_t now) {
  if(screen==PLAYER && now<manualNoticeUntil) {
    float w=std::min(880.f,ui::W-80),x=(ui::W-w)/2;
    if(manualNoticeLines.empty() || manualNoticeFont!=g.fontRevision || manualNoticeLang!=settings.lang || manualNoticeWidth!=w) {
      manualNoticeLines=gfx::wrap(g,ui::F_SMALL,std::to_string(playback::labelHeight(videoTex.w,pl.actualHeight.load()))+"p · "+tr(K::ManualQualityPressure),w-40,3);
      manualNoticeFont=g.fontRevision;manualNoticeLang=settings.lang;manualNoticeWidth=w;
    }
    float lineH=g.fonts[ui::F_SMALL].lineH+2;
    gfx::rect(g,x,24,w,24+lineH*manualNoticeLines.size(),ui::SURFACE2,16);
    float y=36;for(const auto& line:manualNoticeLines){gfx::textBox(g,ui::F_SMALL,line,x+20,y,w-40,ui::TEXT);y+=lineH;}
  }
}

inline void UiContext::ratePanelInput(uint64_t now) {
  if (in.hit(Input::LEFT) || in.hit(Input::RIGHT)) rateSel = 1 - rateSel;
  if (in.hit(Input::A)) {
    // apertar de novo o que já está marcado desfaz, como no app do YouTube
    bool same = likeState == (rateSel == 0 ? "LIKE" : "DISLIKE");
    control.add({5, "/api/video/" + playing.id + "/rate?r=" + (same ? "none" : rateSel == 0 ? "like" : "dislike"), 0});
    ratePanel = false;
  }
  if (in.hit(Input::B) || in.hit(Input::DOWN)) ratePanel = false;
  overlayUntil = now + 4000;
}
