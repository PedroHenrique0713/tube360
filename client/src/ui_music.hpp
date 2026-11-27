// Música: as páginas (início com prateleiras, busca, álbum, artista, rádio, playlist de música), a tela "Tocando agora"
// (capa, fila, letra e botões) e o que a Música acrescenta por cima do player. A entrada genérica das páginas está em
// ui_collection.hpp; aqui ficam os ganchos da Música.
#pragma once
#include "ui_context.hpp"

inline void Navigation::prepareNow(gfx::Gfx& g, Video& playing, i18n::Lang lang) {
  if (nowId == playing.id) return;
  lyricsOpen = false; lyrics.clear(); lyricsId.clear(); lyricLines.clear(); lyricScroll = 0;
  nowId = playing.id; size_t index = 0; const NavItem* current = nullptr;
  for (size_t i = 0; i < queue.size(); i++) if (queue[i].id == playing.id) { current = &queue[i]; index = i; break; }
  if (current) { playing.title = current->title; playing.channel = current->channel; }
  nowImage = current && !current->image.empty() ? current->image : playing.id;
  nowTitle = gfx::ellipsize(g, ui::F_BIG, playing.title, ui::W-(ui::narrow ? 404 : 530));
  nowArtist = gfx::ellipsize(g, ui::F_KEY, playing.channel, ui::W-(ui::narrow ? 404 : 530));
  nextTitles.clear();
  for (size_t i = index + 1; i < std::min(queue.size(), index + 5); i++) nextTitles.push_back(gfx::ellipsize(g, ui::F_KEY, queue[i].title, ui::W-(ui::narrow ? 448 : 574)));
  noLyrics = i18n::get(lang, K::NoLyrics); nowError.clear(); nowErrorKey = K::None;
}

inline void Navigation::drawNow(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, const Video& playing, double pos, double duration, bool paused, bool waiting, K error, uint64_t frame, i18n::Lang lang) {
  (void)playing;
  auto tr = [&](K k) { return i18n::get(lang, k); };
  float contentX=ui::narrow ? 340 : 466;
  gfx::hgrad(g, 0, 0, 620, 720, rgb(0x37151f), ui::BG);
  gfx::text(g, ui::F_H2, tr(K::NowPlaying), 64, 40, ui::MUTED);
  artwork(g, thumbs, worker, nowImage, frame, 64, 104, ui::narrow ? 230 : 350, ui::narrow ? 230 : 350, true);
  gfx::textBox(g, ui::F_BIG, nowTitle, contentX, 104, ui::W-contentX-64, ui::TEXT);
  gfx::textBox(g, ui::F_KEY, nowArtist, contentX, 158, ui::W-contentX-64, ui::MUTED);
  if (error != nowErrorKey) { nowErrorKey = error; nowError = error == K::None ? "" : gfx::ellipsize(g, ui::F_KEY, tr(error), 714); }
  if (error != K::None) gfx::text(g, ui::F_KEY, nowError, contentX, 210, ui::A2);
  else if (waiting) gfx::text(g, ui::F_KEY, tr(K::Loading), contentX, 210, ui::A2);
  else if (lyricsOpen) {
    float y = 226;
    if (lyricLines.empty()) gfx::text(g, ui::F_KEY, noLyrics, contentX, y, ui::MUTED);
    for (size_t i = lyricScroll; i < std::min(lyricLines.size(), (size_t)lyricScroll + 9); i++) { gfx::textBox(g, ui::F_KEY, lyricLines[i], contentX, y, ui::W-contentX-64, ui::TEXT); y += 30; }
  } else {
    gfx::text(g, ui::F_H2, tr(K::Queue), contentX, 222, ui::TEXT);
    for (size_t n = 0; n < nextTitles.size(); n++) {
      float y = 272 + n * 58;
      gfx::text(g, ui::F_CHAN, std::to_string(n + 1), contentX+4, y, ui::A2);
      gfx::textBox(g, ui::F_KEY, nextTitles[n], contentX+44, y, ui::W-contentX-108, ui::TEXT);
    }
  }
  float fraction = duration > 0 ? (float)std::clamp(pos / duration, 0.0, 1.0) : 0;
  gfx::rect(g, 64, 516, ui::W-128, 6, ui::SURFACE2, 3);
  if (fraction > 0) gfx::hgrad(g, 64, 516, std::max(6.f, fraction * (ui::W-128)), 6, ui::A1, ui::A2, 3);
  gfx::text(g, ui::F_CHAN, fmtTime(pos), 64, 538, ui::MUTED); gfx::text(g, ui::F_CHAN, fmtTime(duration), ui::W-64, 538, ui::MUTED, 2);
  const K buttons[] = {paused ? K::Resume : K::Pause,K::NextTrack,K::Lyrics,K::ShowVideo}; float x = 64;
  for (int i = 0; i < 4; i++) {
    float w = gfx::textWidth(g, ui::F_KEY, tr(buttons[i])) + 56;
    gfx::rect(g, x, 592, w, 52, nowSel == i ? ui::TEXT : ui::SURFACE2, 26);
    gfx::text(g, ui::F_KEY, tr(buttons[i]), x + w / 2, 607, nowSel == i ? ui::INK : ui::TEXT, 1); x += w + 18;
  }
  float hx = hint(g, 64, 677, tr(K::BtnHorizontal), XG, tr(K::ChooseRow));
  hx = hint(g, hx, 677, "A", XA, tr(K::Open)); hint(g, hx, 677, "B", XB, tr(K::Back));
}

