// Página de canal: avatar, nome com verificado, @handle · inscritos · vídeos, descrição, botão Inscrever-se, as abas
// Início/Vídeos/Shorts/Ao vivo/Playlists e a ordenação (Mais recentes/Populares/Mais antigos) nas abas Vídeos e Ao vivo.
// A entrada genérica das páginas de navegação está em ui_collection.hpp; aqui ficam os ganchos do canal.
#pragma once
#include "ui_context.hpp"

// A no botão do cabeçalho: inscrever (pede conta) ou, já inscrito, pedir a confirmação para cancelar.
inline void UiContext::channelHeaderPress(NavPage& p, uint64_t now) {
  if (!p.subscribedKnown) return;  // sem o estado real da inscrição (página pública) não se assina nem cancela
  if (!account.signedIn()) { toast = tr(K::AccountRequired); toastUntil = now + 4000; }
  else if (p.subscribed) nav.unsubscribe = true;
  else nav.action(navWorker, "/api/nav/action?action=subscribe&id=" + p.channelId + "&value=1&lang=" + i18n::code(settings.lang));
}

// Troca de aba do canal: Início, Vídeos, Shorts, Ao vivo, Playlists. A ordenação volta para "Mais recentes".
inline void UiContext::channelTabSelected(NavPage& p) {
  p.tab = p.tabSel==1 ? "videos" : p.tabSel==2 ? "shorts" : p.tabSel==3 ? "live" : p.tabSel==4 ? "playlists" : "home";
  p.sort = 0;
}

// LB/RB trocam a ordenação nas abas que a têm (Vídeos e Ao vivo): a lista recarrega no chip escolhido.
inline void UiContext::channelSortInput(NavPage& p) {
  if (p.kind != "channel" || !p.sortable || (p.tab != "videos" && p.tab != "live") || p.loading || nav.busy) return;
  int step = ctl::hit(in, ctl::Ctx::Channel, ctl::Act::NextTab) ? 1 : ctl::hit(in, ctl::Ctx::Channel, ctl::Act::PrevTab) ? 2 : 0;
  if (!step) return;
  p.sort = (p.sort + step) % 3; p.sel = 0; p.scroll = 0;
  nav.load(navWorker, settings.lang);
}

inline float Navigation::drawChannelPage(NavDraw& d) {
  auto& g = d.g; auto& p = d.p;
  float tx = 136;
  if (!p.image.empty()) { artwork(g, d.thumbs, d.worker, p.image, d.frame, tx, 28, 72, 72, true, true); tx += 96; }
  const float tw = ui::W-tx-364;
  std::string name = p.displayTitle.empty() ? d.tr(titleKey(p)) : p.displayTitle;
  float markW = p.verified ? 34.f : 0;
  std::string shown = gfx::ellipsize(g, ui::F_BIG, name, tw - markW);
  gfx::textBox(g, ui::F_BIG, shown, tx, 24, tw, ui::TEXT);
  if (p.verified) {
    float nameW = gfx::textWidth(g, ui::F_BIG, shown);
    bool rtl = g.rtlText || gfx::visualLine(g, shown, ui::F_BIG).rtl;
    verifiedMark(g, rtl ? tx + tw - nameW - 12 - 22 : tx + nameW + 12, 24 + (g.fonts[ui::F_BIG].lineH - 22) / 2, 22, ui::MUTED, ui::BG);
  }
  gfx::text(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, p.displaySub, tw), tx, 78, ui::MUTED);
  if (!p.description.empty()) gfx::text(g, ui::F_SMALL, gfx::ellipsize(g, ui::F_SMALL, p.description, tw), tx, 78 + g.fonts[ui::F_CHAN].lineH + 4, rgb(0xa1a1aa));
  bool f = d.contentFocus && p.zone == 0;
  if (p.subscribedKnown) {
    gfx::rect(g, ui::W-316, 32, 252, 54, f ? ui::TEXT : ui::SURFACE2, 27);
    gfx::text(g, ui::F_KEY, d.tr(p.subscribed ? K::Subscribed : K::Subscribe), ui::W-190, 48, f ? ui::INK : ui::TEXT, 1);
  }
  const K tabs[] = {K::ChannelHome,K::Videos,K::ChannelShorts,K::ChannelLive,K::Playlists};
  // As abas ficam abaixo da descrição, que cresce com o texto grande.
  const float tabY = std::max(148.f, 78 + g.fonts[ui::F_CHAN].lineH + 4 + g.fonts[ui::F_SMALL].lineH + 10);
  navTabs(d, tabs, 5, 136, tabY);
  float top = tabY + 58;
  if (p.sortable && (p.tab == "videos" || p.tab == "live")) {
    chipRow(d, {d.tr(K::SortRecent), d.tr(K::SortPopular), d.tr(K::SortOldest)}, p.sort, -1, 136, top - 4);
    top += 48;
  }
  navGrid(d, top, navMetrics(d));
  return top;
}
