// Música: as páginas (início com prateleiras, busca, álbum, artista, rádio, playlist de música), a tela "Tocando agora"
// (capa, fila, letra e botões) e o que a Música acrescenta por cima do player. A entrada genérica das páginas está em
// ui_collection.hpp; aqui ficam os ganchos da Música.
#pragma once
#include "ui_context.hpp"

// ---- Tocando agora -------------------------------------------------------------------------------------------------
// Esquerda: seletor Música/Vídeo, capa (ou o vídeo), título, barra de tempo e os botões. Direita: abas "A seguir" e
// "Letra" e a lista da fila. O foco anda entre cinco zonas (MusicNavigation::Zone).
struct NowLayout {
  float margin, leftW, rightX, rightW, coverY, titleY, artistY, barY, timeY, btnY, listY, rowH, hintY;
  int rows;
};
static NowLayout nowLayout() {
  NowLayout L{};
  L.margin = ui::narrow ? 36 : 48; L.leftW = ui::narrow ? 300 : 360;
  L.rightX = L.margin + L.leftW + (ui::narrow ? 32 : 56); L.rightW = ui::W - L.rightX - L.margin;
  L.coverY = 74; L.titleY = L.coverY + L.leftW + 12; L.artistY = L.titleY + 44;
  L.barY = L.artistY + (ui::large ? 40 : 34) + 12; L.timeY = L.barY + 12; L.btnY = L.timeY + (ui::large ? 36 : 28) + 28;
  L.hintY = 677; L.listY = 128; L.rowH = ui::large ? 72 : 62;
  L.rows = std::max(3, (int)((L.hintY - 12 - L.listY) / L.rowH));
  return L;
}

inline void MusicNavigation::prepareNow(gfx::Gfx& g, Video& playing, i18n::Lang lang) {
  auto L = nowLayout();
  bool layoutChanged = builtWidth != ui::W || builtLarge != ui::large;
  if (nowId == playing.id && !layoutChanged) return;
  if (nowId != playing.id) {
    lyricsOpen = false; lyrics.clear(); lyricsId.clear(); lyricLines.clear(); lyricScroll = 0; seekPending = false;
    if (localQueue) queueSel = (int)std::min(queueIndex, queue.size() ? queue.size() - 1 : 0);
  }
  builtWidth = ui::W; builtLarge = ui::large;
  nowId = playing.id; size_t index = 0; const NavItem* current = nullptr;
  for (size_t i = 0; i < queue.size(); i++) if (queue[i].id == playing.id) { current = &queue[i]; index = i; break; }
  (void)index;
  if (current) { playing.title = current->title; playing.channel = current->channel; }
  nowImage = current && !current->image.empty() ? current->image : playing.id;
  nowTitle = gfx::ellipsize(g, ui::F_BIG, playing.title, L.leftW);
  nowArtist = gfx::ellipsize(g, ui::F_CHAN, playing.channel, L.leftW);
  noLyrics = i18n::get(lang, K::NoLyrics); nowError.clear(); nowErrorKey = K::None;
  rowsFor = -1; ++queueRev;
}

