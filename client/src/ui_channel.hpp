// Página de canal: avatar, nome, botão Inscrever-se e as abas Vídeos/Shorts/Ao vivo/Playlists, sobre a grade do canal.
// A entrada genérica das páginas de navegação está em ui_collection.hpp; aqui ficam os ganchos do canal.
#pragma once
#include "ui_context.hpp"

// A no botão do cabeçalho: inscrever (pede conta) ou, já inscrito, pedir a confirmação para cancelar.
inline void UiContext::channelHeaderPress(NavPage& p, uint64_t now) {
  if (!account.signedIn()) { toast = tr(K::AccountRequired); toastUntil = now + 4000; }
  else if (p.subscribed) nav.unsubscribe = true;
  else nav.action(navWorker, "/api/nav/action?action=subscribe&id=" + p.channelId + "&value=1&lang=" + i18n::code(settings.lang));
}

// Troca de aba do canal: Vídeos, Shorts, Ao vivo, Playlists.
inline void UiContext::channelTabSelected(NavPage& p) {
  p.tab = p.tabSel==1 ? "shorts" : p.tabSel==2 ? "live" : p.tabSel==3 ? "playlists" : "videos";
}
// Página de canal: avatar, nome, botão Inscrever-se e as abas Vídeos/Shorts/Ao vivo/Playlists, sobre a grade do canal.
inline float Navigation::drawChannelPage(NavDraw& d) {
  auto& g = d.g; auto& p = d.p;
  float tx = 136;
  if (!p.image.empty()) { artwork(g, d.thumbs, d.worker, p.image, d.frame, tx, 28, 72, 72, true, true); tx += 96; }
  gfx::textBox(g, ui::F_BIG, p.displayTitle.empty() ? d.tr(titleKey(p)) : p.displayTitle, tx, 28, ui::W-tx-64, ui::TEXT);
  gfx::text(g, ui::F_CHAN, p.displaySub, tx, 82, ui::MUTED);
  bool f = d.contentFocus && p.zone == 0;
  gfx::rect(g, ui::W-316, 32, 252, 54, f ? ui::TEXT : ui::SURFACE2, 27);
  gfx::text(g, ui::F_KEY, d.tr(p.subscribed ? K::Subscribed : K::Subscribe), ui::W-190, 48, f ? ui::INK : ui::TEXT, 1);
  const K tabs[] = {K::Videos,K::ChannelShorts,K::ChannelLive,K::Playlists};
  navTabs(d, tabs, 4, 136, 120);
  float top = 178;
  navGrid(d, top, navMetrics(d));
  return top;
}
