// Música: as páginas (início com prateleiras, busca, álbum, artista, rádio, playlist de música), a tela "Tocando agora"
// (capa, fila, letra e botões) e o que a Música acrescenta por cima do player. A entrada genérica das páginas está em
// ui_collection.hpp; aqui ficam os ganchos da Música.
#pragma once
#include "ui_context.hpp"

// ---- Tocando agora -------------------------------------------------------------------------------------------------
// Esquerda: seletor Música/Vídeo, capa (ou o vídeo), título, barra de tempo e os botões. Direita: abas "A seguir" e
// "Letra" e a lista da fila. O foco anda entre cinco zonas (MusicNavigation::Zone).
struct NowLayout {
  float areaX, leftX, leftW, cover, rightX, rightW, coverY, titleY, artistY, barY, timeY, btnY, tabsY, tabsH, autoY, queueTitleY, listY, rowH, hintY;
  int rows;
  bool stacked;  // 4:3 e texto grande: a chave de reprodução automática desce para uma linha própria
};
// Com o trilho lateral (96) à esquerda. rtl: capa e controles à direita, fila à esquerda (o trilho continua à esquerda).
static NowLayout nowLayout(bool rtl = false) {
  NowLayout L{};
  const float padArea = ui::narrow ? 24 : 40, padEdge = ui::narrow ? 30 : 48, gap = ui::narrow ? 28 : 40;
  L.areaX = ui::RAIL; L.leftW = ui::narrow ? 280 : 360;
  L.rightW = ui::W - L.areaX - padArea - L.leftW - gap - padEdge;
  L.leftX = rtl ? ui::W - padEdge - L.leftW : L.areaX + padArea;
  L.rightX = rtl ? L.areaX + padArea : L.leftX + L.leftW + gap;
  L.cover = ui::large && !ui::narrow ? 250 : L.leftW;  // texto grande em 16:9: capa menor para as duas fileiras de botões caberem
  L.coverY = 74; L.titleY = L.coverY + L.cover + 12; L.artistY = L.titleY + 44;
  L.barY = L.artistY + (ui::large ? 40 : 34) + 12; L.timeY = L.barY + 12; L.btnY = L.timeY + (ui::large ? 36 : 28) + 28;
  L.stacked = ui::narrow || ui::large;
  L.tabsY = 72; L.tabsH = ui::large ? 48 : 40;
  L.autoY = L.tabsY + L.tabsH + 6;
  L.queueTitleY = L.stacked ? L.autoY + 34 : L.tabsY + L.tabsH + 6;
  L.listY = L.queueTitleY + (ui::large ? 36 : 28);
  L.hintY = 677; L.rowH = ui::large ? 72 : 62;
  L.rows = std::max(3, (int)((L.hintY - 12 - L.listY) / L.rowH));
  return L;
}

// Altura e quantidade de linhas da lista (fila ou relacionados): com música só para membros na lista, cada linha ganha uma
// terceira linha para "Só para membros" (ela nunca some, nem em 4:3 nem com artista longo). Desenho e controle usam esta conta.
struct NowRows { float rowH; int rows; };
static NowRows nowRows(const NowLayout& L, gfx::Gfx& g, const std::vector<NavItem>& list) {
  bool members = false;
  for (const auto& item : list) if (item.members != yt::Membership::None) { members = true; break; }
  NowRows r; r.rowH = L.rowH + (members ? g.fonts[ui::F_TINY].lineH + 2 : 0);
  r.rows = std::max(3, (int)((L.hintY - 12 - L.listY) / r.rowH));
  return r;
}

