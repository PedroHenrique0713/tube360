// Páginas de coleção: Inscrições, Biblioteca, Histórico, Assistir mais tarde, playlists e as listas locais. O desenho de cada
// uma (cabeçalho, faixa de canais, grade) e a entrada genérica das páginas de navegação (zonas, paginação, menu, voltar).
#pragma once
#include "ui_context.hpp"

// Entrada de qualquer página de navegação: zonas (cabeçalho, abas, faixa de canais, grade), paginação, menu, busca e voltar.
// O que depende do tipo da página está nos ganchos de cada dono: ui_channel, ui_music, ui_search e aqui (coleção).
inline void UiContext::navInput(uint64_t now) {
  auto& p = nav.page(); uint64_t currentPage = p.stamp;
  bool channel = p.kind == "channel";
  bool playlist = Navigation::playlistKind(p);
  bool search = p.kind == "search";
  bool tabs = channel || search || p.kind == "music" || p.kind == "music_search";
  if (p.kind=="search") searchFiltersPress(p);
  int count = (int)p.items.size();
  if (p.musicHome()) { if (count) p.sel = std::clamp(p.sel, 0, count - 1); }
  else { nav.columnsSetting = settings.cols; p.grid(ui::columns(settings.cols),settings.hideShorts); }
  bool hasItems = p.musicHome() ? count != 0 : !p.rows.empty();
  if (channel) channelSortInput(p);
  bool left = in.hit(Input::LEFT), right = in.hit(Input::RIGHT), up = in.hit(Input::UP), down = in.hit(Input::DOWN), a = in.hit(Input::A);
  bool firstColumn = p.musicHome() && !p.shelves.empty() ? p.sel == p.shelves[p.shelf].first : p.gridPosition() == 0;
  if (left && (p.zone == 0 || (p.zone == 1 && p.tabSel == 0) || (p.zone == 2 && p.channelSel == 0) || (p.zone == 3 && firstColumn))) {
    railFocus = true; railSel = railPage();
  } else if (p.zone == 0) {
    if (down) p.zone = tabs ? 1 : !p.channels.empty() ? 2 : 3;
    if (a && !p.loading && !nav.busy) {
      if (channel) channelHeaderPress(p, now);
      else if (playlist) playlistHeaderPress(p);
    }
  } else if (p.zone == 1) {
    int maxTab = channel ? 4 : search ? 5 : p.kind == "music_search" ? 5 : p.kind == "music" ? 3 : 2, before = p.tabSel;
    if (left && p.tabSel > 0) p.tabSel--;
    if (right && p.tabSel < maxTab) p.tabSel++;
    if (up && (channel || playlist)) p.zone = 0;
    // Busca: a direcional só anda pelos chips; buscar de novo é A no chip (ou LB/RB), e A na pílula abre o painel de filtros.
    if (a && search && p.tabSel == 5) searchUI.beginFilters(p.filters);
    else if (a && search && p.tabSel != p.searchChip()) searchChipSelected(p);
    else if (down || a) p.zone = !p.channels.empty() ? 2 : 3;
    if (p.tabSel != before && !search) {
      if (channel) channelTabSelected(p); else musicTabSelected(p);
      p.sel = 0; p.scroll = 0; nav.load(navWorker, settings.lang);
    }
  } else if (p.zone == 2) {
    subscriptionStripInput(p, left, right, up && tabs, down, a);
  } else {
    if (p.musicHome() && !p.shelves.empty()) {
      musicHomeMove(p, left, right, up, down);
    } else {
      p.moveGrid(right ? 1 : left ? -1 : 0,0);
      if (up && !p.moveGrid(0,-1)) p.zone = !p.channels.empty() ? 2 : tabs ? 1 : channel || playlist ? 0 : 3;
      if (down) p.moveGrid(0,1);
    }
    if (a && hasItems && !p.loading) {
      const auto item = p.items[(size_t)p.sel];
      if (item.type == "video") {
        if (playlist && nav.nativePage()) {
          phonePlaybackSerial = 0;
          nav.queueStart = item.id; nav.cancelQueue = false; nav.musicSession = p.music; nav.musicNow = false; nav.queueTitle = p.title;
          nav.action(navWorker,"/api/nav/play?kind="+p.kind+"&id="+net::urlEncode(p.id)+"&lang="+i18n::code(settings.lang));
        } else if (p.music) {  // música avulsa: toca já e monta a fila de rádio dela (como no YouTube Music), sem a página "Iniciar rádio"
          phonePlaybackSerial = 0;
          nav.queueStart = item.id; nav.cancelQueue = false; nav.musicSession = true; nav.musicNow = false; nav.queueTitle = std::string(tr(K::Radio)) + " · " + item.title;
          nav.action(navWorker,"/api/nav/play?kind=radio&id="+net::urlEncode(item.id)+"&lang="+i18n::code(settings.lang));
        }
        else requestVideo(item.video());
      } else if (item.type == "mixes") openNav("music", "", "mixes", true);
      else if (item.type == "history" || item.type == "later") openNav(item.type, "", "", p.music);
      else openNav(item.type == "playlist" && p.music ? "music_playlist" : item.type, item.id, "", p.music);
    }
    if (screen == NAV && nav.page().stamp == currentPage && in.hit(Input::START) && hasItems && p.items[(size_t)p.sel].type == "video") { nav.menuItem = p.items[(size_t)p.sel]; nav.menuSel = 0; nav.menu = true; }
  }
  if (screen == NAV && nav.page().stamp == currentPage && p.more && !p.loading) {
    bool needPage = p.sel >= count - 8 || (p.zone == 2 && p.channelSel >= (int)p.channels.size() - 4);
    if (p.musicHome()) needPage |= p.shelf + 3 >= (int)p.shelves.size();
    else if (p.rows.empty()) needPage = true;
    else if (p.gridRow()+2 >= (int)p.rows.size()) {
      const auto& row = p.rows[p.gridRow()];
      needPage |= !row.shorts || p.gridPosition()+8 >= (int)row.items.size();
    }
    if (needPage) nav.load(navWorker, settings.lang, false, true);
  }
  // X = Assistir mais tarde no vídeo em foco (atualizar a página agora vive no Menu)
  if (screen == NAV && nav.page().stamp == currentPage && !p.music && ctl::hit(in, ctl::Ctx::Collection, ctl::Act::WatchLater) && hasItems && p.zone == 3 && !p.loading &&
      p.sel >= 0 && p.sel < (int)p.items.size() && p.items[(size_t)p.sel].type == "video") saveForLater(p.items[(size_t)p.sel], now);
  if (screen == NAV && nav.page().stamp == currentPage && p.music) musicPageKeys(p, now);  // X = adicionar à fila, LB/RB = abas (ui_music.hpp)
  if (in.hit(Input::Y)) openSearch();
  if (in.hit(Input::B)) {
    if (nav.busy && nav.actionPath.rfind("/api/nav/play?", 0) == 0) nav.cancelQueue = true;
    navBack();
  }
}

