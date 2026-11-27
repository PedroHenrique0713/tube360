// Busca: o teclado na tela, as sugestões e o painel de filtros (a lógica e o desenho moram em search_ui.hpp).
// Os resultados da busca são uma página de navegação: o desenho (Navigation::drawSearchPage) fica aqui também.
#pragma once
#include "ui_context.hpp"

inline void UiContext::searchInput(const char* const* keys) {
  if (settings.lang==i18n::Lang::AR && ctl::hit(in, ctl::Ctx::Search, ctl::Act::NextTab)) latinKeyboard=!latinKeyboard;
  if (!searchMusic && ctl::hit(in, ctl::Ctx::Search, ctl::Act::PrevTab)) searchUI.beginFilters(searchUI.filters);
  if (!searchUI.listFocus && searchUI.keyCol==0 && in.fired[Input::LEFT]) { in.hit(Input::LEFT); railFocus=true; railSel=1; }
  auto chosen=searchUI.keyboardInput(in,query,keys);
  if (!chosen.empty()) doSearch(chosen);
  if (in.hit(Input::B)) screen=searchBack;
}

inline void UiContext::searchDraw(const char* const* keys, double t) {
  searchUI.draw(g,query,keys,settings.lang,searchMusic,railFocus,t);
}

// Nos resultados, LB/RB trocam o chip de tipo (Tudo, Vídeos, Canais, Playlists, Ao vivo); a pílula "Filtros" na fileira de
// chips (A) abre o painel. O chip marcado acompanha o tipo do filtro, também quando ele muda pelo painel.
inline void UiContext::searchFiltersPress(NavPage& p) {
  if (p.zone != 1) p.tabSel = std::max(0, p.searchChip());  // andar pelos chips só move o foco; buscar é com A (ou LB/RB)
  if (p.loading || nav.busy) return;
  int step = ctl::hit(in, ctl::Ctx::Search, ctl::Act::NextTab) ? 1 : ctl::hit(in, ctl::Ctx::Search, ctl::Act::PrevTab) ? 4 : 0;
  if (!step) return;
  p.tabSel = (std::max(0, p.searchChip()) + step) % 5;
  searchChipSelected(p);
}

// Chip escolhido (0 Tudo, 1 Vídeos, 2 Shorts, 3 Canais, 4 Ao vivo). Shorts é filtro no cliente (mostra só os Shorts dos
// resultados, sem nova busca); os outros trocam o tipo do filtro e refazem a busca.
inline void UiContext::searchChipSelected(NavPage& p) {
  const int chip = std::clamp(p.tabSel, 0, 4), types[] = {0, 1, 0, 2, 4};
  const bool shorts = chip == 2, sameType = p.filters.type == types[chip];
  p.onlyShorts = shorts; p.sel = 0; p.scroll = 0; p.gridSize = -1;
  if (sameType) return;
  p.filters.type = types[chip]; p.filters.normalize();
  searchUI.filters = p.filters;
  p.filterLabel.clear(); nav.load(navWorker, settings.lang);
}

// Nome + marca de verificado na mesma linha, numa linha só (a marca segue o fim do texto, no lado certo em árabe).
// Devolve o y logo abaixo.
static float nameWithBadge(gfx::Gfx& g, int font, const std::string& name, bool verified, float x, float y, float w, Color c) {
  float mark = verified ? 14.f + 8 : 0;
  std::string shown = gfx::ellipsize(g, font, name, w - mark);
  gfx::textBox(g, font, shown, x, y, w, c);
  if (verified) {
    float tw = gfx::textWidth(g, font, shown);
    bool rtl = g.rtlText || gfx::visualLine(g, shown, font).rtl;
    verifiedMark(g, rtl ? x + w - tw - 8 - 14 : x + tw + 8, y + (g.fonts[font].lineH - 14) / 2, 14, ui::MUTED, ui::BG);
  }
  return y + g.fonts[font].lineH;
}