inline void UiContext::musicNowInput(uint64_t now) {
  if (pl.state == player::FAILED && pl.nestFailed && !nestSkip) {
    nestSkip = true; toast = tr(K::MusicSpeakerFallback); toastUntil = now + 5000;
    startVideo(playing, pl.position(), playerFromRemote);
  }
  if (in.hit(Input::UP) && nav.lyricsOpen && nav.lyricScroll > 0) nav.lyricScroll--;
  if (in.hit(Input::DOWN) && nav.lyricsOpen && nav.lyricScroll + 9 < (int)nav.lyricLines.size()) nav.lyricScroll++;
  if (in.hit(Input::LEFT) && nav.nowSel > 0) nav.nowSel--;
  if (in.hit(Input::RIGHT) && nav.nowSel < 3) nav.nowSel++;
  if (in.hit(Input::A)) {
    if (nav.nowSel == 0 && (!pl.waiting() || pl.paused)) pl.setPaused(!pl.paused);
    else if (nav.nowSel == 1 && !nav.busy) {
      if (nav.localQueue) {
        if (nav.queueIndex+1 < nav.queue.size()) { ++nav.queueIndex; startVideo(nav.queue[nav.queueIndex].video(),0,false); }
        else stopVideo();
      } else nav.action(navWorker, "/api/nav/transport?action=next");
    }
    else if (nav.nowSel == 2) {
      nav.lyricsOpen = !nav.lyricsOpen;
      if (nav.lyricsOpen && nav.lyricsId != playing.id && !nav.busy) { nav.lyricsId = playing.id; nav.lyrics.clear(); nav.lyricLines.clear(); nav.action(navWorker, "/api/nav/lyrics?id=" + playing.id); }
    } else if (nav.nowSel == 3) {
      double pos = pl.position(); bool paused = pl.paused;
      nav.musicNow = false; startVideo(playing,pos,playerFromRemote); pl.setPaused(paused);
    }
  }
  if (in.hit(Input::B)) {
    if (nav.localQueue) stopVideo();
    else {
      nav.ownsQueue = nav.musicSession = nav.musicNow = false; nav.busy = false; nav.cancelQueue = true;
      nav.action(navWorker, "/api/nav/transport?action=stop"); stopVideo();
    }
  }
}

inline void UiContext::musicNowDraw() {
  nav.drawNow(g, thumbs, thumbsWorker, playing, pl.position(), pl.duration, pl.paused, pl.waiting(),
      pl.state == player::FAILED ? i18n::playerError(pl.error) : K::None, frame, settings.lang);
}

inline void UiContext::musicSleepDraw(double sleepLeft) {
  std::string value = sleep.end ? tr(K::SleepEnd) : fmtTime(std::ceil(sleepLeft));
  if (sleep.end && sleepLeft >= 0) value += " · " + fmtTime(std::ceil(sleepLeft));
  std::string label = std::string(tr(K::SleepTimer)) + " · " + value;
  float w = gfx::textWidth(g, ui::F_SMALL, label) + 30, x = ui::W - 64 - w;
  float y = screen == PLAYER ? 124 : 24;
  gfx::rect(g, x, y, w, 32, ui::SURFACE2, 16);
  gfx::text(g, ui::F_SMALL, label, x + 15, y + 7, ui::MUTED);
}

// Prateleiras do início da Música: esquerda/direita andam na prateleira, cima/baixo trocam de prateleira (cima da primeira sobe para as abas).
inline void UiContext::musicHomeMove(NavPage& p, bool left, bool right, bool up, bool down) {
  auto& shelf = p.shelves[p.shelf];
  if (left && p.sel > shelf.first) shelf.selected--;
  if (right && p.sel + 1 < shelf.end) shelf.selected++;
  if (up && p.shelf == 0) p.zone = 1;
  else if (up) p.shelf--;
  if (down && p.shelf + 1 < (int)p.shelves.size()) p.shelf++;
  p.sel = p.shelves[p.shelf].selected;
}

