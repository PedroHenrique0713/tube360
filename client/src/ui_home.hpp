// Início: abas, fileiras e cartões de vídeo (a entrada e o desenho da tela HOME).
#pragma once
#include "ui_context.hpp"

// A Home da conta (TVHTML5) vem em prateleiras com título, na ordem do YouTube; cada uma vira uma fileira rolável com o título.
// Só a categoria Início da conta e só quando a conta trouxe as seções; busca e demais categorias seguem a lista única.
inline int UiContext::homeShelves(const Shelf& cur) const {
  if (inResults || cur.kind!="home" || !account.signedIn()) return 0;
  for (const auto& v:cur.feed.items) if (!v.section.empty()) return 2;
  return 0;
}

inline void UiContext::homeInput(uint64_t now) {
  // Voltar ao Início depois de um tempo fora (player, busca, coleção) atualiza o feed sozinho, o que o X fazia antes.
  if (!inResults && homeSeenAt && now > homeSeenAt + 30000 && cats[(size_t)cat].stamp) refreshPage(now);
  homeSeenAt = now;
  Shelf& cur = inResults ? results : cats[(size_t)cat];
  auto& grid=cur.feed.layout;
  grid.build(cur.feed.items,cur.sel,ui::columns(settings.cols),settings.hideShorts,homeShelves(cur));
  int n = (int)cur.feed.items.size(), previousSel = cur.sel;
  bool hasItems=!grid.rows.empty();
  if (zone == 1 && !inResults) {
    if (in.hit(Input::LEFT)) { if (cat > 0) { cat--; scroll = 0; } else { railFocus = true; railSel = 0; } }
    if (in.hit(Input::RIGHT) && cat + 1 < (int)cats.size()) { cat++; scroll = 0; }
    if (!cats[cat].stamp) requestFeed(cats[cat], cat == 0 ? 0 : 3);
    if (cur.kind == "subs" && (in.hit(Input::DOWN) || in.hit(Input::A))) openNav("subscriptions");
    else if ((in.hit(Input::DOWN) || in.hit(Input::A)) && hasItems) zone = 0;
  } else {
    if (in.hit(Input::LEFT)) { if (grid.gridPosition(cur.sel)>0) grid.moveGrid(cur.sel,-1,0); else { railFocus = true; railSel = inResults ? 1 : 0; } }
    if (in.hit(Input::RIGHT)) grid.moveGrid(cur.sel,1,0);
    if (in.hit(Input::DOWN)) grid.moveGrid(cur.sel,0,1);
    // rolagem sem fim: perto do fim da grade, pede a próxima página
    bool nearEnd=!hasItems || cur.sel>=n-8;
    if (hasItems && grid.gridRow(cur.sel)+2 >= (int)grid.rows.size()) {
      const auto& row=grid.rows[grid.gridRow(cur.sel)];
      nearEnd |= !row.shorts || grid.gridPosition(cur.sel)+8 >= (int)row.items.size();
    }
    if (nearEnd && cur.feed.more && !cur.feed.loadingMore && !cur.feed.loading) {
      cur.feed.loadingMore = true;
      requestFeed(cur, inResults ? 1 : cat == 0 ? 0 : 3, false, n, true);
    }
    if (in.hit(Input::UP) && !grid.moveGrid(cur.sel,0,-1) && !inResults) zone = 1;
    if (in.hit(Input::A) && hasItems) {
      const Video& v = cur.feed.items[(size_t)cur.sel];
      // Um item já concluído recomeça; pedir o fim exato devolve HTTP 416.
      requestVideo(v, v.dur > 0 && v.pos >= v.dur ? 0 : v.pos);
    }
  }
  if (cur.sel != previousSel) LOG("home: categoria %d, foco %d/%d", cat, cur.sel + 1, n);
  if (in.hit(Input::START) && hasItems && zone == 0) { const auto& v = cur.feed.items[(size_t)cur.sel]; nav.menuItem = {"video",v.id,v.title,v.channel,v.id,"","",v.dur}; nav.menuSel = 0; nav.menu = true; }
  if (in.hit(Input::Y)) openSearch();
  // X = Assistir mais tarde no vídeo em foco; atualizar vive no Menu e acontece sozinho ao voltar para o Início
  if (ctl::hit(in, ctl::Ctx::Home, ctl::Act::WatchLater) && hasItems && zone == 0 && !railFocus) {
    const Video& v = cur.feed.items[(size_t)cur.sel];
    saveForLater(NavItem{"video",v.id,v.title,v.channel,v.id,"","",v.live ? 0 : v.dur > 0 ? v.dur : -1,v.members}, now);
  }
  if (in.hit(Input::B)) { if (inResults) goHome(); else if (zone == 0) zone = 1; }
}