// Salvar para depois, o mesmo que a entrada do Menu: com conta vai para o "Assistir mais tarde" do YouTube, sem conta para a lista local.
inline void UiContext::saveForLater(const NavItem& item, uint64_t now) {
  if (nav.busy) return;
  if (nav.signedIn) nav.action(navWorker, "/api/nav/action?action=later&id=" + item.id + "&lang=" + i18n::code(settings.lang));
  else { localUI.library.toggle(LocalLibraryUI::entry(item.video())); toast = tr(K::LocalSaving); toastUntil = now + 2500; }
}

// Botão "Reproduzir tudo" no cabeçalho de uma playlist (ou lista de Música): começa a fila do servidor desde o primeiro item.
inline void UiContext::playlistHeaderPress(NavPage& p) {
  phonePlaybackSerial = 0;
  nav.queueStart.clear();
  nav.cancelQueue = false; nav.musicSession = p.music; nav.musicNow = false; nav.queueTitle = p.title;
  nav.action(navWorker, "/api/nav/play?kind=" + p.kind + "&id=" + net::urlEncode(p.id) + "&lang=" + i18n::code(settings.lang));
}

// Faixa de canais das Inscrições: esquerda/direita escolhem, baixo volta para a grade, A abre o canal.
inline void UiContext::subscriptionStripInput(NavPage& p, bool left, bool right, bool up, bool down, bool a) {
  if (up) { p.zone = 1; return; }
  if (left && p.channelSel > 0) p.channelSel--;
  if (right && p.channelSel + 1 < (int)p.channels.size()) p.channelSel++;
  if (down) p.zone = 3;
  if (a && !p.channels.empty()) { const auto id = p.channels[(size_t)p.channelSel].id; openNav("channel", id); }
}

// Cabeçalho (capa, título, subtítulo e "Reproduzir tudo" quando é playlist ou lista de mais tarde), faixa de canais das
// Inscrições e a grade. A Biblioteca começa mais acima (120) e usa ladrilhos com o título sobre a capa.
inline float Navigation::drawCollectionPage(NavDraw& d) {
  auto& g = d.g; auto& p = d.p;
  float tx = 136;
  if (!p.image.empty()) { artwork(g, d.thumbs, d.worker, p.image, d.frame, tx, 28, 72, 72, true, p.kind == "artist"); tx += 96; }
  gfx::textBox(g, ui::F_BIG, p.displayTitle.empty() ? d.tr(titleKey(p)) : p.displayTitle, tx, 28, ui::W-tx-64, ui::TEXT);
  gfx::text(g, ui::F_CHAN, p.displaySub, tx, 82, ui::MUTED);
  if (playlistKind(p)) {
    bool f = d.contentFocus && p.zone == 0;
    gfx::rect(g, ui::W-316, 32, 252, 54, f ? ui::TEXT : ui::SURFACE2, 27);
    gfx::text(g, ui::F_KEY, d.tr(K::PlayAll), ui::W-190, 48, f ? ui::INK : ui::TEXT, 1);
  }
  float top = p.kind == "library" ? 120 : 178;
  if (p.kind == "subscriptions" && !p.channels.empty()) {
    // logo abaixo do cabeçalho: com subtítulo vazio (Inscrições) a faixa sobe até o fim do título (#27)
    const float stripY = p.displaySub.empty() ? 28 + g.fonts[ui::F_BIG].lineH + 18 : 126;
    int channelCount=ui::narrow ? 4 : ui::large ? 5 : 7;
    int first = std::max(0, p.channelSel-channelCount+1);
    for (int c = first; c < std::min(first+channelCount, (int)p.channels.size()); c++) {
      float x = 136 + (c - first) * 154; const auto& item = p.channels[(size_t)c];  // faixa logo abaixo do cabeçalho (#27)
      bool f = d.contentFocus && p.zone == 2 && c == p.channelSel;
      if (f) gfx::rect(g, x - 5, stripY - 5, 106, 106, ui::TEXT, 53);
      artwork(g, d.thumbs, d.worker, item.image, d.frame, x, stripY, 96, 96, true, true);
      gfx::text(g, ui::F_CHAN, item.byline, x, stripY + 110, f ? ui::TEXT : ui::MUTED);
    }
    top = stripY + 176;
  }
  navGrid(d, top, navMetrics(d));
  return top;
}