// Fileira de chips: a ativa em branco, as outras no cinza; `focus` = índice em foco na fileira (ou -1).
// Em árabe a fileira espelha: o 1º chip fica no canto direito e as seguintes vão para a esquerda; `x` é o canto de partida
// (esquerdo em LTR, direito em RTL) e o retorno é onde a fileira terminou.
static float chipRow(NavDraw& d, const std::vector<std::string>& labels, int active, int focus, float x, float y) {
  auto& g = d.g;
  const bool rtl = g.rtlText;
  for (size_t i = 0; i < labels.size(); i++) {
    float w = gfx::textWidth(g, ui::F_SMALL, labels[i]) + 32;
    if (rtl) x -= w;
    bool on = int(i) == active, f = int(i) == focus;
    if (f) gfx::rect(g, x - 3, y - 3, w + 6, 42, ui::A1, 21);  // anel de foco
    gfx::rect(g, x, y, w, 36, on ? ui::TEXT : ui::SURFACE2, 18);  // chip ativo em branco, como na proposta
    gfx::text(g, ui::F_SMALL, labels[i], x + w / 2, y + (36 - g.fonts[ui::F_SMALL].size) / 2, on ? ui::INK : ui::TEXT, 1);
    x = rtl ? x - 10 : x + w + 10;
  }
  return x;
}

// Resultado de canal em destaque: cartão largo com avatar, nome + verificado, @handle · inscritos · vídeos, descrição e o estado
// da inscrição. A abre o canal (zona 2).
static void featuredChannel(NavDraw& d, const NavItem& it, bool focused, float y) {
  auto& g = d.g; auto& p = d.p;
  const float x = 136, w = ui::W - 136 - 64, h = 104;
  if (focused) gfx::rect(g, x - 4, y - 4, w + 8, h + 8, ui::TEXT, 20);
  gfx::rect(g, x, y, w, h, ui::SURFACE, 16);
  const bool rtl = g.rtlText;  // árabe: avatar à direita, texto à esquerda dele
  artwork(g, d.thumbs, d.worker, it.image, d.frame, rtl ? x + w - 98 : x + 22, y + 14, 76, 76, true, true, p.direct, false, 646);
  const float tx = rtl ? x + 44 : x + 122, tw = w - 122 - 44;
  float ty = nameWithBadge(g, ui::F_KEY, it.title, it.verified, tx, y + 12, tw, ui::TEXT) + 4;
  std::string meta = it.date;
  if (!it.views.empty()) meta += (meta.empty() ? "" : "  \xC2\xB7  ") + it.views;
  if (!meta.empty()) { gfx::textBox(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, meta, tw), tx, ty, tw, ui::MUTED); ty += g.fonts[ui::F_CHAN].lineH + 2; }
  if (!it.description.empty()) gfx::textBox(g, ui::F_SMALL, gfx::ellipsize(g, ui::F_SMALL, it.description, tw), tx, ty, tw, rgb(0xa1a1aa));
}

// Cartão da grade de busca: vídeo e playlist com o cartão da F61 (canal + verificado, visualizações · data, "Só para membros");
// canal avulso e Shorts seguem o cartão genérico.
static void searchCard(NavDraw& d, const NavMetrics& m, const NavItem& it, bool shorts, bool focused, float x, float y, float artW, float artH) {
  if (shorts || (it.type != "video" && it.type != "playlist")) { navCard(d, m, it, shorts, focused, x, y, artW, artH); return; }
  CardMeta meta; meta.title = it.title; meta.channel = it.channel; meta.views = it.views; meta.date = it.date;
  meta.duration = it.type == "video" ? it.duration : -1; meta.members = it.members; meta.verified = it.verified;
  videoCard(d.g, d.thumbs, d.worker, d.frame, d.lang, it.image, meta, d.p.direct, false, focused, x, y, artW, artH, focused ? 1.f : 0.f);
}