inline void UiContext::homeDraw(Shelf& cur, double t, double sleepLeft) {
  static float selAnim[600] = {};
  const int cols = ui::columns(settings.cols);
  const float gap=24, cw=std::floor((ui::W-ui::X0-64-(cols-1)*gap)/cols), th=std::round(cw*9/16), rowH=th+cardTextBlock(g,false), top=150;
  auto& grid=cur.feed.layout;
  grid.build(cur.feed.items,cur.sel,cols,settings.hideShorts,homeShelves(cur));
  float shortW=std::min(cw*.5f,112.f), shortH=shortW*16/9, shortHeading=std::max(30.f,g.fonts[ui::F_KEY].lineH+8), shortRowH=shortHeading+shortH+(ui::large ? 124 : 100);
  const float groupH=std::max(40.f,g.fonts[ui::F_KEY].lineH+14);
  auto rowHeight=[&](const ItemGrid::GridRow& r) { return (r.shorts ? shortRowH : rowH)+(r.heading.empty() ? 0.f : groupH); };
  float want=0;
  if (zone==0) for (int row=0;row<grid.gridRow(cur.sel);++row) want+=rowHeight(grid.rows[row]);
  scroll+=(want-scroll)*.2f;
  float rowY=top-scroll;
  int cardsDrawn=0, noImage=0, noImageClipped=0;  // para o log abaixo
  for (const auto& row:grid.rows) {
    float y=rowY;
    rowY+=rowHeight(row);
    // Fileira que só aparece como uma tira na borda de baixo (cartões ou Shorts) some até a rolagem trazê-la: a tira, sem imagem, era a faixa cinza.
    const float artTop=y+(row.heading.empty() ? 0.f : groupH)+(row.shorts ? shortHeading : 0.f), artHeight=row.shorts ? shortH : th;
    if (y<140 || 646-artTop<std::min(artHeight*.45f,64.f)) continue;
    if (!row.heading.empty()) {  // título da prateleira do YouTube (conta)
      if (y+g.fonts[ui::F_KEY].lineH<=646) gfx::textBox(g,ui::F_KEY,gfx::ellipsize(g,ui::F_KEY,row.heading,ui::W-ui::X0-64),ui::X0,y,ui::W-ui::X0-64,ui::TEXT);
      y+=groupH;
    }
    int first=0, visible=(int)row.items.size();
    if (row.carousel) {  // fileira rolável: mostra `cols` cartões e acompanha o selecionado
      visible=cols;
      int selected=grid.gridRow(cur.sel)>=0 && &grid.rows[grid.gridRow(cur.sel)]==&row ? grid.gridPosition(cur.sel) : row.column;  // cada prateleira guarda a própria posição
      first=std::clamp(selected-visible+1,0,std::max(0,(int)row.items.size()-visible));
    }
    if (row.shorts) {
      if (y+g.fonts[ui::F_KEY].lineH<=646) gfx::text(g,ui::F_KEY,tr(K::ChannelShorts),ui::X0,y,ui::TEXT);
      y+=shortHeading;
      visible=std::max(1,(int)((ui::W-ui::X0-64+gap)/(shortW+gap)));
      int selected=grid.gridRow(cur.sel)>=0 && grid.rows[grid.gridRow(cur.sel)].shorts ? grid.gridPosition(cur.sel) : grid.shortColumn;
      first=std::clamp(selected-visible+1,0,std::max(0,(int)row.items.size()-visible));
    }
    for (int col=first;col<std::min((int)row.items.size(),first+visible);++col) {
      int i=row.items[col];
      const Video& v=cur.feed.items[(size_t)i];
      float artW=row.shorts ? shortW : cw, artH=row.shorts ? shortH : th;
      float x=ui::X0+(col-first)*(artW+gap);
      bool focused=zone==0 && !railFocus && i==cur.sel;
      float& an=selAnim[i%600]; an+=((focused ? 1.f : 0.f)-an)*.25f;
      const auto* saved=(!account.signedIn() || cur.kind=="local_later" || cur.kind=="local_history")?local::Library::find(localUI.library.history,v.id):nullptr;
      homeCard(g,thumbs,thumbsWorker,frame,settings.lang,v,cur.feed.direct,row.shorts,focused,x,y,artW,artH,an,saved);
      ++cardsDrawn;
      if (!thumbs.byId.count(v.id)) { ++noImage; if (y+artH>646) ++noImageClipped; }
    }
  }
  // O que a tela mostra de miniatura e de aviso, no log só quando muda (separa "miniatura que não chegou" de "aviso de atualização" na faixa do pé).
  {
    static int lastShown=-1, lastClipped=-1, lastNotice=-1;
    const int notice=updateUI.status.app.build ? 1 : 0;
    if (noImage!=lastShown || noImageClipped!=lastClipped || notice!=lastNotice) {
      lastShown=noImage; lastClipped=noImageClipped; lastNotice=notice;
      LOG("home: %d cartoes na tela, %d sem miniatura (%d cortados pela borda de baixo), aviso de atualizacao=%d", cardsDrawn, noImage, noImageClipped, notice);
    }
  }
  if (grid.rows.empty()) {
    if (cur.feed.loading || cur.feed.loadingMore) { spinner(g, ui::X0 + (ui::W - ui::X0) / 2 - 32, 380, 22, t); gfx::text(g, ui::F_BODY, tr(K::LoadingVideos), ui::X0 + (ui::W - ui::X0) / 2 - 32, 420, ui::MUTED, 1); }
    else if (cur.feed.error != K::None) {
      float y=190;
#ifndef __XSDK__
      errorShot={&cur.feed,cur.feed.error,K::Refresh};
#endif
      for(const auto& line:gfx::wrap(g,ui::F_H2,tr(cur.feed.error),ui::W-ui::X0-64,4)) {
        gfx::textBox(g,ui::F_H2,line,ui::X0,y,ui::W-ui::X0-64,ui::TEXT);y+=g.fonts[ui::F_H2].lineH+8;
      }
      const auto& errorDetail=cur.feed.errorDetail;
      if(!errorDetail.empty()){gfx::textBox(g,ui::F_BODY,errorDetail,ui::X0,y+16,ui::W-ui::X0-64,ui::MUTED);y+=48;}
      ctlHint(g,ui::X0,y+24,ctl::Ctx::Home,ctl::Act::Options,settings.lang,K::Refresh);
    } else if (cur.kind=="live") {  // F71: sem conta ou ninguém ao vivo agora
      float y=190;
      for(const auto& line:gfx::wrap(g,ui::F_H2,tr(account.signedIn() ? K::HomeLiveNone : K::HomeLiveSignIn),ui::W-ui::X0-64,3)) {
        gfx::textBox(g,ui::F_H2,line,ui::X0,y,ui::W-ui::X0-64,ui::MUTED);y+=g.fonts[ui::F_H2].lineH+8;
      }
    } else gfx::text(g, ui::F_BODY, tr(K::Empty), ui::X0, 190, ui::MUTED);
  }
  if (ui::large || ui::narrow) gfx::rect(g,96,648,ui::W-96,72,ui::BG);
  // topo fixo: o nome e as categorias (a grade passa por baixo)
  gfx::rect(g, 0, 0, ui::W, 118, ui::BG, 0);
  gfx::vgrad(g, 0, 118, ui::W, 36, ui::BG, ui::alpha(ui::BG, 0));
  wordmark(g, ui::X0, 22, 1, ui::BG);
  {
    float hw = 24 + 8 + gfx::textWidth(g, ui::F_SMALL, tr(K::WatchLater)) + 26 + 24 + 8 + gfx::textWidth(g, ui::F_SMALL, tr(K::Search)) + 26 + 24 + 8 + gfx::textWidth(g, ui::F_SMALL, tr(K::MoreOptions));
    float lx = ui::W - 88 - hw, pillRight = lx - 18;
    if (ui::narrow || ui::large) lx=ui::X0;
    float hintY=(ui::narrow || ui::large) ? 680 : 32;
    auto pill = [&](const std::string& label, Color color) {
      std::string txt = gfx::ellipsize(g, ui::F_SMALL, label, 218);
      float w = gfx::textWidth(g, ui::F_SMALL, txt) + 32;
      gfx::rect(g, pillRight - w, 30, w, 28, ui::alpha(color, 0.16f), 14);
      gfx::rect(g, pillRight - w + 10, 41, 6, 6, color, 3);
      gfx::text(g, ui::F_SMALL, txt, pillRight - w + 22, 34, ui::TEXT);
      pillRight -= w + 10;
    };
    // a contagem do timer entra na fileira de pílulas (sozinha à direita, cobria a legenda do X)
    if (sleep.active && !ui::narrow && !ui::large) {
      std::string value = sleep.end ? tr(K::SleepEnd) : fmtTime(std::ceil(sleepLeft));
      if (sleep.end && sleepLeft >= 0) value += " · " + fmtTime(std::ceil(sleepLeft));
      pill(std::string(tr(K::SleepTimer)) + " · " + value, ui::A1);
    }
    if (!castWho.empty() && !ui::narrow && !ui::large) pill(tr(K::PhonePrefix) + castWho, rgb(0x34c759));
    accountPhoto();
    float hx3 = ctlHint(g, lx, hintY, ctl::Ctx::Home, ctl::Act::WatchLater, settings.lang);
    float hx4 = ctlHint(g, hx3, hintY, ctl::Ctx::Home, ctl::Act::Search, settings.lang);
    ctlHint(g, hx4, hintY, ctl::Ctx::Home, ctl::Act::Options, settings.lang, K::MoreOptions);
  }
  if (inResults) {
    gfx::text(g, ui::F_H2, gfx::ellipsize(g, ui::F_H2, cur.feed.title, ui::W-ui::X0-220), ui::X0, 78, ui::TEXT);
    hint(g, ui::W - 64 - 120, 80, "B", XB, tr(K::Home));
  } else {
    // Chips inteiros: a janela acompanha o selecionado sem sair da tela.
    int first = cat; float used = gfx::textWidth(g, ui::F_BODY, cats[(size_t)cat].title) + 34;
    while (first > 0) {
      float prev = gfx::textWidth(g, ui::F_BODY, cats[(size_t)first - 1].title) + 44;
      if (used + prev > ui::W - ui::X0 - 64) break;
      used += prev; first--;
    }
    float cx = ui::X0;
    int last = first - 1;
    for (int c = first; c < (int)cats.size(); c++) {
      float w = gfx::textWidth(g, ui::F_BODY, cats[(size_t)c].title) + 34;
      if (cx + w > ui::W - 64) break;
      bool sel = c == cat, f = sel && zone == 1 && !railFocus;
      if (f) gfx::shadow(g, cx, 76 + 5, w, 40, 20, 14, 0.5f);
      gfx::rect(g, cx, 76, w, 40, f ? rgb(0xffffff) : sel ? ui::alpha(rgb(0xffffff), 0.9f) : ui::alpha(rgb(0xffffff), 0.09f), 20);
      gfx::text(g, ui::F_BODY, cats[(size_t)c].title, cx + w / 2, 85, sel ? ui::INK : ui::TEXT, 1);
      if (cats[(size_t)c].feed.loading && cats[(size_t)c].feed.items.empty()) gfx::rect(g, cx + w - 12, 82, 6, 6, ui::A1, 3);
      cx += w + 10;
      last = c;
    }
    if (first > 0) gfx::text(g, ui::F_SMALL, tr(K::BtnHorizontal), ui::X0 - 34, 88, ui::FAINT);
    if (last + 1 < (int)cats.size()) gfx::triangle(g, ui::W - 36, 89, ui::W - 29, 96, ui::W - 36, 103, ui::MUTED);
  }
}
