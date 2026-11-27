// Busca: o teclado na tela, as sugestões e o painel de filtros (a lógica e o desenho moram em search_ui.hpp).
// Os resultados da busca são uma página de navegação: o desenho (Navigation::drawSearchPage) fica aqui também.
#pragma once
#include "ui_context.hpp"

inline void UiContext::searchInput(const char* const* keys) {
  if (settings.lang==i18n::Lang::AR && in.hit(Input::RB)) latinKeyboard=!latinKeyboard;
  if (!searchMusic && in.hit(Input::LB)) searchUI.beginFilters(searchUI.filters);
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
  if (!(p.zone == 1 && p.tabSel == 5)) p.tabSel = p.filters.type;
  if (p.loading || nav.busy) return;
  int step = ctl::hit(in, ctl::Ctx::Search, ctl::Act::NextTab) ? 1 : ctl::hit(in, ctl::Ctx::Search, ctl::Act::PrevTab) ? 4 : 0;
  if (!step) return;
  p.tabSel = (p.filters.type + step) % 5;
  searchChipSelected(p);
}

// Chip de tipo escolhido: troca o tipo do filtro e refaz a busca.
inline void UiContext::searchChipSelected(NavPage& p) {
  p.filters.type = std::clamp(p.tabSel, 0, 4); p.filters.normalize();
  searchUI.filters = p.filters;
  p.filterLabel.clear(); p.sel = 0; p.scroll = 0; nav.load(navWorker, settings.lang);
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
static float chipRow(NavDraw& d, const std::vector<std::string>& labels, int active, int focus, float x, float y) {
  auto& g = d.g;
  for (size_t i = 0; i < labels.size(); i++) {
    float w = gfx::textWidth(g, ui::F_SMALL, labels[i]) + 32;
    bool on = int(i) == active, f = int(i) == focus;
    gfx::rect(g, x, y, w, 36, f ? ui::TEXT : on ? ui::SURFACE2 : ui::SURFACE, 18);
    if (on && !f) gfx::hgrad(g, x + 16, y + 34, w - 32, 2, ui::A1, ui::A2, 1);
    gfx::text(g, ui::F_SMALL, labels[i], x + w / 2, y + (36 - g.fonts[ui::F_SMALL].size) / 2, f ? ui::INK : ui::TEXT, 1);
    x += w + 10;
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
  artwork(g, d.thumbs, d.worker, it.image, d.frame, x + 22, y + 14, 76, 76, true, true, p.direct, false, 646);
  const float tx = x + 122, btnW = 190, tw = w - 122 - btnW - 44;
  float ty = nameWithBadge(g, ui::F_KEY, it.title, it.verified, tx, y + 12, tw, ui::TEXT) + 4;
  std::string meta = it.date;
  if (!it.views.empty()) meta += (meta.empty() ? "" : "  \xC2\xB7  ") + it.views;
  if (!meta.empty()) { gfx::textBox(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, meta, tw), tx, ty, tw, ui::MUTED); ty += g.fonts[ui::F_CHAN].lineH + 2; }
  if (!it.description.empty()) gfx::textBox(g, ui::F_SMALL, gfx::ellipsize(g, ui::F_SMALL, it.description, tw), tx, ty, tw, rgb(0xa1a1aa));
  if (it.subscribed >= 0) {
    gfx::rect(g, x + w - btnW - 22, y + (h - 46) / 2, btnW, 46, ui::SURFACE2, 23);
    gfx::text(g, ui::F_KEY, d.tr(it.subscribed ? K::Subscribed : K::Subscribe), x + w - btnW / 2 - 22, y + (h - 46) / 2 + 9, ui::TEXT, 1);
  }
}

// Cartão da grade de busca: vídeo e playlist com o cartão da F61 (canal + verificado, visualizações · data, "Só para membros");
// canal avulso e Shorts seguem o cartão genérico.
static void searchCard(NavDraw& d, const NavMetrics& m, const NavItem& it, bool shorts, bool focused, float x, float y, float artW, float artH) {
  if (shorts || (it.type != "video" && it.type != "playlist")) { navCard(d, m, it, shorts, focused, x, y, artW, artH); return; }
  CardMeta meta; meta.title = it.title; meta.channel = it.channel; meta.views = it.views; meta.date = it.date;
  meta.duration = it.type == "video" ? it.duration : -1; meta.members = it.members; meta.verified = it.verified;
  videoCard(d.g, d.thumbs, d.worker, d.frame, d.lang, it.image, meta, d.p.direct, false, focused, x, y, artW, artH, focused ? 1.f : 0.f);
}

// Página de resultados da busca (proposta A2): título, chips de tipo + "Filtros", resultado de canal em destaque, grade de
// cartões ricos e os Shorts numa faixa própria.
inline float Navigation::drawSearchPage(NavDraw& d) {
  auto& g = d.g; auto& p = d.p;
  float tx = 136;
  if (!p.image.empty()) { artwork(g, d.thumbs, d.worker, p.image, d.frame, tx, 28, 72, 72, true, false); tx += 96; }
  gfx::textBox(g, ui::F_BIG, p.displayTitle.empty() ? d.tr(titleKey(p)) : p.displayTitle, tx, 24, ui::W-tx-64, ui::TEXT);
  const float chipsY = 84;
  const std::vector<std::string> types = {d.tr(K::All), d.tr(K::Videos), d.tr(K::SearchTypeChannels), d.tr(K::Playlists), d.tr(K::ChannelLive)};
  bool onRow = d.contentFocus && p.zone == 1;
  float cx = chipRow(d, types, p.filters.type, onRow && p.tabSel < 5 ? p.tabSel : -1, 136, chipsY);
  std::vector<std::string> filtersPill = {d.tr(K::SearchFilters)};
  float endX = chipRow(d, filtersPill, -1, onRow && p.tabSel == 5 ? 0 : -1, cx + 10, chipsY);
  for (size_t row = 0; row < p.filterLines.size(); ++row) gfx::textBox(g, ui::F_CHAN, p.filterLines[row], endX + 16, chipsY + 6 + row * 26, ui::W - endX - 80, ui::A2);
  const float top = 140;
  auto m = navMetrics(d);
  const bool feat = !p.channels.empty();
  const float cardH = 104 + 16;
  const float f = std::min(1.f, p.scroll / std::max(1.f, m.rowH));
  if (feat && f < 1) featuredChannel(d, p.channels[(size_t)std::min(p.channelSel, (int)p.channels.size()-1)], d.contentFocus && p.zone == 2, top - f * cardH);
  float rowY = top + (feat ? cardH * (1 - f) : 0) - p.scroll;
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
  return top;
}