inline void MusicNavigation::prepareNow(gfx::Gfx& g, Video& playing, i18n::Lang lang) {
  auto L = nowLayout(g.rtlText);
  bool layoutChanged = builtWidth != ui::W || builtLarge != ui::large;
  if (nowId == playing.id && !layoutChanged) return;
  if (nowId != playing.id) {
    lyrics.clear(); lyricsId.clear(); lyricLines.clear(); lyricScroll = 0; seekPending = false; lyricsFailed = false;
    if (localQueue && nowZone != ZoneQueue) queueSel  /* quem está navegando na fila não perde o lugar a cada troca de faixa */ = (int)std::min(queueIndex, queue.size() ? queue.size() - 1 : 0);
  }
  builtWidth = ui::W; builtLarge = ui::large;
  nowId = playing.id; const NavItem* current = nullptr;
  for (size_t i = 0; i < queue.size(); i++) if (queue[i].id == playing.id) { current = &queue[i]; break; }
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
  const bool rtl = g.rtlText;
  const NowLayout L = nowLayout(rtl);
  auto focus = [&](int zone) { return nowZone == zone && !railHasFocus; };
  if (rtl) gfx::hgrad(g, L.leftX - 24, 0, ui::W - L.leftX + 24, 720, ui::BG, rgb(0x37151f));
  else gfx::hgrad(g, 0, 0, L.rightX - 24, 720, rgb(0x37151f), ui::BG);

  // Seletor Música | Vídeo
  {
    float sw = ui::large ? 150 : 120, sh = ui::large ? 48 : 40, x = L.leftX, y = 18;
    gfx::rect(g, x - (focus(ZoneMode) ? 4 : 0), y - (focus(ZoneMode) ? 4 : 0), 2 * sw + (focus(ZoneMode) ? 8 : 0), sh + (focus(ZoneMode) ? 8 : 0), focus(ZoneMode) ? ui::TEXT : ui::SURFACE2, sh / 2 + 4);
    gfx::rect(g, x, y, 2 * sw, sh, ui::SURFACE2, sh / 2);
    gfx::rect(g, x + (videoMode ? sw : 0) + 3, y + 3, sw - 6, sh - 6, ui::TEXT, sh / 2 - 3);
    gfx::text(g, ui::F_KEY, tr(K::MusicSongTab), x + sw / 2, y + (sh - g.fonts[ui::F_KEY].size) / 2 - 2, videoMode ? ui::TEXT : ui::INK, 1);
    gfx::text(g, ui::F_KEY, tr(K::MusicVideoTab), x + sw * 1.5f, y + (sh - g.fonts[ui::F_KEY].size) / 2 - 2, videoMode ? ui::INK : ui::TEXT, 1);
  }

  // Capa ou vídeo
  float cx = L.leftX, cy = L.coverY, cs = L.cover;
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

  // Botões: anterior, -10, tocar/pausar, +30, próxima, joinhas, repetir, aleatório. Em 4:3 e texto grande vão em duas fileiras
  // (mídia em cima, avaliação e modos embaixo), porque numa só os ícones se encostam.
  {
    float big = ui::large ? 64 : 54, small = ui::large ? 44 : 36;
    const float signW = 7, signGap = 3, seekW = std::max(gfx::textWidth(g, ui::F_SMALL, "10"), gfx::textWidth(g, ui::F_SMALL, "30")) + signW + signGap + 6;
    const int firstRow = L.stacked ? BtnLike : BtnCount;  // índices abaixo disto ficam na 1ª fileira
    auto drawButton = [&](int i, float mx, float my, float w) {
      bool sel = focus(ZoneButtons) && nowSel == i;
      const bool dim = !localQueue && (i == BtnRepeat || i == BtnShuffle);  // fila sem dono local: sem repetir nem aleatório
      bool lit = (i == BtnLike && rating == "LIKE") || (i == BtnDislike && rating == "DISLIKE") || (i == BtnRepeat && repeat != musicqueue::OFF) || (i == BtnShuffle && shuffled);
      if (i == BtnPlay) {
        if (sel) gfx::rect(g, mx - w / 2 - 5, my - w / 2 - 5, w + 10, w + 10, ui::A1, w / 2 + 5);
        gfx::rect(g, mx - w / 2, my - w / 2, w, w, ui::TEXT, w / 2);
      }
      else if (sel) gfx::rect(g, mx - w / 2, my - w / 2, w, w, ui::TEXT, w / 2);
      Color c = i == BtnPlay ? ui::INK : sel ? ui::INK : lit ? ui::A2 : dim ? ui::alpha(rgb(0xe4e4e7), .35f) : rgb(0xe4e4e7);
      if (i == BtnPrev || i == BtnNext || i == BtnShuffle) transportGlyph(g, i, mx, my, c);
      else if (i == BtnBack10 || i == BtnForward30) {  // sinal desenhado e número em texto: no árabe "-10" viraria "10-"
        const char* digits = i == BtnBack10 ? "10" : "30";
        float tw = gfx::textWidth(g, ui::F_SMALL, digits), left = mx - (signW + signGap + tw) / 2;
        gfx::rect(g, left, my - 1, signW, 2, c, 1);
        if (i == BtnForward30) gfx::rect(g, left + signW / 2 - 1, my - signW / 2, 2, signW, c, 1);
        gfx::text(g, ui::F_SMALL, digits, left + signW + signGap, my - g.fonts[ui::F_SMALL].size / 2 - 2, c);
      }
      else if (i == BtnPlay) atlasIcon(g, paused ? icons::PLAY : icons::PAUSE, mx, my, true, ui::INK);
      else if (i == BtnLike) glyph(g, rating == "LIKE" ? icons::THUMB_UP_FILL : icons::THUMB_UP, false, std::round(mx - 12), std::round(my - 12), c);
      else if (i == BtnDislike) glyph(g, rating == "DISLIKE" ? icons::THUMB_DOWN_FILL : icons::THUMB_DOWN, false, std::round(mx - 12), std::round(my - 12), c);
      else if (i == BtnRepeat) {
        atlasIcon(g, icons::REPEAT, mx, my, lit, c);
        if (repeat == musicqueue::ONE) { gfx::rect(g, mx + 4, my - 14, 14, 14, ui::A1, 7); gfx::text(g, ui::F_BADGE, "1", mx + 11, my - 14 + (14 - g.fonts[ui::F_BADGE].size) / 2 - 1, ui::TEXT, 1); }
      }
    };
    for (int rowIndex = 0; rowIndex < (L.stacked ? 2 : 1); rowIndex++) {
      const int from = rowIndex ? firstRow : 0, to = rowIndex ? BtnCount : firstRow, n = to - from;
      float widths[BtnCount], total = 0, bigR = big, smallR = small;
      for (int tries = 0; tries < 12; tries++) {  // encolhe o play e os ícones até caber com folga
        total = 0;
        for (int i = from; i < to; i++) { widths[i] = i == BtnPlay ? bigR : (i == BtnBack10 || i == BtnForward30) ? seekW : smallR; total += widths[i]; }
        if (total + (n - 1) * 10 <= L.leftW) break;
        bigR *= 0.92f; smallR *= 0.92f;
      }
      float gap = n > 1 ? (L.leftW - total) / (n - 1) : 0, x = L.leftX, my = L.btnY + rowIndex * (big + 10);
      if (rowIndex && n > 1) { gap = std::min(gap, 36.f); x = L.leftX + (L.leftW - total - gap * (n - 1)) / 2; }  // a 2ª fileira fica centrada
      for (int i = from; i < to; i++) { drawButton(i, x + widths[i] / 2, my, widths[i]); x += widths[i] + gap; }
    }
  }

  // Direita: abas (A seguir, Letra, Relacionados), chave de reprodução automática, de onde veio a fila e o conteúdo
  {
    const float h = L.tabsH, edge = L.rightX + L.rightW;
    const K names[] = {K::Queue, K::Lyrics, K::MusicRelated};
    // as três abas têm de caber na coluna: se a fonte normal não couber (4:3, texto grande) usa a menor
    int tabFont = ui::F_KEY; float pad = 36, total = 0;
    for (int pass = 0; pass < 2; pass++) {
      total = 0; for (int i = 0; i < 3; i++) total += gfx::textWidth(g, tabFont, tr(names[i])) + pad + 8;
      if (total <= L.rightW || tabFont != ui::F_KEY) break;
      tabFont = ui::F_SMALL; pad = 24;
    }
    float x = rtl ? edge : L.rightX;
    for (int i = 0; i < 3; i++) {
      float w = gfx::textWidth(g, tabFont, tr(names[i])) + pad;
      float px = rtl ? x - w : x;
      bool on = tab == i, f = focus(ZoneTabs) && !autoFocus && on;
      gfx::rect(g, px, L.tabsY, w, h, f ? ui::TEXT : on ? ui::SURFACE2 : ui::alpha(ui::SURFACE2, 0.0f), h / 2);
      gfx::text(g, tabFont, tr(names[i]), px + w / 2, L.tabsY + (h - g.fonts[tabFont].size) / 2 - 2, f ? ui::INK : on ? ui::TEXT : ui::MUTED, 1);
      x += rtl ? -(w + 8) : w + 8;
    }
    // reprodução automática: chave com foco próprio (na fileira das abas ou, em 4:3 e texto grande, logo abaixo)
    {
      const char* label = tr(K::MusicAutoplay);
      float sw = 44, sh = 24, tw = gfx::textWidth(g, ui::F_DETAIL, label), width = tw + 12 + sw, ay = L.stacked ? L.autoY : L.tabsY;
      float bx = rtl ? L.rightX : edge - width, ah = L.stacked ? 30 : h;
      bool f = focus(ZoneTabs) && autoFocus, usable = localQueue;
      if (f) gfx::rect(g, bx - 10, ay, width + 20, ah, ui::TEXT, ah / 2);
      gfx::text(g, ui::F_DETAIL, label, bx, ay + (ah - g.fonts[ui::F_DETAIL].size) / 2 - 1, f ? ui::INK : ui::alpha(ui::MUTED, usable ? 1.f : .45f));
      float kx = bx + tw + 12, ky = ay + (ah - sh) / 2;
      gfx::rect(g, kx, ky, sw, sh, autoplay && usable ? ui::A1 : (f ? ui::alpha(ui::INK, 0.3f) : ui::SURFACE2), sh / 2);
      gfx::rect(g, autoplay && usable ? kx + sw - sh + 3 : kx + 3, ky + 3, sh - 6, sh - 6, ui::TEXT, (sh - 6) / 2);
    }
    if (moving) gfx::text(g, ui::F_DETAIL, gfx::ellipsize(g, ui::F_DETAIL, tr(K::MusicMovingTrack), L.rightW), rtl ? edge : L.rightX, L.queueTitleY, ui::A2, rtl ? 2 : 0);
    else if (tab == 0 && !queueTitle.empty())
      gfx::text(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, queueTitle, L.rightW), rtl ? edge : L.rightX, L.queueTitleY, ui::MUTED, rtl ? 2 : 0);
    if (tab != 1) {
      const std::vector<NavItem>& list = tab == 2 ? related : queue;
      const NowRows R = nowRows(L, g, list);
      int& sel = tab == 2 ? relatedSel : queueSel; int& top = tab == 2 ? relatedTop : queueTop;
      if (list.empty()) gfx::text(g, ui::F_KEY, tr(tab == 2 ? (relatedLoaded ? K::MusicRelatedEmpty : K::Loading) : K::MusicQueueEmpty), rtl ? edge : L.rightX, L.listY + 8, ui::MUTED, rtl ? 2 : 0);
      if (rowsFor != (int)list.size() || rowsWidth != L.rightW || rowsRev != queueRev || rowsTab != tab) {
        rowTitle.assign(list.size(), ""); rowArtist.assign(list.size(), ""); rowsFor = (int)list.size(); rowsWidth = L.rightW; rowsRev = queueRev; rowsTab = tab;
      }
      top = musicqueue::windowTop(sel, top, R.rows, (int)list.size());
      float thumb = R.rowH - 14, textW = L.rightW - thumb - 28 - 70;
      for (int r = 0; r < R.rows && top + r < (int)list.size(); r++) {
        int i = top + r; const auto& item = list[(size_t)i];
        float ry = L.listY + r * R.rowH;
        bool current = tab == 0 && item.id == playing.id, on = focus(ZoneQueue) && i == sel;
        if (rowTitle[(size_t)i].empty()) {
          rowTitle[(size_t)i] = gfx::ellipsize(g, ui::F_KEY, item.title, textW);
          rowArtist[(size_t)i] = gfx::ellipsize(g, ui::F_CHAN, item.channel, textW);
        }
        if (on) gfx::rect(g, L.rightX - 4, ry - 3, L.rightW + 8, R.rowH - 2, moving ? ui::A1 : ui::TEXT, 14);
        gfx::rect(g, L.rightX, ry, L.rightW, R.rowH - 8, current ? ui::SURFACE : on ? ui::SURFACE2 : ui::alpha(ui::SURFACE, 0.0f), 12);
        const float tx = rtl ? edge - 7 - (thumb - 6) : L.rightX + 7;
        artwork(g, thumbs, worker, item.image.empty() ? item.id : item.image, frame, tx, ry + 3, thumb - 6, thumb - 6, true);
        const float textX = rtl ? tx - 14 : L.rightX + thumb + 14, dy = ry + 4 + (ui::large ? 32 : 27);
        Color tc = current ? ui::A2 : ui::TEXT;
        gfx::text(g, ui::F_KEY, rowTitle[(size_t)i], textX, ry + 4, tc, rtl ? 2 : 0);
        gfx::text(g, ui::F_CHAN, rowArtist[(size_t)i], textX, dy, ui::MUTED, rtl ? 2 : 0);
        if (item.members != yt::Membership::None) memberLine(g, item.members, lang, rtl ? textX - textW : textX, dy + g.fonts[ui::F_CHAN].lineH, textW);
        if (item.duration > 0) gfx::text(g, ui::F_DETAIL, fmtTime(item.duration), rtl ? L.rightX + 12 : edge - 12, ry + (R.rowH - 8 - g.fonts[ui::F_DETAIL].size) / 2, ui::MUTED, rtl ? 0 : 2);
      }
    } else {
      float ly = L.listY + 4, lh = ui::large ? 40 : 33;
      std::string sig = std::to_string(lyrics.size()) + "/" + std::to_string((int)L.rightW);
      if (lyricWrapSig != sig) { lyricWrapSig = sig; lyricWrap = lyrics.empty() ? std::vector<std::string>{} : gfx::wrap(g, ui::F_KEY, lyrics, L.rightW - 24, 400); }
      int visible = std::max(3, (int)((L.hintY - 12 - ly) / lh));
      if (focus(ZoneQueue)) {  // foco da letra: moldura no painel (rola com cima e baixo)
        gfx::rect(g, L.rightX - 6, L.listY - 6, L.rightW + 12, L.hintY - 8 - L.listY + 6, ui::TEXT, 14);
        gfx::rect(g, L.rightX - 3, L.listY - 3, L.rightW + 6, L.hintY - 8 - L.listY, ui::BG, 12);
      }
      const float tx = rtl ? edge - 12 : L.rightX + 12;
      if (lyricsFailed) gfx::text(g, ui::F_KEY, tr(K::MusicLyricsFailed), tx, ly, ui::A2, rtl ? 2 : 0);
      else if (lyricWrap.empty()) gfx::text(g, ui::F_KEY, noLyrics, tx, ly, ui::MUTED, rtl ? 2 : 0);
      for (size_t i = (size_t)lyricScroll; i < std::min(lyricWrap.size(), (size_t)lyricScroll + (size_t)visible); i++) { gfx::text(g, ui::F_KEY, lyricWrap[i], tx, ly, ui::TEXT, rtl ? 2 : 0); ly += lh; }
    }
  }
}

