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

// Nos resultados, LB abre o painel de filtros.
inline void UiContext::searchFiltersPress(NavPage& p) {
  if (in.hit(Input::LB)) searchUI.beginFilters(p.filters);
}

// Marca de verificado (círculo com o visto) ao lado do nome do canal; d = diâmetro.
static void verifiedBadge(gfx::Gfx& g, float x, float y, float d, Color c, Color ink) {
  gfx::rect(g, x, y, d, d, c, d / 2);
  float k = d / 14;
  gfx::triangle(g, x + 3.2f*k, y + 7.4f*k, x + 4.6f*k, y + 6.2f*k, x + 6.2f*k, y + 9.2f*k, ink);
  gfx::triangle(g, x + 5.6f*k, y + 9.9f*k, x + 6.8f*k, y + 8.9f*k, x + 11.2f*k, y + 4.4f*k, ink);
  gfx::triangle(g, x + 11.2f*k, y + 4.4f*k, x + 12.2f*k, y + 5.4f*k, x + 6.8f*k, y + 8.9f*k, ink);
}

// Nome + marca de verificado na mesma linha; devolve o y logo abaixo.
static float nameWithBadge(gfx::Gfx& g, int font, const std::string& name, bool verified, float x, float y, float w, Color c) {
  float mark = verified ? 14.f + 8 : 0;
  std::string shown = gfx::ellipsize(g, font, name, w - mark);
  gfx::textBox(g, font, shown, x, y, w, c);
  if (verified) verifiedBadge(g, x + gfx::textWidth(g, font, shown) + 8, y + (g.fonts[font].lineH - 14) / 2, 14, ui::MUTED, ui::BG);
  return y + g.fonts[font].lineH;
}

// Resultado rico: miniatura (ou avatar redondo no resultado de canal) à esquerda; título, visualizações · data, canal com
// verificado e um trecho da descrição à direita. (x,y) = canto do resultado; rowH = altura da fileira.
static void searchResult(NavDraw& d, const NavItem& it, bool focused, float y, float rowH) {
  auto& g = d.g; auto& p = d.p;
  const float x = 148, artW = 256, artH = 144;
  bool channel = it.type == "channel";
  if (focused) gfx::rect(g, 130, y - 8, ui::W - 130 - 58, rowH - 4, ui::SURFACE, 16);
  if (channel) artwork(g, d.thumbs, d.worker, it.image, d.frame, x + (artW - 120) / 2, y + (artH - 120) / 2, 120, 120, true, true, p.direct, false, 646);
  else {
    artwork(g, d.thumbs, d.worker, it.image, d.frame, x, y, artW, artH, false, false, p.direct, false, 646);
    if (it.type == "video" && it.duration >= 0 && y + artH <= 646) {
      std::string t = it.duration ? fmtTime(it.duration) : d.tr(K::Live);
      float dw = gfx::textWidth(g, ui::F_TINY, t) + 12, bh = ui::large ? 36 : 20, by = y + artH - bh - 7;
      gfx::rect(g, x + artW - dw - 7, by, dw, bh, it.duration ? Color{0,0,0,.8f} : ui::A1, 5);
      gfx::text(g, ui::F_TINY, t, x + artW - dw / 2 - 7, by + (bh - g.fonts[ui::F_TINY].size) / 2, rgb(0xffffff), 1);
    }
  }
  const float tx = x + artW + 24, tw = ui::W - tx - 64;
  float ty = y;
  auto room = [&](int font) { return ty + g.fonts[font].lineH <= 646; };
  for (const auto& line : gfx::wrap(g, ui::F_KEY, it.title, tw - (channel && it.verified ? 22 : 0), 2)) {
    if (!room(ui::F_KEY)) break;
    if (channel && it.verified) ty = nameWithBadge(g, ui::F_KEY, line, true, tx, ty, tw, ui::TEXT);
    else { gfx::textBox(g, ui::F_KEY, line, tx, ty, tw, ui::TEXT); ty += g.fonts[ui::F_KEY].lineH + 2; }
  }
  ty += 4;
  std::string meta = channel ? it.date : it.views;
  auto join = [](std::string a, const std::string& b) { return b.empty() ? a : a + (a.empty() ? "" : "  \xC2\xB7  ") + b; };
  meta = channel ? join(it.date, it.views) : join(it.views, it.date);
  if (!meta.empty() && room(ui::F_CHAN)) { gfx::textBox(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, meta, tw), tx, ty, tw, ui::MUTED); ty += g.fonts[ui::F_CHAN].lineH + 4; }
  if (!channel && !it.channel.empty() && room(ui::F_CHAN)) ty = nameWithBadge(g, ui::F_CHAN, it.channel, it.verified, tx, ty, tw, ui::MUTED) + 4;
  if (!it.description.empty() && room(ui::F_SMALL))
    for (const auto& line : gfx::wrap(g, ui::F_SMALL, it.description, tw, 2)) {
      if (!room(ui::F_SMALL)) break;
      gfx::textBox(g, ui::F_SMALL, line, tx, ty, tw, rgb(0xa1a1aa)); ty += g.fonts[ui::F_SMALL].lineH;
    }
}

