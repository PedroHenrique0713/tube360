// Busca: o teclado na tela, as sugestões e o painel de filtros (a lógica e o desenho moram em search_ui.hpp).
// Os resultados da busca são uma página de navegação (ui_channel.hpp).
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
