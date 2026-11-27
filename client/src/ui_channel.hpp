// Páginas de navegação: canal (cabeçalho e abas Vídeos/Shorts/Ao vivo/Playlists), Inscrições, Biblioteca, Histórico,
// Assistir mais tarde, resultados da busca e as páginas de Música: a entrada (UiContext::navInput) e o desenho
// (Navigation::draw, que escolhe o que mostrar pelo p.kind da página). O estado das páginas fica em navigation.hpp.
#pragma once
#include "ui_context.hpp"

inline void UiContext::navInput(uint64_t now) {
  auto& p = nav.page(); uint64_t currentPage = p.stamp;
  bool channel = p.kind == "channel";
  bool playlist = p.kind == "playlist" || p.kind == "music_playlist" || p.kind == "music_recent" || p.kind == "album" || p.kind == "radio" || p.kind == "later";
  bool tabs = channel || p.kind == "music" || p.kind == "music_search";
  if (p.kind=="search" && in.hit(Input::LB)) searchUI.beginFilters(p.filters);
  int count = (int)p.items.size();
  if (p.musicHome()) { if (count) p.sel = std::clamp(p.sel, 0, count - 1); }
  else p.grid(ui::columns(4),settings.hideShorts);
  bool hasItems = p.musicHome() ? count != 0 : !p.rows.empty();
  bool left = in.hit(Input::LEFT), right = in.hit(Input::RIGHT), up = in.hit(Input::UP), down = in.hit(Input::DOWN), a = in.hit(Input::A);
  bool firstColumn = p.musicHome() && !p.shelves.empty() ? p.sel == p.shelves[p.shelf].first : p.gridPosition() == 0;
  if (left && (p.zone == 0 || (p.zone == 1 && p.tabSel == 0) || (p.zone == 2 && p.channelSel == 0) || (p.zone == 3 && firstColumn))) {
    railFocus = true; railSel = railPage();
  } else if (p.zone == 0) {
    if (down) p.zone = tabs ? 1 : !p.channels.empty() ? 2 : 3;
    if (a && !p.loading && !nav.busy) {
      if (channel) {
        if (!account.signedIn()) { toast = tr(K::AccountRequired); toastUntil = now + 4000; }
        else if (p.subscribed) nav.unsubscribe = true;
        else nav.action(navWorker, "/api/nav/action?action=subscribe&id=" + p.channelId + "&value=1&lang=" + i18n::code(settings.lang));
      } else if (playlist) {
        phonePlaybackSerial = 0;
        nav.queueStart.clear();
        nav.cancelQueue = false; nav.musicSession = p.music; nav.musicNow = false;
        nav.action(navWorker, "/api/nav/play?kind=" + p.kind + "&id=" + net::urlEncode(p.id) + "&lang=" + i18n::code(settings.lang));
      }
    }
  } else if (p.zone == 1) {
    int maxTab = channel || p.kind == "music_search" ? 3 : 2, before = p.tabSel;
    if (left && p.tabSel > 0) p.tabSel--;
    if (right && p.tabSel < maxTab) p.tabSel++;
    if (up && (channel || playlist)) p.zone = 0;
    if (down || a) p.zone = 3;
    if (p.tabSel != before) {
      p.tab = channel ? (p.tabSel==1 ? "shorts" : p.tabSel==2 ? "live" : p.tabSel==3 ? "playlists" : "videos") : p.tabSel == 1 ? "live" : p.tabSel == 2 ? "playlists" : "videos";
      if (p.kind == "music") p.query = p.tabSel == 1 ? "charts" : p.tabSel == 2 ? "mixes" : "";
      p.sel = 0; p.scroll = 0; nav.load(navWorker, settings.lang);
    }
  } else if (p.zone == 2) {
    if (left && p.channelSel > 0) p.channelSel--;
    if (right && p.channelSel + 1 < (int)p.channels.size()) p.channelSel++;
    if (down) p.zone = 3;
    if (a && !p.channels.empty()) { const auto id = p.channels[(size_t)p.channelSel].id; openNav("channel", id); }
  } else {
    if (p.musicHome() && !p.shelves.empty()) {
      auto& shelf = p.shelves[p.shelf];
      if (left && p.sel > shelf.first) shelf.selected--;
      if (right && p.sel + 1 < shelf.end) shelf.selected++;
      if (up && p.shelf == 0) p.zone = 1;
      else if (up) p.shelf--;
      if (down && p.shelf + 1 < (int)p.shelves.size()) p.shelf++;
      p.sel = p.shelves[p.shelf].selected;
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
          nav.queueStart = item.id; nav.cancelQueue = false; nav.musicSession = p.music; nav.musicNow = false;
          nav.action(navWorker,"/api/nav/play?kind="+p.kind+"&id="+net::urlEncode(p.id)+"&lang="+i18n::code(settings.lang));
        } else if (p.music) { openNav("radio", item.id, "", true); nav.page().zone = 0; }
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
  if (screen == NAV && nav.page().stamp == currentPage && in.hit(Input::X)) { nav.page().sel = 0; nav.page().scroll = 0; nav.load(navWorker, settings.lang, true); }
  if (in.hit(Input::Y)) openSearch();
  if (in.hit(Input::B)) {
    if (nav.busy && nav.actionPath.rfind("/api/nav/play?", 0) == 0) nav.cancelQueue = true;
    navBack();
  }
}

inline void Navigation::draw(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, uint64_t frame, uint64_t now, i18n::Lang lang, bool contentFocus, bool hideShorts) {
  if (pages.empty()) return;
  refreshLocal(g,lang);
  auto tr = [&](K k) { return i18n::get(lang, k); };
  auto& p = page();
  bool home = p.musicHome();
  bool hasBanner = !p.banner.empty();
  if (hasBanner) {
    if (const auto* cover = thumbs.get(worker, p.banner, frame, p.direct, true)) {
      gfx::image(g, cover->tex, 96, 0, Thumbs::bannerWidth, Thumbs::bannerHeight);
      gfx::hgrad(g, 96, 0, 780, Thumbs::bannerHeight, {0,0,0,0.55f}, {0,0,0,0});
      gfx::vgrad(g, 96, 0, Thumbs::bannerWidth, Thumbs::bannerHeight, {0,0,0,0.10f}, ui::BG);
    }
  }
  if (p.music && !home) gfx::hgrad(g, 96, 0, 1184, 224, rgb(0x32161f), ui::BG);
  float tx = 136;
  if (!home && !p.image.empty()) { artwork(g, thumbs, worker, p.image, frame, tx, p.music ? 46 : 28, p.music ? 86 : 72, p.music ? 86 : 72, true, p.kind == "channel" || p.kind == "artist"); tx += p.music ? 110 : 96; }

  int titleFont = p.music && !p.musicHome() ? ui::F_HERO : ui::F_BIG;
  gfx::textBox(g, titleFont, p.displayTitle.empty() ? tr(titleKey(p)) : p.displayTitle, tx, p.music && !home ? 42 : 28, home ? (ui::narrow ? 150 : 480) : ui::W-tx-64, ui::TEXT);
  if (home && !p.musicLabel.empty()) gfx::textBox(g, ui::F_TINY, p.musicLabel, ui::narrow ? 300 : tx, ui::narrow ? 32 : 69, ui::W-(ui::narrow ? 300 : tx)-64, ui::MUTED);
  if (!home) gfx::text(g, ui::F_CHAN, p.displaySub, tx, p.music ? 103 : 82, ui::MUTED);
  bool channel = p.kind == "channel", playlist = p.kind == "playlist" || p.kind == "album" || p.kind == "music_playlist" || p.kind == "music_recent" || p.kind == "later" || p.kind == "radio";
  if (channel || playlist) {
    bool f = contentFocus && p.zone == 0;
    gfx::rect(g, ui::W-316, p.music ? 64 : 32, 252, 54, f ? ui::TEXT : ui::SURFACE2, 27);
    gfx::text(g, ui::F_KEY, tr(channel ? p.subscribed ? K::Subscribed : K::Subscribe : K::PlayAll), ui::W-190, p.music ? 80 : 48, f ? ui::INK : ui::TEXT, 1);
  }
  if (p.kind=="search") {
    float hx=hint(g,136,126,"LB",XG,tr(K::SearchFilters));
    for (size_t row=0;row<p.filterLines.size();++row) gfx::textBox(g,ui::F_CHAN,p.filterLines[row],hx,122+row*30,ui::W-hx-64,ui::A2);
  }
  bool tabs = channel || p.kind == "music" || p.kind == "music_search";
  if (tabs) {
    const K channelTabs[] = {K::Videos,K::ChannelShorts,K::ChannelLive,K::Playlists};
    const K musicTabs[] = {K::Home,K::Charts,K::Mixes};
    const K searchTabs[] = {K::AllResults,K::Videos,K::Channel};
    const K songTabs[] = {K::AllResults,K::Songs,K::Albums,K::Artists};
    int count = channel || p.kind == "music_search" ? 4 : 3; float x = home && !ui::narrow ? 650 : 136;
    float tabY = home ? (ui::narrow ? 88 : 32) : p.music ? 166 : 120;
    for (int i = 0; i < count; i++) {
      K key = channel ? channelTabs[i] : p.kind == "music" ? musicTabs[i] : p.kind == "music_search" ? songTabs[i] : searchTabs[i];
      float w = gfx::textWidth(g, ui::F_KEY, tr(key)) + 40;
      bool active = i == p.tabSel, focus = contentFocus && active && p.zone == 1;
      gfx::rect(g, x, tabY, w, 42, focus ? ui::TEXT : active ? ui::SURFACE2 : ui::BG, 23);
      gfx::text(g, ui::F_KEY, tr(key), x + w / 2, tabY + 11, focus ? ui::INK : ui::TEXT, 1);
      if (active && !focus) gfx::hgrad(g, x + 20, tabY + 43, w - 40, 3, ui::A1, ui::A2, 1.5f);
      x += w + 14;
    }
  }
  float top = p.kind == "library" ? 120 : p.kind=="search" ? 210 : p.music ? 246 : 178;
  if (p.kind == "subscriptions" && !p.channels.empty()) {
    int channelCount=ui::narrow ? 4 : ui::large ? 5 : 7;
    int first = std::max(0, p.channelSel-channelCount+1);
    for (int c = first; c < std::min(first+channelCount, (int)p.channels.size()); c++) {
      float x = 136 + (c - first) * 154; const auto& item = p.channels[(size_t)c];
      bool f = contentFocus && p.zone == 2 && c == p.channelSel;
      if (f) gfx::rect(g, x - 5, 166 - 5, 106, 106, ui::TEXT, 53);
      artwork(g, thumbs, worker, item.image, frame, x, 166, 96, 96, true, true);
      gfx::text(g, ui::F_CHAN, item.byline, x, 276, f ? ui::TEXT : ui::MUTED);
    }
    top = 342;
  }
  int cols=ui::columns(4); float cw=std::floor((ui::W-200-(cols-1)*24)/cols), art=p.music ? std::min(cw,246.f) : cw, h=p.music ? art : cw*9/16, rowH=h+(p.kind=="library" ? 28 : ui::large ? 114 : 100);
  p.grid(cols,hideShorts);
  float shortW=std::min(cw*.5f,112.f), shortH=shortW*16/9, shortHeading=std::max(30.f,g.fonts[ui::F_KEY].lineH+8), shortRowH=shortHeading+shortH+(ui::large ? 124 : 100);
  float want = 0;
  if (p.zone == 3) for (int row=0; row<p.gridRow(); ++row) want += p.rows[row].shorts ? shortRowH : rowH;
  p.scroll += (want - p.scroll) * 0.24f;
  if (home) {
    int shelfCount=ui::large ? 2 : 3, shelfColumns=ui::narrow ? 4 : ui::large ? 5 : 7;
    int firstShelf = std::max(0, p.shelf-shelfCount+1);
    for (int row = firstShelf; row < std::min(firstShelf+shelfCount, (int)p.shelves.size()); row++) {
      const auto& shelf = p.shelves[row];
      float y = (ui::narrow ? 150 : 99) + (row-firstShelf)*(ui::large ? 216 : 184);
      gfx::text(g, ui::F_KEY, shelf.title, 136, y, ui::TEXT);
      int first = std::max(shelf.first, shelf.selected-shelfColumns+1);
      for (int i = first; i < std::min(shelf.end, first+shelfColumns); i++) {
        const auto& item = p.items[i]; float x = 136 + (i - first) * 155;
        bool f = contentFocus && p.zone == 3 && i == p.sel;
        if (f) gfx::rect(g, x - 4, y + 26, 118, 118, ui::TEXT, 9);
        artwork(g, thumbs, worker, item.image, frame, x, y + 30, 110, 110, true);
        memberBadge(g, item.members, lang, x + 5, y + 35);
        gfx::textBox(g, ui::F_SMALL, item.titleLines[0], x, y + 143, 135, ui::TEXT);
        gfx::textBox(g, ui::F_TINY, item.byline, x, y + (ui::large ? 178 : 163), 135, ui::MUTED);
      }
    }
  } else {
  float rowY = top-p.scroll;
  for (const auto& row : p.rows) {
    float y=rowY, artW=row.shorts ? shortW : art, artH=row.shorts ? shortH : h;
    rowY += row.shorts ? shortRowH : rowH;
    if (y < top-2 || y >= 646) continue;
    if (p.kind == "library" && !row.shorts && y+h > 656) continue;
    int first=0, visible=(int)row.items.size();
    if (row.shorts) {
      if (y+g.fonts[ui::F_KEY].lineH <= 646) gfx::text(g,ui::F_KEY,tr(K::ChannelShorts),136,y,ui::TEXT);
      y+=shortHeading;
      visible=std::max(1,(int)((ui::W-200+24)/(shortW+24)));
      int selected=p.gridRow()>=0 && p.rows[p.gridRow()].shorts ? p.gridPosition() : p.shortColumn;
      first=std::clamp(selected-visible+1,0,std::max(0,(int)row.items.size()-visible));
    }
    for (int col=first; col<std::min((int)row.items.size(),first+visible); ++col) {
    int i=row.items[col];
    const auto& item = p.items[i];
    float x = 136 + (col-first) * ((row.shorts ? shortW : cw) + 24);
    bool f = contentFocus && p.zone == 3 && (int)i == p.sel;
    if (f && y-5 < 646) gfx::rect(g, x - 5, y - 5, artW + 10, std::min(artH+10,646-y+5), ui::TEXT, 12);
    artwork(g, thumbs, worker, item.image, frame, x, y, artW, artH, p.music || item.type == "channel" || item.type == "artist", item.type == "channel" || item.type == "artist", p.direct, row.shorts, 646);
    if (y+7+(ui::large ? std::max(36.f,g.fonts[ui::F_TINY].size+8) : 20) <= 646) memberBadge(g, item.members, lang, x + 7, y + 7);
    const auto* saved=local && (!signedIn || p.kind=="local_later" || p.kind=="local_history")?local::Library::find(local->history,item.id):nullptr;
    double pos=saved?saved->position:0,duration=saved?saved->duration:item.duration;
    if(item.type=="video" && pos>0 && duration>0 && y+artH <= 646){gfx::rect(g,x+4,y+artH-6,artW-8,4,ui::SURFACE2,2);gfx::rect(g,x+4,y+artH-6,(artW-8)*float(std::clamp(pos/duration,0.0,1.0)),4,ui::A1,2);}
    if (p.kind == "library" && !row.shorts) {
      gfx::vgrad(g, x, y, art, h, {0,0,0,0}, {0,0,0,0.94f}, 12);
      float labelY = y + h - 12 - 27 * item.titleLines.size();
      for (const auto& line : item.titleLines) { gfx::textBox(g, ui::F_KEY, line, x + 12, labelY, art-24, ui::TEXT); labelY += 27; }
      continue;
    }
    if (item.image.empty()) {
      K k = item.type == "history" ? K::History : item.type == "later" ? K::WatchLater : item.type == "mixes" ? K::Mixes : K::Playlists;
      if (y+h/2-12+g.fonts[ui::F_H2].lineH <= 646) gfx::text(g, ui::F_H2, gfx::ellipsize(g, ui::F_H2, tr(k), art - 32), x + 16, y + h / 2 - 12, ui::MUTED);
    }
    float ty = y + artH + 16, textW=row.shorts ? shortW : cw;
    int titleFont=row.shorts ? ui::F_SMALL : ui::F_KEY, bylineFont=row.shorts ? ui::F_TINY : ui::F_CHAN;
    for (const auto& line : item.titleLines) {
      if (ty+g.fonts[titleFont].lineH <= 646) gfx::textBox(g, titleFont, line, x, ty, textW, ui::TEXT);
      ty += ui::large ? 34 : 27;
    }
    if (ty+4+g.fonts[bylineFont].lineH <= 646) gfx::textBox(g, bylineFont, item.byline, x, ty + 4, textW, ui::MUTED);
    }
  }
  }
  if ((home ? p.items.empty() : p.rows.empty()) && !p.loading && !p.more) {
    std::string msg = !p.error.empty() ? p.error : tr(channel && p.tab == "live" ? K::NoLive : K::Empty);
    float y = top + 42;
    for (const auto& line : gfx::wrap(g, ui::F_H2, msg, ui::W-200, 3)) { gfx::textBox(g, ui::F_H2, line, 136, y, ui::W-200, ui::MUTED); y += 34; }
  }
  gfx::rect(g, 96, 646, ui::W-96, 74, ui::BG);
  if (p.loading) { spinner(g, ui::W-64, 146, 12, now / 1000.0); gfx::text(g, ui::F_CHAN, tr(p.offset ? K::MoreLoading : K::Loading), ui::W-324, 130, ui::MUTED); }
  float hx = hint(g, 136, 676, "A", XA, tr(p.zone == 0 ? channel ? p.subscribed ? K::Subscribed : K::Subscribe : playlist ? K::PlayAll : K::Open : K::Open));
  hx = hint(g, hx, 676, "B", XB, tr(K::Back));
  bool videoMenu=p.zone == 3 && (home ? !p.items.empty() : !p.rows.empty()) && p.sel < (int)p.items.size() && p.items[(size_t)p.sel].type == "video";
  // Reserva a dica de salvar no rodapé curto; X/Y continuam disponíveis.
  if (!videoMenu || (!ui::narrow && !ui::large)) {
    hx = hint(g, hx, 676, "X", XX, tr(K::Refresh));
    hx = hint(g, hx, 676, "Y", XY, tr(K::Search));
  }
  if (videoMenu) hint(g, hx, 676, "Menu", XG, tr(K::MoreOptions));
}

inline void Navigation::drawMenu(gfx::Gfx& g, i18n::Lang lang) {
  auto tr = [&](K k) { return i18n::get(lang, k); };
  gfx::rect(g, 0, 0, ui::W, 720, {0,0,0,0.8f});
  float left=(ui::W-740)/2;
  gfx::rect(g, left, 150, 740, 480, ui::SURFACE, 28);
  gfx::text(g, ui::F_BIG, tr(K::CardMenu), left+38, 180, ui::TEXT);
  gfx::text(g, ui::F_KEY, gfx::ellipsize(g, ui::F_KEY, menuItem.title, 662), left+38, 235, ui::TEXT);
  gfx::text(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, menuItem.channel + " · " + fmtTime(menuItem.duration), 662), left+38, 272, ui::MUTED);
  const K keys[] = {K::GoChannel, !signedIn ? (local && local->contains(menuItem.id) ? K::RemoveLocalLater : K::LocalLater) : K::WatchLater, K::Radio, local && local->contains(menuItem.id) ? K::RemoveLocalLater : K::LocalLater};
  for (int i = 0; i < (signedIn ? 4 : 3); i++) {
    gfx::rect(g, left+34, 320 + i * 60, 672, 50, i == menuSel ? ui::TEXT : ui::SURFACE2, 16);
    gfx::text(g, ui::F_KEY, tr(keys[i]), left+58, 334 + i * 60, i == menuSel ? ui::INK : ui::TEXT);
  }
  float hx = hint(g, left+38, 584, "A", XA, tr(K::Open)); hint(g, hx, 584, "B", XB, tr(K::Back));
}