// Troca de aba da Música: Início, Paradas, Mixes (ou Tudo, Músicas, Álbuns, Artistas na busca); no início a aba vira a consulta.
inline void UiContext::musicTabSelected(NavPage& p) {
  p.tab = p.tabSel == 1 ? "live" : p.tabSel == 2 ? "playlists" : "videos";
  if (p.kind == "music") p.query = p.tabSel == 1 ? "charts" : p.tabSel == 2 ? "mixes" : "";
}

// Página de Música: fundo vermelho escuro, capa quadrada (redonda para artista), título grande, "Reproduzir tudo", abas e a
// grade quadrada. O início troca a grade por prateleiras e põe as abas ao lado do título.
inline float Navigation::drawMusicPage(NavDraw& d) {
  auto& g = d.g; auto& p = d.p;
  bool home = p.musicHome();
  bool channel = p.kind == "channel", playlist = playlistKind(p);
  if (!home) gfx::hgrad(g, 96, 0, 1184, 224, rgb(0x32161f), ui::BG);
  float tx = 136;
  if (!home && !p.image.empty()) { artwork(g, d.thumbs, d.worker, p.image, d.frame, tx, 46, 86, 86, true, p.kind == "channel" || p.kind == "artist"); tx += 110; }
  int titleFont = !home ? ui::F_HERO : ui::F_BIG;
  gfx::textBox(g, titleFont, p.displayTitle.empty() ? d.tr(titleKey(p)) : p.displayTitle, tx, !home ? 42 : 28, home ? (ui::narrow ? 150 : 480) : ui::W-tx-64, ui::TEXT);
  if (home && !p.musicLabel.empty()) gfx::textBox(g, ui::F_TINY, p.musicLabel, ui::narrow ? 300 : tx, ui::narrow ? 32 : 69, ui::W-(ui::narrow ? 300 : tx)-64, ui::MUTED);
  if (!home) gfx::text(g, ui::F_CHAN, p.displaySub, tx, 103, ui::MUTED);
  if (channel || playlist) {
    bool f = d.contentFocus && p.zone == 0;
    gfx::rect(g, ui::W-316, 64, 252, 54, f ? ui::TEXT : ui::SURFACE2, 27);
    gfx::text(g, ui::F_KEY, d.tr(channel ? p.subscribed ? K::Subscribed : K::Subscribe : K::PlayAll), ui::W-190, 80, f ? ui::INK : ui::TEXT, 1);
  }
  bool tabs = channel || p.kind == "music" || p.kind == "music_search";
  if (tabs) {
    const K channelTabs[] = {K::Videos,K::ChannelShorts,K::ChannelLive,K::Playlists};
    const K musicTabs[] = {K::Home,K::Charts,K::Mixes};
    const K songTabs[] = {K::AllResults,K::Songs,K::Albums,K::Artists};
    int count = channel || p.kind == "music_search" ? 4 : 3; float x = home && !ui::narrow ? 650 : 136;
    float tabY = home ? (ui::narrow ? 88 : 32) : 166;
    navTabs(d, channel ? channelTabs : p.kind == "music" ? musicTabs : songTabs, count, x, tabY);
  }
  float top = 246;
  auto m = navMetrics(d);
  if (home) {
    int shelfCount=ui::large ? 2 : 3, shelfColumns=ui::narrow ? 4 : ui::large ? 5 : 7;
    int firstShelf = std::max(0, p.shelf-shelfCount+1);
    for (int row = firstShelf; row < std::min(firstShelf+shelfCount, (int)p.shelves.size()); row++) {
      const auto& shelf = p.shelves[row];
      float y = (ui::narrow ? 150 : 99) + (row-firstShelf)*(ui::large ? 216 : 184);
      gfx::text(g, ui::F_KEY, shelf.title, 136, y, ui::TEXT);
      int first = std::max(shelf.first, shelf.selected-shelfColumns+1);
      for (int i = first; i < std::min(shelf.end, first+shelfColumns); i++) {
        const auto& item = p.items[i]; float x = 136 + (i - first) * 155;
        bool f = d.contentFocus && p.zone == 3 && i == p.sel;
        if (f) gfx::rect(g, x - 4, y + 26, 118, 118, ui::TEXT, 9);
        artwork(g, d.thumbs, d.worker, item.image, d.frame, x, y + 30, 110, 110, true);
        memberBadge(g, item.members, d.lang, x + 5, y + 35);
        gfx::textBox(g, ui::F_SMALL, item.titleLines[0], x, y + 143, 135, ui::TEXT);
        gfx::textBox(g, ui::F_TINY, item.byline, x, y + (ui::large ? 178 : 163), 135, ui::MUTED);
      }
    }
  } else navGrid(d, top, m);
  return top;
}
