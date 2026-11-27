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

// Página de resultados da busca de vídeos: título, resumo dos filtros ativos (com a dica do LB) e a grade.
inline float Navigation::drawSearchPage(NavDraw& d) {
  auto& g = d.g; auto& p = d.p;
  float tx = 136;
  if (!p.image.empty()) { artwork(g, d.thumbs, d.worker, p.image, d.frame, tx, 28, 72, 72, true, false); tx += 96; }
  gfx::textBox(g, ui::F_BIG, p.displayTitle.empty() ? d.tr(titleKey(p)) : p.displayTitle, tx, 28, ui::W-tx-64, ui::TEXT);
  gfx::text(g, ui::F_CHAN, p.displaySub, tx, 82, ui::MUTED);
  float hx=hint(g,136,126,"LB",XG,d.tr(K::SearchFilters));
  for (size_t row=0;row<p.filterLines.size();++row) gfx::textBox(g,ui::F_CHAN,p.filterLines[row],hx,122+row*30,ui::W-hx-64,ui::A2);
  float top = 210;
  navGrid(d, top, navMetrics(d));
  return top;
}