// Cabeçalho da busca: título, chips de tipo e a pílula "Filtros" (o resumo dos filtros ativos vai ao lado).
static void drawSearchHeader(NavDraw& d, const std::string& title) {
  auto& g = d.g; auto& p = d.p;
  float tx = 136;
  if (!p.image.empty()) { artwork(g, d.thumbs, d.worker, p.image, d.frame, tx, 28, 72, 72, true, false); tx += 96; }
  gfx::textBox(g, ui::F_BIG, title, tx, 24, ui::W-tx-64, ui::TEXT);
  const float chipsY = 84;
  const std::vector<std::string> types = {d.tr(K::All), d.tr(K::Videos), d.tr(K::ChannelShorts), d.tr(K::SearchTypeChannels), d.tr(K::ChannelLive)};
  const bool rtl = g.rtlText, onRow = d.contentFocus && p.zone == 1;
  const float start = rtl ? ui::W - 64 : 136;
  float cx = chipRow(d, types, p.searchChip(), onRow && p.tabSel < 5 ? p.tabSel : -1, start, chipsY);
  float endX = chipRow(d, {d.tr(K::SearchFilters)}, -1, onRow && p.tabSel == 5 ? 0 : -1, rtl ? cx - 10 : cx + 10, chipsY);
  for (size_t row = 0; row < p.filterLines.size(); ++row) {
    if (rtl) gfx::textBox(g, ui::F_CHAN, p.filterLines[row], 136, chipsY + 6 + row * 26, std::max(80.f, endX - 150), ui::A2);
    else gfx::textBox(g, ui::F_CHAN, p.filterLines[row], endX + 16, chipsY + 6 + row * 26, ui::W - endX - 80, ui::A2);
  }
}

// Página de resultados da busca (proposta A2): resultado de canal em destaque, grade de cartões ricos e os Shorts numa faixa
// própria. O cartão em destaque faz parte do conteúdo que rola; o cabeçalho (título e chips) é desenhado por cima, sobre uma
// máscara, para o cartão nunca cobri-lo.
inline float Navigation::drawSearchPage(NavDraw& d) {
  auto& g = d.g; auto& p = d.p;
  const float top = 140;
  const bool feat = !p.channels.empty();
  const float cardH = feat ? 104 + 16 : 0;
  const float before = p.scroll;
  auto m = navMetrics(d);  // monta as fileiras e dá o passo de rolagem sem o cartão em destaque; refaço o passo com ele
  float want = 0;
  if (p.zone == 3 && p.gridRow() > 0) { want = cardH; for (int row = 0; row < p.gridRow(); ++row) want += navRowHeight(m, p.rows[row]); }
  p.scroll = before + (want - before) * 0.24f;
  float rowY = top + cardH - p.scroll;
  if (feat && cardH - p.scroll > 0) featuredChannel(d, p.channels[(size_t)std::min(p.channelSel, (int)p.channels.size()-1)], d.contentFocus && p.zone == 2, top - p.scroll);
  for (const auto& row : p.rows) {
    float y = rowY, artW = row.shorts ? m.shortW : m.art, artH = row.shorts ? m.shortH : m.h;
    rowY += navRowHeight(m, row);
    if (y >= 646 && y < 646 + 2 * m.rowH && !row.shorts)
      for (int i : row.items) if (!p.items[i].image.empty() && p.items[i].image.rfind("local:",0)) d.thumbs.get(d.worker, p.items[i].image, d.frame, p.direct);
    if (y < top - 2 || y >= 646) continue;
    int first = 0, visible = (int)row.items.size();
    if (row.shorts) {
      if (y + g.fonts[ui::F_KEY].lineH <= 646) gfx::text(g, ui::F_KEY, d.tr(K::ChannelShorts), 136, y, ui::TEXT);
      y += m.shortHeading;
      visible = std::max(1, (int)((ui::W-200+24)/(m.shortW+24)));
      int selected = p.gridRow() >= 0 && p.rows[p.gridRow()].shorts ? p.gridPosition() : p.shortColumn;
      first = std::clamp(selected-visible+1, 0, std::max(0, (int)row.items.size()-visible));
    }
    for (int col = first; col < std::min((int)row.items.size(), first+visible); ++col) {
      int i = row.items[col];
      float x = 136 + (col-first) * ((row.shorts ? m.shortW : m.cw) + 24);
      searchCard(d, m, p.items[i], row.shorts, d.contentFocus && p.zone == 3 && i == p.sel, x, y, artW, artH);
    }
  }
  gfx::rect(g, 96, 0, ui::W - 96, top, ui::BG);
  drawSearchHeader(d, p.displayTitle.empty() ? d.tr(titleKey(p)) : p.displayTitle);
  return top;
}