// Página de resultados da busca de vídeos: título, resumo dos filtros ativos (com a dica do LB) e a lista de resultados ricos.
// Os Shorts continuam numa fileira horizontal própria, como nas outras grades.
inline float Navigation::drawSearchPage(NavDraw& d) {
  auto& g = d.g; auto& p = d.p;
  float tx = 136;
  if (!p.image.empty()) { artwork(g, d.thumbs, d.worker, p.image, d.frame, tx, 28, 72, 72, true, false); tx += 96; }
  gfx::textBox(g, ui::F_BIG, p.displayTitle.empty() ? d.tr(titleKey(p)) : p.displayTitle, tx, 28, ui::W-tx-64, ui::TEXT);
  gfx::text(g, ui::F_CHAN, p.displaySub, tx, 82, ui::MUTED);
  float hx=hint(g,136,126,"LB",XG,d.tr(K::SearchFilters));
  for (size_t row=0;row<p.filterLines.size();++row) gfx::textBox(g,ui::F_CHAN,p.filterLines[row],hx,122+row*30,ui::W-hx-64,ui::A2);
  float top = 210;
  p.grid(1, d.hideShorts);
  NavMetrics m{}; m.cols = 1; m.cw = std::floor((ui::W-200)/4.f); m.shortW = std::min(m.cw*.5f,112.f); m.shortH = m.shortW*16/9;
  m.shortHeading = std::max(30.f,g.fonts[ui::F_KEY].lineH+8); m.shortRowH = m.shortHeading+m.shortH+(ui::large ? 124 : 100);
  const float rowH = ui::large ? 196 : 168;
  float want = 0;
  if (p.zone == 3) for (int row=0; row<p.gridRow(); ++row) want += p.rows[row].shorts ? m.shortRowH : rowH;
  p.scroll += (want - p.scroll) * 0.24f;
  float rowY = top - p.scroll;
  for (const auto& row : p.rows) {
    float y = rowY; rowY += row.shorts ? m.shortRowH : rowH;
    if (y < top-2 || y >= 646) continue;
    if (!row.shorts) { int i = row.items[0]; searchResult(d, p.items[i], d.contentFocus && p.zone == 3 && i == p.sel, y, rowH); continue; }
    if (y+g.fonts[ui::F_KEY].lineH <= 646) gfx::text(g,ui::F_KEY,d.tr(K::ChannelShorts),136,y,ui::TEXT);
    y += m.shortHeading;
    int visible = std::max(1,(int)((ui::W-200+24)/(m.shortW+24)));
    int selected = p.gridRow()>=0 && p.rows[p.gridRow()].shorts ? p.gridPosition() : p.shortColumn;
    int first = std::clamp(selected-visible+1,0,std::max(0,(int)row.items.size()-visible));
    for (int col=first; col<std::min((int)row.items.size(),first+visible); ++col) {
      int i = row.items[col];
      navCard(d, m, p.items[i], true, d.contentFocus && p.zone == 3 && i == p.sel, 136 + (col-first)*(m.shortW+24), y, m.shortW, m.shortH);
    }
  }
  return top;
}