inline void UiContext::musicNowInput(uint64_t now) {
  if (pl.state == player::FAILED && pl.nestFailed && !nestSkip) {
    nestSkip = true; toast = tr(K::MusicSpeakerFallback); toastUntil = now + 5000;
    startVideo(playing, pl.position(), playerFromRemote);
  }
  const bool rtl = g.rtlText;
  const NowLayout L = nowLayout(rtl);
  // Sair da Música: para a fila e fecha a tela
  auto leaveMusic = [&]() {
    const bool pendingRadio = nav.continuing;  // B também cancela a rádio de continuação que ainda não chegou
    nav.continuing = false; nav.radioFor.clear(); nav.resetNow();
    if (nav.localQueue) { if (pendingRadio) nav.cancelQueue = true; stopVideo(); }
    else {
      nav.ownsQueue = nav.musicSession = nav.musicNow = false; nav.busy = false; nav.cancelQueue = true;
      nav.action(navWorker, "/api/nav/transport?action=stop"); stopVideo();
    }
  };
  // O trilho lateral (A6/A7) recebe o foco: A em um item sai da Música (para a fila) e abre a página escolhida
  // "Celular" abre o painel por cima do Tocando agora, com a música seguindo; B fecha o painel e o foco volta ao trilho
  if (railFocus) {
    if (in.fired[Input::A] && railSel == 5) { in.hit(Input::A); phonePanel = true; return; }
    if (in.fired[Input::A]) leaveMusic();
    railInput(); return;
  }
  auto enterRail = [&]() { railFocus = true; railSel = 4; };
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
  auto continueWithRadio = [&]() {  // fim da fila com a reprodução automática ligada: segue com a rádio da faixa atual (o main pede quando o worker liberar)
    if (nav.continuing) return;
    nav.continuing = true; nav.cancelQueue = false; nav.queueStart.clear(); nav.radioFor = playing.id;
    nav.queueTitle = std::string(tr(K::MusicMixOf)) + " " + playing.title;
  };
  auto next = [&]() {
    size_t to = 0;
    if (nav.localQueue) { if (nav.skipNext(to)) playQueueAt(to); else if (nav.autoplay) continueWithRadio(); else stopVideo(); }
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
    if (nav.lyricsId != playing.id && !nav.busy) { nav.lyricsId = playing.id; nav.lyricsFailed = false; nav.lyrics.clear(); nav.lyricLines.clear(); nav.lyricWrapSig.clear(); nav.action(navWorker, "/api/nav/lyrics?id=" + playing.id); }
  };

  if (nav.seekPending && now >= nav.seekAt) { nav.seekPending = false; restart(nav.seekTarget); }
  auto loadRelated = [&]() {
    if (nav.relatedId != playing.id && !nav.busy) { nav.relatedId = playing.id; nav.related.clear(); nav.relatedLoaded = false; nav.relatedSel = nav.relatedTop = 0; nav.queueChanged(); nav.action(navWorker, "/api/nav/related?id=" + playing.id + "&lang=" + i18n::code(settings.lang)); }
  };
  if (nav.tab == 1) loadLyrics();
  if (nav.tab == 2) loadRelated();

  // Faixas e modo valem em qualquer zona (LB RB e View do mapa)
  if (ctl::hit(in, ctx, Act::PrevItem)) previous();
  if (ctl::hit(in, ctx, Act::NextItem)) next();
  if (ctl::hit(in, ctx, Act::SongVideo)) toggleVideo();

  bool up = in.hit(Input::UP), down = in.hit(Input::DOWN), left = in.hit(Input::LEFT), right = in.hit(Input::RIGHT), a = in.hit(Input::A);
  bool pageUp = false, pageDown = false;
  // LT/RT: nas listas (fila, relacionadas, letra) pulam página (mapa Music); nos botões e na barra ajustam o volume (mapa MusicNow)
  if (nav.nowZone == Zone::ZoneQueue) { pageUp = ctl::hit(in, ctx, Act::PageUp); pageDown = ctl::hit(in, ctx, Act::PageDown); }
  else {
    if (ctl::hit(in, ctl::Ctx::MusicNow, Act::VolumeDown)) adjustVolume(now, -1);
    if (ctl::hit(in, ctl::Ctx::MusicNow, Act::VolumeUp)) adjustVolume(now, +1);
  }
  bool menu = ctl::hit(in, ctx, Act::Options);
  std::vector<NavItem>& list = nav.tab == 2 ? nav.related : nav.queue;
  int& sel = nav.tab == 2 ? nav.relatedSel : nav.queueSel;
  const int count = (int)list.size();
  const NowRows R = nowRows(L, g, list);
  // Com o layout espelhado (árabe) a fila fica à esquerda: "para a fila" é a esquerda e as abas andam ao contrário
  const int prevSel = nav.nowSel;
  const bool fromQueue = rtl ? right : left, tabNext = rtl ? left : right, tabPrev = rtl ? right : left;
  switch (nav.nowZone) {
    case Zone::ZoneButtons:  // os botões de mídia seguem a ordem de leitura de esquerda para direita nos dois idiomas
      if (left && nav.nowSel > 0) nav.nowSel--;
      else if (left && !rtl) enterRail();  // no primeiro botão, ← vai ao trilho
      if (right && nav.nowSel + 1 < Zone::BtnCount) nav.nowSel++;
      if (L.stacked && down && nav.nowSel >= Zone::BtnBack10 && nav.nowSel < Zone::BtnLike) nav.nowSel = std::min(nav.nowSel + 4, (int)Zone::BtnCount - 1);  // 4:3 e texto grande: 2ª fileira
      else if (L.stacked && up && nav.nowSel >= Zone::BtnLike) nav.nowSel = std::max(nav.nowSel - 4, 0);
      else if (up) nav.nowZone = Zone::ZoneSeek;
      if (a) switch (nav.nowSel) {
        case Zone::BtnPrev: previous(); break;
        case Zone::BtnBack10: seekBy(-10); break;
        case Zone::BtnPlay: togglePlay(); break;
        case Zone::BtnForward30: seekBy(30); break;
        case Zone::BtnNext: next(); break;
        case Zone::BtnLike: rate(true); break;
        case Zone::BtnDislike: rate(false); break;
        case Zone::BtnRepeat:
          if (!nav.localQueue) { say(K::MusicQueueServer); break; }
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
      if (left) { if (nav.videoMode) toggleVideo(); else if (rtl) { nav.nowZone = Zone::ZoneTabs; nav.autoFocus = false; } else enterRail(); }
      if (right) { if (!nav.videoMode) toggleVideo(); else if (!rtl) { nav.nowZone = Zone::ZoneTabs; nav.autoFocus = false; } }
      if (down) nav.nowZone = Zone::ZoneSeek;
      break;
    case Zone::ZoneTabs:  // abas A seguir, Letra, Relacionados e, depois delas, a chave de reprodução automática
      if (tabPrev) { if (nav.autoFocus) nav.autoFocus = false; else if (nav.tab > 0) nav.tab--; else nav.nowZone = Zone::ZoneMode; }
      if (tabNext) { if (!nav.autoFocus && nav.tab < 2) nav.tab++; else if (nav.autoFocus && rtl) enterRail(); else nav.autoFocus = true; }
      if (a && nav.autoFocus && !nav.localQueue) say(K::MusicQueueServer);
      else if (a && nav.autoFocus) { nav.autoplay = !nav.autoplay; say(nav.autoplay ? K::MusicAutoplayOn : K::MusicAutoplayOff); }
      if (down) { nav.nowZone = Zone::ZoneQueue; nav.autoFocus = false; }
      break;
    case Zone::ZoneQueue:
      if (nav.tab == 1) {
        int visible = std::max(3, (int)((L.hintY - 12 - (L.listY + 4)) / (ui::large ? 40 : 33)));
        int total = (int)nav.lyricWrap.size();
        if (up && nav.lyricScroll > 0) nav.lyricScroll--; else if (up) nav.nowZone = Zone::ZoneTabs;
        if (down && nav.lyricScroll + visible < total) nav.lyricScroll++;
        if (pageUp) nav.lyricScroll = std::max(0, nav.lyricScroll - visible);
        if (pageDown) nav.lyricScroll = std::max(0, std::min(nav.lyricScroll + visible, total - visible));
        if (fromQueue) nav.nowZone = Zone::ZoneButtons;
        else if (rtl && left) enterRail();
        break;
      }
      if (nav.moving && nav.tab == 0) {
        if (up || down) {
          size_t to = musicqueue::move(nav.queue, nav.queueIndex, (size_t)nav.queueSel, up ? -1 : 1);
          if ((int)to != nav.queueSel) { nav.queueSel = (int)to; nav.queueChanged(); nav.saveOrderBroken(); }
        }
        if (a || menu) nav.moving = false;
        break;
      }
      if (up) { if (sel > 0) sel--; else nav.nowZone = Zone::ZoneTabs; }
      if (down && sel + 1 < count) sel++;
      if (pageUp) sel = std::max(0, sel - R.rows);
      if (pageDown) sel = std::min(count - 1, sel + R.rows);
      if (fromQueue) nav.nowZone = Zone::ZoneButtons;
      else if (rtl && left) enterRail();
      if (a && count) {
        if (nav.tab == 2) {  // relacionada: toca a rádio dela e troca a fila
          nav.queueStart = list[(size_t)sel].id; nav.cancelQueue = false; nav.musicSession = true; nav.queueTitle = std::string(tr(K::MusicMixOf)) + " " + list[(size_t)sel].title;
          nav.action(navWorker, "/api/nav/play?kind=radio&id=" + net::urlEncode(nav.queueStart) + "&lang=" + i18n::code(settings.lang));
        } else if (nav.localQueue) playQueueAt((size_t)sel); else say(K::MusicQueueServer);
      }
      if (nav.tab == 2 && count && ctl::hit(in, ctx, Act::AddQueue) && nav.localQueue) {  // X: relacionada entra no fim da fila (e na ordem original do aleatório)
        nav.queue.push_back(list[(size_t)sel]);
        if (nav.shuffled) nav.queueSaved.push_back(list[(size_t)sel]);
        nav.queueChanged(); say(K::MusicAddedQueue);
      }
      if (menu && count && nav.tab == 0) { if (nav.localQueue) nav.moving = true; else say(K::MusicQueueServer); }
      break;
  }
  // Fim da fila dos botões: o último botão leva à lista (à direita no layout normal, à esquerda no espelhado)
  if (nav.nowZone == Zone::ZoneButtons) {
    if (!rtl && right && nav.nowSel + 1 == Zone::BtnCount && prevSel == Zone::BtnCount - 1 && (count || nav.tab == 1)) nav.nowZone = Zone::ZoneQueue;
    if (rtl && left && nav.nowSel == 0 && prevSel == 0 && (count || nav.tab == 1)) nav.nowZone = Zone::ZoneQueue;
  }
  if (nav.queueSel >= (int)nav.queue.size()) nav.queueSel = std::max(0, (int)nav.queue.size() - 1);
  if (nav.relatedSel >= (int)nav.related.size()) nav.relatedSel = std::max(0, (int)nav.related.size() - 1);

  if (in.hit(Input::B)) {
    if (nav.moving) nav.moving = false;
    else leaveMusic();
  }
}

inline void UiContext::musicNowDraw() {
  nav.rating = likeState; nav.railHasFocus = railFocus;
  nav.drawNow(g, thumbs, thumbsWorker, playing, pl.position(), pl.duration, pl.paused, pl.waiting(),
      pl.state == player::FAILED ? i18n::playerError(pl.error) : K::None, frame, settings.lang);
  const NowLayout L = nowLayout(g.rtlText);
  const auto ctx = ctl::Ctx::Music; using Act = ctl::Act;
  float hx = ui::X0, y = L.hintY;
  hx = ctlHint(g, hx, y, ctx, Act::Open, settings.lang);
  hx = ctlHint(g, hx, y, ctx, Act::Back, settings.lang);
  if (!ui::narrow) hx = ctlHint(g, hx, y, ctx, Act::PrevItem, settings.lang);
  hx = ctlHint(g, hx, y, ctx, Act::SongVideo, settings.lang);
  if (nav.nowZone == MusicNavigation::ZoneQueue && nav.tab == 0 && nav.localQueue) hx = ctlHint(g, hx, y, ctx, Act::Options, settings.lang, K::MusicMoveTrack);
  if (nav.nowZone == MusicNavigation::ZoneButtons || nav.nowZone == MusicNavigation::ZoneSeek) hx = ctlHint(g, hx, y, ctl::Ctx::MusicNow, Act::VolumeDown, settings.lang);  // LT RT: volume
  if (nav.nowZone == MusicNavigation::ZoneQueue && nav.tab == 2 && nav.localQueue) hx = ctlHint(g, hx, y, ctx, Act::AddQueue, settings.lang);
  if (nav.nowZone == MusicNavigation::ZoneSeek) hx = hint(g, hx, y, i18n::get(i18n::Lang::EN, K::BtnHorizontal), XG, tr(K::MusicSeekBar));
  volumeHud(SDL_GetTicks());  // o volume (do controle ou do celular) aparece aqui como no player e nos Shorts
}

inline void UiContext::musicSleepDraw(double sleepLeft) {
  std::string value = sleep.end ? tr(K::SleepEnd) : fmtTime(std::ceil(sleepLeft));
  if (sleep.end && sleepLeft >= 0) value += " · " + fmtTime(std::ceil(sleepLeft));
  std::string label = std::string(tr(K::SleepTimer)) + " · " + value;
  float w = gfx::textWidth(g, ui::F_SMALL, label) + 30;
  // canto de cima do lado da fila, acima das abas (no árabe o seletor Música/Vídeo fica à direita, então o timer vai à esquerda)
  float x = g.rtlText ? nowLayout(true).rightX : ui::W - 48 - w, y = 24;
  gfx::rect(g, x, y, w, 32, ui::SURFACE2, 16);
  gfx::text(g, ui::F_SMALL, label, x + 15, y + 7, ui::MUTED);
}

// Estilo de cada prateleira do início: cartões quadrados, linhas compactas (músicas em linha de lista: "Escolhas rápidas"), cartões largos
// (mixes: "Seus mixes") ou pílulas (chips de clima e botões do Explorar). Decidido pelo conteúdo, não pelo título, para valer em
// qualquer idioma. O desenho e o controle usam esta mesma função.
enum MusicShelfStyle { SHELF_CARDS, SHELF_ROWS, SHELF_WIDE, SHELF_PILLS };
static MusicShelfStyle musicShelfStyle(const NavPage& p, int row) {
  const auto& shelf = p.shelves[(size_t)row];
  if (shelf.first >= shelf.end) return SHELF_CARDS;
  if (p.items[(size_t)shelf.first].pill()) return SHELF_PILLS;
  bool songs = true, mixes = true;
  for (int i = shelf.first; i < shelf.end; i++) {
    const auto& item = p.items[(size_t)i];
    songs = songs && item.type == "video" && item.listStyle;  // "Escolhas rápidas": linhas de lista do YouTube Music; "Ouvir de novo" é carrossel de cartões
    mixes = mixes && item.type == "playlist" && (item.id.rfind("RD", 0) == 0 || item.id.rfind("VLRD", 0) == 0);
  }
  if (songs && shelf.end - shelf.first >= 3) return SHELF_ROWS;
  return mixes ? SHELF_WIDE : SHELF_CARDS;
}

// Prateleiras do início da Música: esquerda/direita andam na prateleira, cima/baixo trocam de prateleira (cima da primeira sobe para as abas).
// Nas linhas compactas (2 linhas por coluna, em ordem de coluna) cima/baixo andam entre as duas linhas e esquerda/direita pulam de coluna.
inline void UiContext::musicHomeMove(NavPage& p, bool left, bool right, bool up, bool down) {
  auto& shelf = p.shelves[p.shelf];
  const bool rows = musicShelfStyle(p, p.shelf) == SHELF_ROWS;
  const int count = shelf.end - shelf.first, rel = shelf.selected - shelf.first, stepX = rows ? 2 : 1;
  if (left && rel - stepX >= 0) shelf.selected -= stepX;
  if (right && rel + stepX < count) shelf.selected += stepX;
  if (rows && up && rel % 2 == 1) { shelf.selected--; up = false; }
  if (rows && down && rel % 2 == 0 && rel + 1 < count) { shelf.selected++; down = false; }
  if (up && p.shelf == 0) { if (p.kind == "music") p.zone = 1; }
  else if (up) p.shelf--;
  if (down && p.shelf + 1 < (int)p.shelves.size()) p.shelf++;
  p.sel = p.shelves[p.shelf].selected;
}

// Troca de aba da Música: Início, Explorar, Biblioteca e Buscar (Paradas e Mixes moram no Explorar e no Início) (ou Tudo, Músicas, Álbuns, Artistas na busca); no início a aba vira a consulta.
inline void UiContext::musicTabSelected(NavPage& p) {
  p.tab = p.tabSel == 1 ? "live" : p.tabSel == 2 ? "playlists" : "videos";
  if (p.kind != "music") return;
  if (p.tabSel == 3) {  // Buscar: abre a busca da Música e a aba continua onde estava
    p.tabSel = p.query == "explore" || p.query == "charts" ? 1 : p.query == "library" ? 2 : 0;
    openSearch();
    return;
  }
  p.query = p.tabSel == 1 ? "explore" : p.tabSel == 2 ? "library" : "";
}

// Teclas das páginas de Música: LB/RB trocam de aba (mapa MusicPage). O X só age no Tocando agora (relacionadas), onde há fila.
inline void UiContext::musicPageKeys(NavPage& p, uint64_t) {
  if (p.kind == "music" || p.kind == "music_search") {
    int step = ctl::hit(in, ctl::Ctx::MusicPage, ctl::Act::NextTab) ? 1 : ctl::hit(in, ctl::Ctx::MusicPage, ctl::Act::PrevTab) ? -1 : 0;
    int maxTab = p.kind == "music" ? 3 : 5, to = std::clamp(p.tabSel + step, 0, maxTab);
    if (step && to != p.tabSel) {
      p.tabSel = to; musicTabSelected(p);
      if (p.kind != "music" || p.tabSel == to) { p.sel = 0; p.scroll = 0; nav.load(navWorker, settings.lang); }
    }
  }
}

// Faixa de abas da Música: se não cabem na largura (busca com 6 filtros em 4:3 e texto grande) ela rola para manter a aba
// escolhida à vista e mostra um triângulo na ponta que tem mais abas.
static void musicTabStrip(NavDraw& d, const K* keys, int count, float x, float tabY, float limit) {
  auto& g = d.g; auto& p = d.p;
  float widths[8]; count = std::min(count, 8);
  for (int i = 0; i < count; i++) widths[i] = gfx::textWidth(g, ui::F_KEY, d.tr(keys[i])) + 40 + 14;
  int first = 0; float used = 0;
  for (int i = 0; i <= std::min(p.tabSel, count - 1); i++) used += widths[i];
  const float room = limit - x - 24;  // 24 px de folga para os triângulos
  while (used > room && first < p.tabSel) { used -= widths[first]; first++; }
  float cx = x;
  int last = first;
  for (int i = first; i < count; i++) {
    if (cx + widths[i] - 14 > limit - (i + 1 < count ? 24 : 0)) break;
    float w = widths[i] - 14;
    bool active = i == p.tabSel, focus = d.contentFocus && active && p.zone == 1;
    gfx::rect(g, cx, tabY, w, 42, focus ? ui::TEXT : active ? ui::SURFACE2 : ui::BG, 23);
    gfx::text(g, ui::F_KEY, d.tr(keys[i]), cx + w / 2, tabY + 11, focus ? ui::INK : ui::TEXT, 1);
    if (active && !focus) gfx::hgrad(g, cx + 20, tabY + 43, w - 40, 3, ui::A1, ui::A2, 1.5f);
    cx += widths[i]; last = i;
  }
  if (first > 0) gfx::triangle(g, x - 6, tabY + 21, x - 0, tabY + 15, x - 0, tabY + 27, ui::MUTED);
  if (last + 1 < count) gfx::triangle(g, limit - 2, tabY + 21, limit - 8, tabY + 15, limit - 8, tabY + 27, ui::MUTED);
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
    const K musicTabs[] = {K::Home,K::MusicExplore,K::Library,K::Search};
    const K songTabs[] = {K::AllResults,K::Songs,K::Albums,K::Artists,K::Videos,K::MusicFeatured};
    int count = p.kind == "music_search" ? 6 : p.kind == "music" ? 4 : channel ? 4 : 3; float x = home && !ui::narrow ? 650 : 136;
    float tabY = home ? (ui::narrow ? 88 : 32) : 166;
    musicTabStrip(d, channel ? channelTabs : p.kind == "music" ? musicTabs : songTabs, count, x, tabY, ui::W - 48);
  }
  float top = 246;
  auto m = navMetrics(d);
  if (home) {
    auto chipShelf = [&](int row) { return !p.items.empty() && p.items[(size_t)p.shelves[(size_t)row].first].pill(); };
    const bool topChips = !p.shelves.empty() && chipShelf(0) && p.items[(size_t)p.shelves[0].first].section == "chips";
    const float card = musicCardSize(), gapX = 24, titleH = g.fonts[ui::F_SMALL].lineH + 2, bylineH = g.fonts[ui::F_TINY].lineH + 2, pillH = ui::large ? 46 : 38;
    const float cardRow = 30 + card + 4 + titleH + bylineH + g.fonts[ui::F_TINY].lineH + 14, pillRow = 30 + pillH + 18, bottom = 646, availW = ui::W - 136 - 48;
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
    if (topChips) { drawPills(0, top0 - 4); top0 += pillH + 30; }
    const int base = topChips ? 1 : 0, count = (int)p.shelves.size();
    const float rowPitch = std::max(60.f, 10 + g.fonts[ui::F_SMALL].lineH + 2 * g.fonts[ui::F_TINY].lineH), wideH = std::max(76.f, 20 + g.fonts[ui::F_SMALL].lineH + 2 * g.fonts[ui::F_TINY].lineH);
    const float rowsRow = 30 + 2 * rowPitch + 14, wideRow = 30 + wideH + 18;
    auto heightOf = [&](int row) { auto st = musicShelfStyle(p, row); return st == SHELF_PILLS ? pillRow : st == SHELF_ROWS ? rowsRow : st == SHELF_WIDE ? wideRow : cardRow; };
    int firstShelf = std::clamp(p.shelf, base, std::max(base, count - 1));
    float used = count > base ? heightOf(firstShelf) : 0;
    while (firstShelf - 1 >= base && used + heightOf(firstShelf - 1) <= bottom - top0) { --firstShelf; used += heightOf(firstShelf); }
    const int columns = std::max(2, (int)((availW + gapX) / (card + gapX)));
    float y = top0;
    for (int row = firstShelf; row < count && y + heightOf(row) <= bottom + 40; row++) {
      const auto& shelf = p.shelves[(size_t)row];
      gfx::text(g, ui::F_KEY, shelf.title, 136, y, ui::TEXT);
      const MusicShelfStyle style = musicShelfStyle(p, row);
      if (style == SHELF_PILLS) { drawPills(row, y + 30); y += pillRow; continue; }
      if (style == SHELF_ROWS) {  // 2 linhas × 3 colunas (2 no 4:3): miniatura 52, título, artista e a linha de membros
        const int cols = ui::narrow ? 2 : 3; const float colW = availW / cols, thumb = 52;
        int rel = shelf.selected - shelf.first, firstCol = std::max(0, rel / 2 - cols + 1);
        for (int c = 0; c < cols; c++) for (int r = 0; r < 2; r++) {
          int i = shelf.first + (firstCol + c) * 2 + r; if (i >= shelf.end) continue;
          const auto& item = p.items[(size_t)i]; float x = 136 + c * colW, yy = y + 30 + r * rowPitch, tx = x + thumb + 12, tw = colW - thumb - 28;
          bool f = d.contentFocus && p.zone == 3 && i == p.sel;
          if (f) {  // moldura branca com fundo escuro por dentro: o texto continua legível em foco
            gfx::rect(g, x - 5, yy - 5, colW - 10, rowPitch - 2, ui::TEXT, 14);
            gfx::rect(g, x - 2, yy - 2, colW - 16, rowPitch - 8, ui::SURFACE2, 12);
          }
          artwork(g, d.thumbs, d.worker, item.image, d.frame, x, yy, thumb, thumb, true);
          float ty = yy + 2;
          gfx::text(g, ui::F_SMALL, gfx::ellipsize(g, ui::F_SMALL, item.title, tw), tx, ty, ui::TEXT); ty += g.fonts[ui::F_SMALL].lineH;
          gfx::text(g, ui::F_TINY, gfx::ellipsize(g, ui::F_TINY, item.channel, tw), tx, ty, ui::MUTED); ty += g.fonts[ui::F_TINY].lineH;
          memberLine(g, item.members, d.lang, tx, ty, tw);
        }
        y += rowsRow; continue;
      }
      if (style == SHELF_WIDE) {  // cartões largos: miniatura 56, título, subtítulo e a linha de membros
        const float w = ui::narrow ? 240 : 250; const int cols = std::max(1, (int)((availW + 18) / (w + 18)));
        int first = std::max(shelf.first, shelf.selected - cols + 1);
        for (int i = first; i < std::min(shelf.end, first + cols); i++) {
          const auto& item = p.items[(size_t)i]; float x = 136 + (i - first) * (w + 18), yy = y + 30, tx = x + 78, tw = w - 56 - 34;
          bool f = d.contentFocus && p.zone == 3 && i == p.sel;
          if (f) gfx::rect(g, x - 4, yy - 4, w + 8, wideH + 8, ui::TEXT, 16);
          gfx::rect(g, x, yy, w, wideH, ui::SURFACE, 12);
          artwork(g, d.thumbs, d.worker, item.image, d.frame, x + 10, yy + 10, 56, 56, true);
          float ty = yy + 8;
          gfx::text(g, ui::F_SMALL, gfx::ellipsize(g, ui::F_SMALL, item.title, tw), tx, ty, ui::TEXT); ty += g.fonts[ui::F_SMALL].lineH;
          gfx::text(g, ui::F_TINY, gfx::ellipsize(g, ui::F_TINY, item.channel, tw), tx, ty, ui::MUTED); ty += g.fonts[ui::F_TINY].lineH;
          memberLine(g, item.members, d.lang, tx, ty, tw);
        }
        y += wideRow; continue;
      }
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