static void strokeLine(gfx::Gfx& g, float x0, float y0, float x1, float y1, float w, Color c) {
  float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0) return;
  float nx = -dy / len * w / 2, ny = dx / len * w / 2;
  gfx::triangle(g, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
  gfx::triangle(g, x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}
// Ícones do atlas do Phosphor (peso regular ou cheio) centrados em (cx, cy).
static void atlasIcon(gfx::Gfx& g, icons::PlayerIcon id, float cx, float cy, bool fill, Color c) {
  int level = g.scale > 1 ? 1 : 0;
  const auto& sprite = icons::PLAYER[level][id];
  float size = sprite.size / std::max(1.f, g.scale), y = fill ? icons::FILL_Y[level] : icons::REGULAR_Y[level];
  gfx::imagePart(g, iconTex, cx - size / 2, cy - size / 2, size, size, float(sprite.x) / icons::W, y / icons::H,
      float(sprite.x + sprite.size) / icons::W, (y + sprite.size) / icons::H, c);
}
// Anterior, próxima e aleatório desenhados com formas (o atlas não tem esses três).
static void transportGlyph(gfx::Gfx& g, int which, float cx, float cy, Color c) {
  const float s = 8.5f;
  if (which == MusicNavigation::BtnPrev) {
    gfx::rect(g, cx - s, cy - s, 3, 2 * s, c, 1);
    gfx::triangle(g, cx + s, cy - s, cx + s, cy + s, cx - s + 5, cy, c);
  } else if (which == MusicNavigation::BtnNext) {
    gfx::rect(g, cx + s - 3, cy - s, 3, 2 * s, c, 1);
    gfx::triangle(g, cx - s, cy - s, cx - s, cy + s, cx + s - 5, cy, c);
  } else {  // aleatório: duas setas que se cruzam
    const float w = 2.2f, a = s * 0.62f;
    strokeLine(g, cx - s, cy - a, cx - 1, cy - a, w, c); strokeLine(g, cx - 1, cy - a, cx + s - 5, cy + a, w, c); strokeLine(g, cx + s - 5, cy + a, cx + s - 3, cy + a, w, c);
    strokeLine(g, cx - s, cy + a, cx - 1, cy + a, w, c); strokeLine(g, cx - 1, cy + a, cx + s - 5, cy - a, w, c); strokeLine(g, cx + s - 5, cy - a, cx + s - 3, cy - a, w, c);
    gfx::triangle(g, cx + s + 1, cy - a, cx + s - 5, cy - a - 4.5f, cx + s - 5, cy - a + 4.5f, c);
    gfx::triangle(g, cx + s + 1, cy + a, cx + s - 5, cy + a - 4.5f, cx + s - 5, cy + a + 4.5f, c);
  }
}

inline void MusicNavigation::drawNow(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, const Video& playing, double pos, double duration, bool paused, bool waiting, K error, uint64_t frame, i18n::Lang lang) {
  auto tr = [&](K k) { return i18n::get(lang, k); };
  const NowLayout L = nowLayout();
  auto focus = [&](int zone) { return nowZone == zone; };
  gfx::hgrad(g, 0, 0, L.rightX - 24, 720, rgb(0x37151f), ui::BG);

  // Seletor Música | Vídeo
  {
    float sw = ui::large ? 150 : 120, sh = ui::large ? 48 : 40, x = L.margin, y = 22;
    gfx::rect(g, x - (focus(ZoneMode) ? 4 : 0), y - (focus(ZoneMode) ? 4 : 0), 2 * sw + (focus(ZoneMode) ? 8 : 0), sh + (focus(ZoneMode) ? 8 : 0), focus(ZoneMode) ? ui::TEXT : ui::SURFACE2, sh / 2 + 4);
    gfx::rect(g, x, y, 2 * sw, sh, ui::SURFACE2, sh / 2);
    gfx::rect(g, x + (videoMode ? sw : 0) + 3, y + 3, sw - 6, sh - 6, ui::TEXT, sh / 2 - 3);
    gfx::text(g, ui::F_KEY, tr(K::MusicSongTab), x + sw / 2, y + (sh - g.fonts[ui::F_KEY].size) / 2 - 2, videoMode ? ui::TEXT : ui::INK, 1);
    gfx::text(g, ui::F_KEY, tr(K::MusicVideoTab), x + sw * 1.5f, y + (sh - g.fonts[ui::F_KEY].size) / 2 - 2, videoMode ? ui::INK : ui::TEXT, 1);
  }

  // Capa ou vídeo
  float cx = L.margin, cy = L.coverY, cs = L.leftW;
  if (videoMode) {
    float vh = cs * 9 / 16;
    gfx::rect(g, cx, cy, cs, cs, ui::SURFACE, 14);
    gfx::videoQuad(g, cx, cy + (cs - vh) / 2, cs, vh);
    if (waiting) gfx::text(g, ui::F_KEY, tr(K::MusicVideoLoading), cx + cs / 2, cy + cs / 2 - 10, ui::MUTED, 1);
  } else {
    gfx::shadow(g, cx, cy + 10, cs, cs, 14, 32, 0.55f);
    artwork(g, thumbs, worker, nowImage, frame, cx, cy, cs, cs, true);
  }
  gfx::text(g, ui::F_BIG, nowTitle, cx, L.titleY, ui::TEXT);
  if (error != nowErrorKey) { nowErrorKey = error; nowError = error == K::None ? "" : gfx::ellipsize(g, ui::F_CHAN, tr(error), L.leftW); }
  gfx::text(g, ui::F_CHAN, error != K::None ? nowError : nowArtist, cx, L.artistY, error != K::None ? ui::A2 : ui::MUTED);

  // Barra de tempo: com foco fica mais grossa e ganha o ponto; a busca pendente mostra o destino.
  double shown = seekPending ? seekTarget : pos;
  float fraction = duration > 0 ? (float)std::clamp(shown / duration, 0.0, 1.0) : 0;
  bool barFocus = focus(ZoneSeek);
  float bh = barFocus ? 10 : 6, by = L.barY - (bh - 6) / 2;
  gfx::rect(g, cx, by, L.leftW, bh, ui::SURFACE2, bh / 2);
  if (fraction > 0) gfx::hgrad(g, cx, by, std::max(bh, fraction * L.leftW), bh, ui::A1, ui::A2, bh / 2);
  if (barFocus) gfx::rect(g, cx + fraction * L.leftW - 9, L.barY + 3 - 9, 18, 18, ui::TEXT, 9);
  gfx::text(g, ui::F_DETAIL, fmtTime(shown), cx, L.timeY, ui::MUTED);
  gfx::text(g, ui::F_DETAIL, fmtTime(duration), cx + L.leftW, L.timeY, ui::MUTED, 2);
  if (error == K::None && waiting) gfx::text(g, ui::F_DETAIL, tr(K::Loading), cx + L.leftW / 2, L.timeY, ui::A2, 1);

  // Botões: anterior, -10, tocar/pausar, +30, próxima, joinhas, repetir, aleatório
  {
    float big = ui::large ? 64 : 54, small = ui::large ? 44 : 36;
    const float signW = 7, signGap = 3, seekW = std::max(gfx::textWidth(g, ui::F_SMALL, "10"), gfx::textWidth(g, ui::F_SMALL, "30")) + signW + signGap + 6;
    float widths[BtnCount], total = 0;
    for (int tries = 0; tries < 12; tries++) {  // encolhe o play e os ícones até caber com folga (texto grande e 4:3)
      total = 0;
      for (int i = 0; i < BtnCount; i++) { widths[i] = i == BtnPlay ? big : (i == BtnBack10 || i == BtnForward30) ? seekW : small; total += widths[i]; }
      if (total + (BtnCount - 1) * 4 <= L.leftW) break;
      big *= 0.92f; small *= 0.92f;
    }
    float gap = (L.leftW - total) / (BtnCount - 1), x = cx;
    for (int i = 0; i < BtnCount; i++) {
      float w = widths[i], mx = x + w / 2, my = L.btnY;
      bool sel = focus(ZoneButtons) && nowSel == i;
      bool lit = (i == BtnLike && rating == "LIKE") || (i == BtnDislike && rating == "DISLIKE") || (i == BtnRepeat && repeat != musicqueue::OFF) || (i == BtnShuffle && shuffled);
      if (i == BtnPlay) {
        if (sel) gfx::rect(g, mx - w / 2 - 5, my - w / 2 - 5, w + 10, w + 10, ui::A1, w / 2 + 5);
        gfx::rect(g, mx - w / 2, my - w / 2, w, w, ui::TEXT, w / 2);
      }
      else if (sel) gfx::rect(g, mx - w / 2, my - w / 2, w, w, ui::TEXT, w / 2);
      Color c = i == BtnPlay ? ui::INK : sel ? ui::INK : lit ? ui::A2 : rgb(0xe4e4e7);
      if (i == BtnPrev || i == BtnNext || i == BtnShuffle) transportGlyph(g, i, mx, my, c);
      else if (i == BtnBack10 || i == BtnForward30) {  // sinal desenhado e número em texto: no árabe "-10" viraria "10-"
        const char* digits = i == BtnBack10 ? "10" : "30";
        float tw = gfx::textWidth(g, ui::F_SMALL, digits), sign = signW, gap = signGap, left = mx - (sign + gap + tw) / 2;
        gfx::rect(g, left, my - 1, sign, 2, c, 1);
        if (i == BtnForward30) gfx::rect(g, left + sign / 2 - 1, my - sign / 2, 2, sign, c, 1);
        gfx::text(g, ui::F_SMALL, digits, left + sign + gap, my - g.fonts[ui::F_SMALL].size / 2 - 2, c);
      }
      else if (i == BtnPlay) { if (i == BtnPlay) atlasIcon(g, paused ? icons::PLAY : icons::PAUSE, mx, my, true, ui::INK); }
      else if (i == BtnLike) glyph(g, rating == "LIKE" ? icons::THUMB_UP_FILL : icons::THUMB_UP, false, std::round(mx - 12), std::round(my - 12), c);
      else if (i == BtnDislike) glyph(g, rating == "DISLIKE" ? icons::THUMB_DOWN_FILL : icons::THUMB_DOWN, false, std::round(mx - 12), std::round(my - 12), c);
      else if (i == BtnRepeat) {
        atlasIcon(g, icons::REPEAT, mx, my, lit, c);
        if (repeat == musicqueue::ONE) { gfx::rect(g, mx + 4, my - 14, 14, 14, ui::A1, 7); gfx::text(g, ui::F_BADGE, "1", mx + 11, my - 14 + (14 - g.fonts[ui::F_BADGE].size) / 2 - 1, ui::TEXT, 1); }
      }
      x += w + gap;
    }
  }

  // Direita: abas e conteúdo
  {
    float x = L.rightX, y = 72;
    const K names[] = {K::Queue, K::Lyrics};
    for (int i = 0; i < 2; i++) {
      float w = gfx::textWidth(g, ui::F_KEY, tr(names[i])) + 36, h = ui::large ? 48 : 40;
      bool on = tab == i, f = focus(ZoneTabs) && on;
      gfx::rect(g, x, y, w, h, f ? ui::TEXT : on ? ui::SURFACE2 : ui::alpha(ui::SURFACE2, 0.0f), h / 2);
      gfx::text(g, ui::F_KEY, tr(names[i]), x + w / 2, y + (h - g.fonts[ui::F_KEY].size) / 2 - 2, f ? ui::INK : on ? ui::TEXT : ui::MUTED, 1);
      x += w + 8;
    }
    if (moving) gfx::text(g, ui::F_DETAIL, tr(K::MusicMovingTrack), L.rightX + L.rightW, y + 12, ui::A2, 2);
    else if (tab == 0 && !queue.empty()) {
      size_t at = queueIndex;  // com fila do servidor o índice local não anda: procura a faixa que toca
      for (size_t i = 0; i < queue.size(); i++) if (queue[i].id == playing.id) { at = i; break; }
      std::string count = std::to_string(at + 1) + " / " + std::to_string(queue.size());
      gfx::text(g, ui::F_DETAIL, count, L.rightX + L.rightW, y + 12, ui::MUTED, 2);
    }
    if (tab == 0) {
      if (queue.empty()) gfx::text(g, ui::F_KEY, tr(K::MusicQueueEmpty), L.rightX, L.listY + 8, ui::MUTED);
      if (rowsFor != (int)queue.size() || rowsWidth != L.rightW || rowsRev != queueRev) {
        rowTitle.assign(queue.size(), ""); rowArtist.assign(queue.size(), ""); rowsFor = (int)queue.size(); rowsWidth = L.rightW; rowsRev = queueRev;
      }
      queueTop = musicqueue::windowTop(queueSel, queueTop, L.rows, (int)queue.size());
      float thumb = L.rowH - 14, textW = L.rightW - thumb - 28 - 70;
      for (int r = 0; r < L.rows && queueTop + r < (int)queue.size(); r++) {
        int i = queueTop + r; const auto& item = queue[(size_t)i];
        float ry = L.listY + r * L.rowH;
        bool current = item.id == playing.id, sel = focus(ZoneQueue) && i == queueSel;
        if (rowTitle[(size_t)i].empty()) {
          rowTitle[(size_t)i] = gfx::ellipsize(g, ui::F_KEY, item.title, textW);
          rowArtist[(size_t)i] = gfx::ellipsize(g, ui::F_CHAN, item.channel, textW);
        }
        if (sel) gfx::rect(g, L.rightX - 4, ry - 3, L.rightW + 8, L.rowH - 2, moving ? ui::A1 : ui::TEXT, 14);
        gfx::rect(g, L.rightX, ry, L.rightW, L.rowH - 8, current ? ui::SURFACE : sel ? ui::SURFACE2 : ui::alpha(ui::SURFACE, 0.0f), 12);
        artwork(g, thumbs, worker, item.image.empty() ? item.id : item.image, frame, L.rightX + 7, ry + 3, thumb - 6, thumb - 6, true);
        Color tc = current ? ui::A2 : (sel && !moving ? ui::TEXT : ui::TEXT);
        gfx::text(g, ui::F_KEY, rowTitle[(size_t)i], L.rightX + thumb + 14, ry + 4, tc);
        gfx::text(g, ui::F_CHAN, rowArtist[(size_t)i], L.rightX + thumb + 14, ry + 4 + (ui::large ? 32 : 27), ui::MUTED);
        if (item.duration > 0) gfx::text(g, ui::F_DETAIL, fmtTime(item.duration), L.rightX + L.rightW - 12, ry + (L.rowH - 8 - g.fonts[ui::F_DETAIL].size) / 2, ui::MUTED, 2);
      }
    } else {
      float ly = L.listY + 4, lh = ui::large ? 40 : 33;
      std::string sig = std::to_string(lyrics.size()) + "/" + std::to_string((int)L.rightW);
      if (lyricWrapSig != sig) { lyricWrapSig = sig; lyricWrap = lyrics.empty() ? std::vector<std::string>{} : gfx::wrap(g, ui::F_KEY, lyrics, L.rightW - 8, 400); }
      int visible = std::max(3, (int)((L.hintY - 12 - ly) / lh));
      if (lyricWrap.empty()) gfx::text(g, ui::F_KEY, noLyrics, L.rightX, ly, ui::MUTED);
      for (size_t i = (size_t)lyricScroll; i < std::min(lyricWrap.size(), (size_t)lyricScroll + (size_t)visible); i++) { gfx::text(g, ui::F_KEY, lyricWrap[i], L.rightX, ly, ui::TEXT); ly += lh; }
    }
  }
}

inline void UiContext::musicNowInput(uint64_t now) {
  if (pl.state == player::FAILED && pl.nestFailed && !nestSkip) {
    nestSkip = true; toast = tr(K::MusicSpeakerFallback); toastUntil = now + 5000;
    startVideo(playing, pl.position(), playerFromRemote);
  }
  const NowLayout L = nowLayout();
  using Zone = MusicNavigation; using Act = ctl::Act; const auto ctx = ctl::Ctx::Music;
  auto say = [&](K k) { toast = tr(k); toastUntil = now + 2500; };
  auto restart = [&](double to) { bool paused = pl.paused; startVideo(playing, std::max(0.0, to), playerFromRemote); pl.setPaused(paused); };
  auto playQueueAt = [&](size_t i) { nav.queueIndex = i; nav.queueSel = (int)i; startVideo(nav.queue[i].video(), 0, false); };
  auto seekBy = [&](double delta) {
    if (pl.live) return;
    double base = nav.seekPending ? nav.seekTarget : pl.position();
    nav.seekTarget = std::clamp(base + delta, 0.0, pl.duration > 0 ? std::max(0.0, pl.duration - 2) : 1e9);
    nav.seekPending = true; nav.seekAt = now + 650;
  };
  auto togglePlay = [&]() { if (!pl.waiting() || pl.paused) pl.setPaused(!pl.paused); };
  auto next = [&]() {
    size_t to = 0;
    if (nav.localQueue) { if (nav.skipNext(to)) playQueueAt(to); else stopVideo(); }
    else if (!nav.busy) nav.action(navWorker, "/api/nav/transport?action=next");
  };
  auto previous = [&]() {
    size_t to = 0;
    if (nav.localQueue && nav.skipPrevious(pl.position(), to)) { if (to == nav.queueIndex) restart(0); else playQueueAt(to); }
    else restart(0);
  };
  auto toggleVideo = [&]() {
    double pos = pl.position(); bool paused = pl.paused;
    nav.videoMode = !nav.videoMode; nav.seekPending = false;
    startVideo(playing, pos, playerFromRemote); pl.setPaused(paused);
  };
  auto rate = [&](bool like) {
    if (!account.signedIn()) { say(K::RateSignIn); return; }
    const char* want = like ? "LIKE" : "DISLIKE";
    control.add({5, "/api/video/" + playing.id + "/rate?r=" + (nav.rating == want ? "none" : like ? "like" : "dislike"), 0});
  };
  auto loadLyrics = [&]() {
    if (nav.lyricsId != playing.id && !nav.busy) { nav.lyricsId = playing.id; nav.lyrics.clear(); nav.lyricLines.clear(); nav.lyricWrapSig.clear(); nav.action(navWorker, "/api/nav/lyrics?id=" + playing.id); }
  };

  if (nav.seekPending && now >= nav.seekAt) { nav.seekPending = false; restart(nav.seekTarget); }
  if (nav.tab == 1) loadLyrics();

  // Faixas e modo valem em qualquer zona (LB RB e View do mapa)
  if (ctl::hit(in, ctx, Act::PrevItem)) previous();
  if (ctl::hit(in, ctx, Act::NextItem)) next();
  if (ctl::hit(in, ctx, Act::SongVideo)) toggleVideo();

  bool up = in.hit(Input::UP), down = in.hit(Input::DOWN), left = in.hit(Input::LEFT), right = in.hit(Input::RIGHT), a = in.hit(Input::A);
  bool pageUp = ctl::hit(in, ctx, Act::PageUp), pageDown = ctl::hit(in, ctx, Act::PageDown);
  bool menu = ctl::hit(in, ctx, Act::Options);
  int count = (int)nav.queue.size();
  switch (nav.nowZone) {
    case Zone::ZoneButtons:
      if (left && nav.nowSel > 0) nav.nowSel--;
      if (right) { if (nav.nowSel + 1 < Zone::BtnCount) nav.nowSel++; else if (count || nav.tab == 1) nav.nowZone = Zone::ZoneQueue; }
      if (up) nav.nowZone = Zone::ZoneSeek;
      if (a) switch (nav.nowSel) {
        case Zone::BtnPrev: previous(); break;
        case Zone::BtnBack10: seekBy(-10); break;
        case Zone::BtnPlay: togglePlay(); break;
        case Zone::BtnForward30: seekBy(30); break;
        case Zone::BtnNext: next(); break;
        case Zone::BtnLike: rate(true); break;
        case Zone::BtnDislike: rate(false); break;
        case Zone::BtnRepeat:
          nav.repeat = (nav.repeat + 1) % 3;
          say(nav.repeat == musicqueue::OFF ? K::MusicRepeatOff : nav.repeat == musicqueue::ALL ? K::MusicRepeatAll : K::MusicRepeatOne);
          break;
        case Zone::BtnShuffle:
          if (!nav.localQueue) { say(K::MusicQueueServer); break; }
          nav.toggleShuffle((uint32_t)(SDL_GetTicks() * 2654435761u));
          say(nav.shuffled ? K::MusicShuffleOn : K::MusicShuffleOff);
          break;
      }
      break;
    case Zone::ZoneSeek:
      if (left) seekBy(-10);
      if (right) seekBy(10);
      if (down) nav.nowZone = Zone::ZoneButtons;
      if (up) nav.nowZone = Zone::ZoneMode;
      if (a) togglePlay();
      break;
    case Zone::ZoneMode:
      if (a) toggleVideo();
      if (down) nav.nowZone = Zone::ZoneSeek;
      if (right) nav.nowZone = Zone::ZoneTabs;
      if (left) toggleVideo();
      break;
    case Zone::ZoneTabs:
      if (left) { if (nav.tab > 0) nav.tab--; else nav.nowZone = Zone::ZoneMode; }
      if (right && nav.tab < 1) nav.tab++;
      if (down) nav.nowZone = Zone::ZoneQueue;
      break;
    case Zone::ZoneQueue:
      if (nav.tab == 1) {
        int visible = std::max(3, (int)((L.hintY - 12 - (L.listY + 4)) / (ui::large ? 40 : 33)));
        int total = (int)nav.lyricWrap.size();
        if (up && nav.lyricScroll > 0) nav.lyricScroll--; else if (up) nav.nowZone = Zone::ZoneTabs;
        if (down && nav.lyricScroll + visible < total) nav.lyricScroll++;
        if (pageUp) nav.lyricScroll = std::max(0, nav.lyricScroll - visible);
        if (pageDown) nav.lyricScroll = std::max(0, std::min(nav.lyricScroll + visible, total - visible));
        if (left) nav.nowZone = Zone::ZoneButtons;
        break;
      }
      if (nav.moving) {
        if (up || down) {
          size_t to = musicqueue::move(nav.queue, nav.queueIndex, (size_t)nav.queueSel, up ? -1 : 1);
          if ((int)to != nav.queueSel) { nav.queueSel = (int)to; nav.queueChanged(); nav.saveOrderBroken(); }
        }
        if (a || menu) nav.moving = false;
        break;
      }
      if (up) { if (nav.queueSel > 0) nav.queueSel--; else nav.nowZone = Zone::ZoneTabs; }
      if (down && nav.queueSel + 1 < count) nav.queueSel++;
      if (pageUp) nav.queueSel = std::max(0, nav.queueSel - L.rows);
      if (pageDown) nav.queueSel = std::min(count - 1, nav.queueSel + L.rows);
      if (left) nav.nowZone = Zone::ZoneButtons;
      if (a && count) {
        if (nav.localQueue) playQueueAt((size_t)nav.queueSel); else say(K::MusicQueueServer);
      }
      if (menu && count) { if (nav.localQueue) nav.moving = true; else say(K::MusicQueueServer); }
      break;
  }
  if (nav.queueSel >= count) nav.queueSel = std::max(0, count - 1);

  if (in.hit(Input::B)) {
    if (nav.moving) nav.moving = false;
    else {
      nav.resetNow();
      if (nav.localQueue) stopVideo();
      else {
        nav.ownsQueue = nav.musicSession = nav.musicNow = false; nav.busy = false; nav.cancelQueue = true;
        nav.action(navWorker, "/api/nav/transport?action=stop"); stopVideo();
      }
    }
  }
}

inline void UiContext::musicNowDraw() {
  nav.rating = likeState;
  nav.drawNow(g, thumbs, thumbsWorker, playing, pl.position(), pl.duration, pl.paused, pl.waiting(),
      pl.state == player::FAILED ? i18n::playerError(pl.error) : K::None, frame, settings.lang);
  const NowLayout L = nowLayout();
  const auto ctx = ctl::Ctx::Music; using Act = ctl::Act;
  float hx = L.margin, y = L.hintY;
  hx = ctlHint(g, hx, y, ctx, Act::Open, settings.lang);
  hx = ctlHint(g, hx, y, ctx, Act::Back, settings.lang);
  if (!ui::narrow) hx = ctlHint(g, hx, y, ctx, Act::PrevItem, settings.lang);
  hx = ctlHint(g, hx, y, ctx, Act::SongVideo, settings.lang);
  if (nav.nowZone == MusicNavigation::ZoneQueue && nav.tab == 0 && nav.localQueue) hx = ctlHint(g, hx, y, ctx, Act::Options, settings.lang, K::MusicMoveTrack);
  if (nav.nowZone == MusicNavigation::ZoneSeek) hx = hint(g, hx, y, i18n::get(i18n::Lang::EN, K::BtnHorizontal), XG, tr(K::MusicSeekBar));
}

inline void UiContext::musicSleepDraw(double sleepLeft) {
  std::string value = sleep.end ? tr(K::SleepEnd) : fmtTime(std::ceil(sleepLeft));
  if (sleep.end && sleepLeft >= 0) value += " · " + fmtTime(std::ceil(sleepLeft));
  std::string label = std::string(tr(K::SleepTimer)) + " · " + value;
  float w = gfx::textWidth(g, ui::F_SMALL, label) + 30, x = ui::W - 64 - w;
  float y = 24;  // canto de cima à direita, acima das abas da fila
  gfx::rect(g, x, y, w, 32, ui::SURFACE2, 16);
  gfx::text(g, ui::F_SMALL, label, x + 15, y + 7, ui::MUTED);
}

// Prateleiras do início da Música: esquerda/direita andam na prateleira, cima/baixo trocam de prateleira (cima da primeira sobe para as abas).
inline void UiContext::musicHomeMove(NavPage& p, bool left, bool right, bool up, bool down) {
  auto& shelf = p.shelves[p.shelf];
  if (left && p.sel > shelf.first) shelf.selected--;
  if (right && p.sel + 1 < shelf.end) shelf.selected++;
  if (up && p.shelf == 0) { if (p.kind == "music") p.zone = 1; }
  else if (up) p.shelf--;
  if (down && p.shelf + 1 < (int)p.shelves.size()) p.shelf++;
  p.sel = p.shelves[p.shelf].selected;
}

// Troca de aba da Música: Início, Explorar, Paradas, Mixes (ou Tudo, Músicas, Álbuns, Artistas na busca); no início a aba vira a consulta.
inline void UiContext::musicTabSelected(NavPage& p) {
  p.tab = p.tabSel == 1 ? "live" : p.tabSel == 2 ? "playlists" : "videos";
  if (p.kind == "music") p.query = p.tabSel == 1 ? "explore" : p.tabSel == 2 ? "charts" : p.tabSel == 3 ? "mixes" : "";
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
    const K musicTabs[] = {K::Home,K::MusicExplore,K::Charts,K::Mixes};
    const K songTabs[] = {K::AllResults,K::Songs,K::Albums,K::Artists,K::Videos,K::MusicFeatured};
    int count = p.kind == "music_search" ? 6 : channel || p.kind == "music" ? 4 : 3; float x = home && !ui::narrow ? 650 : 136;
    float tabY = home ? (ui::narrow ? 88 : 32) : 166;
    navTabs(d, channel ? channelTabs : p.kind == "music" ? musicTabs : songTabs, count, x, tabY);
  }
  float top = 246;
  auto m = navMetrics(d);
  if (home) {
    auto chipShelf = [&](int row) { return !p.items.empty() && p.items[(size_t)p.shelves[(size_t)row].first].pill(); };
    const bool topChips = !p.shelves.empty() && chipShelf(0) && p.items[(size_t)p.shelves[0].first].section == "chips";
    const float card = ui::narrow ? 112 : 140, gapX = 24, titleH = ui::large ? 36 : 22, bylineH = ui::large ? 34 : 20, pillH = ui::large ? 46 : 38;
    const float cardRow = 30 + card + 4 + titleH + bylineH + 14, pillRow = 30 + pillH + 18, bottom = 646, availW = ui::W - 136 - 48;
    // fileira de pílulas (chips de clima, botões do Explorar): rola para manter a escolhida à vista
    auto drawPills = [&](int row, float y) {
      const auto& shelf = p.shelves[(size_t)row];
      auto widthOf = [&](int i) { return gfx::textWidth(g, ui::F_SMALL, gfx::ellipsize(g, ui::F_SMALL, p.items[(size_t)i].title, 220)) + 34 + 10; };
      int first = shelf.first; float used = 0;
      for (int i = shelf.first; i <= std::min(shelf.selected, shelf.end - 1); i++) used += widthOf(i);
      while (used > availW && first < shelf.selected) { used -= widthOf(first); first++; }
      float x = 136;
      for (int i = first; i < shelf.end; i++) {
        const auto& chip = p.items[(size_t)i];
        std::string label = gfx::ellipsize(g, ui::F_SMALL, chip.title, 220);
        float w = widthOf(i) - 10;
        if (x + w > 136 + availW) break;
        bool f = d.contentFocus && p.zone == 3 && p.shelf == row && i == p.sel, on = chip.id == p.id;
        gfx::rect(g, x, y, w, pillH, f ? ui::TEXT : on ? ui::A1 : ui::SURFACE2, pillH / 2);
        gfx::text(g, ui::F_SMALL, label, x + w / 2, y + (pillH - g.fonts[ui::F_SMALL].size) / 2 - 1, f ? ui::INK : ui::TEXT, 1);
        x += w + 10;
      }
    };
    float top0 = ui::narrow ? 150 : 96;
    if (topChips) { drawPills(0, top0 - 4); top0 += pillH + 18; }
    const int base = topChips ? 1 : 0, count = (int)p.shelves.size();
    auto heightOf = [&](int row) { return chipShelf(row) ? pillRow : cardRow; };
    int firstShelf = std::clamp(p.shelf, base, std::max(base, count - 1));
    float used = count > base ? heightOf(firstShelf) : 0;
    while (firstShelf - 1 >= base && used + heightOf(firstShelf - 1) <= bottom - top0) { --firstShelf; used += heightOf(firstShelf); }
    const int columns = std::max(2, (int)((availW + gapX) / (card + gapX)));
    float y = top0;
    for (int row = firstShelf; row < count && y + heightOf(row) <= bottom + 40; row++) {
      const auto& shelf = p.shelves[(size_t)row];
      gfx::text(g, ui::F_KEY, shelf.title, 136, y, ui::TEXT);
      if (chipShelf(row)) { drawPills(row, y + 30); y += pillRow; continue; }
      int first = std::max(shelf.first, shelf.selected-columns+1);
      for (int i = first; i < std::min(shelf.end, first+columns); i++) {
        const auto& item = p.items[(size_t)i]; float x = 136 + (i - first) * (card + gapX);
        bool f = d.contentFocus && p.zone == 3 && i == p.sel;
        if (f) gfx::rect(g, x - 4, y + 26, card + 8, card + 8, ui::TEXT, 12);
        artwork(g, d.thumbs, d.worker, item.image, d.frame, x, y + 30, card, card, true, item.type == "artist");
        gfx::textBox(g, ui::F_SMALL, item.titleLines[0], x, y + 30 + card + 4, card + gapX - 6, ui::TEXT);
        gfx::textBox(g, ui::F_TINY, item.byline, x, y + 30 + card + 4 + titleH, card + gapX - 6, ui::MUTED);
        memberLine(g, item.members, d.lang, x, y + 30 + card + 4 + titleH + bylineH, card);
      }
      y += cardRow;
    }
  } else navGrid(d, top, m);
  return top;
}
