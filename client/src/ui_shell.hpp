// Casca comum das telas: a barra lateral (entrada e desenho) e o que aparece por cima de qualquer tela (painéis do celular,
// da conta e do timer, aviso e menu de vídeo).
#pragma once
#include "ui_context.hpp"

inline void UiContext::railInput() {
  if (in.hit(Input::UP) && railSel > 0) railSel--;
  if (in.hit(Input::DOWN) && railSel < 7) railSel++;
  if (in.hit(Input::RIGHT) || in.hit(Input::B)) railFocus = false;
  if (in.hit(Input::A)) {
    if (screen != HOME && railSel >= 2 && railSel <= 4) { goHome(); railFocus = true; }
    if (railSel == 0) goHome();
    else if (railSel == 1) { if (screen == SEARCH) railFocus = false; else openSearch(); }
    else if (railSel == 2) openNav("subscriptions");
    else if (railSel == 3) openNav("library");
    else if (railSel == 4) openNav("music", "", "", true);
    else if (railSel == 5) { phonePanel = true; }
    else if (railSel == 6) { screen = SETTINGS; railFocus = false; nestInfoAt = 0; accountAt = 0; }
    else quit = true;
  }
}

inline void UiContext::railDraw(bool remoteOnline) {
  // barra lateral: ícones; com foco, abre com os nomes
  float railExtra=ui::large ? 270 : 200;
  float rw = ui::RAIL + railExtra * railOpen;
  if (railOpen > 0.01f) {
    gfx::rect(g, 0, 0, rw, ui::H, ui::alpha(ui::BG, 0.96f * railOpen), 0);
    gfx::hgrad(g, rw - 1, 0, 200, ui::H, ui::alpha(ui::BG, 0.96f * railOpen), ui::alpha(ui::BG, 0));
  }
  const char* names[] = {tr(K::Home), tr(K::Search), tr(K::Subscriptions), tr(K::Library), tr(K::Music), tr(K::Phone), tr(K::Settings), tr(K::Exit)};
  int page = railPage();
  for (int i = 0; i < 8; i++) {
    float y = 142 + i * 68;
    bool f = railFocus && railSel == i, act = i == page;
    if (f) gfx::rect(g, 30, y - 24, 52 + (railExtra-32) * railOpen, 48, rgb(0xffffff), 24);
    else if (act) gfx::rect(g, 30, y - 24, 52, 48, ui::alpha(rgb(0xffffff), 0.1f), 24);
    if (act && !f) gfx::vgrad(g, 16, y - 12, 4, 24, ui::A1, ui::A2, 2);
    Color ic = f ? ui::INK : act ? ui::TEXT : ui::MUTED;
    const int symbols[] = {0,1,7,6,5,2,3,4};
    icon(g, symbols[i], 56, y, ic, f ? rgb(0xffffff) : act ? ui::mix(ui::BG, rgb(0xffffff), 0.1f) : ui::BG);
    if (railOpen > 0.05f) gfx::text(g, ui::F_BODY, names[i], 94, y - 12, ui::alpha(f ? ui::INK : ui::TEXT, railOpen));
    if (i == 5) gfx::rect(g, 66, y - 18, 9, 9, !castWho.empty() ? rgb(0x34c759) : remoteOnline ? rgb(0xd4d4d8) : ui::FAINT, 4.5f);
  }
}

// Tudo o que desenha por cima das telas, na ordem do quadro: menu de vídeo, painéis (celular, conta, timer), avisos e atualização.
inline void UiContext::overlaysDraw(uint64_t now, double t, bool sleepWarning, double sleepLeft) {
  if (nav.menu) nav.drawMenu(g, settings.lang);
  localUI.draw(g,settings.lang);
  if (nav.unsubscribe) {
    gfx::rect(g, 0, 0, ui::W, 720, {0,0,0,0.8f}); gfx::rect(g, (ui::W-780)/2, 250, 780, 210, ui::SURFACE, 24);
    gfx::text(g, ui::F_H2, tr(K::SubscribeConfirm), (ui::W-700)/2, 290, ui::TEXT);
    float hx = hint(g, (ui::W-700)/2, 398, "A", XA, tr(K::Confirm)); hint(g, hx, 398, "B", XB, tr(K::Cancel));
  }
  if (nav.busy && screen != PLAYER) {
    gfx::rect(g, (ui::W-460)/2, 615, 460, 48, ui::SURFACE2, 24);
    gfx::text(g, ui::F_CHAN, tr(nav.actionPath.rfind("/api/nav/play?",0) == 0 ? K::StartingQueue : K::Saving), ui::W/2, 630, ui::TEXT, 1);
  }
  if (phonePanel) {
    gfx::rect(g, 0, 0, ui::W, ui::H, {0, 0, 0, 0.72f});
    float pw = 820, ph = ui::large ? 500 : 418, px = (ui::W - pw) / 2, py = (ui::H - ph) / 2;
    gfx::shadow(g, px, py + 10, pw, ph, 28, 40, 0.6f);
    gfx::rect(g, px, py, pw, ph, ui::SURFACE, 28);
    icon(g, 2, px + 56, py + 60, ui::TEXT, ui::SURFACE);
    gfx::text(g, ui::F_BIG, tr(K::CastTitle), px + 92, py + 40, ui::TEXT);
    float ty = py + 104;
    for (auto& line : gfx::wrap(g, ui::F_BODY, std::string(tr(K::CastChoose)) + "YouTube 360.", pw - 96, 2)) {
      gfx::textBox(g, ui::F_BODY, line, px + 48, ty, pw-96, ui::TEXT); ty += ui::large ? 32 : 25;
    }
    for (auto& line : gfx::wrap(g, ui::F_SMALL, tr(K::CastPath), pw - 96, 2)) {
      gfx::textBox(g, ui::F_SMALL, line, px + 48, ty + 8, pw-96, ui::MUTED); ty += ui::large ? 30 : 22;
    }
    if (!castCode.empty()) gfx::textGrad(g, ui::F_CODE, castCode, px + 48, py + (ui::large ? 252 : 186), ui::A1, ui::A2);
    else gfx::text(g, ui::F_BODY, tr(K::CastLoading), px + 48, py + (ui::large ? 270 : 208), ui::FAINT);
    gfx::textBox(g, ui::F_SMALL, gfx::ellipsize(g,ui::F_SMALL,tr(K::CastRemember),pw-96), px + 48, py + (ui::large ? 330 : 264), pw-96, ui::MUTED);
    gfx::rect(g, px + 48, py + (ui::large ? 366 : 300), pw - 96, 1, ui::alpha(rgb(0xffffff), 0.08f));
    std::string phoneLabel=!castWho.empty() ? tr(K::PhoneConnected)+castWho : std::string(tr(phoneStatus.online ? K::NoPhone : K::CastOffline));
    gfx::textBox(g,ui::F_SMALL,gfx::ellipsize(g,ui::F_SMALL,phoneLabel,pw-96),px+48,py+(ui::large ? 386 : 320),pw-96,!castWho.empty() ? rgb(0x34c759) : ui::FAINT);
    float hx = hint(g, px + 48, py + ph-52, "A", XA, tr(K::Close));
    hint(g, hx, py + ph-52, "B", XB, tr(K::Back));
  }
  if (loginPanel || logoutPanel) {
    gfx::rect(g, 0, 0, ui::W, ui::H, {0, 0, 0, 0.78f});
    float pw = 880, ph = logoutPanel ? 330 : 488, px = (ui::W - pw) / 2, py = (ui::H - ph) / 2;
    gfx::shadow(g, px, py + 12, pw, ph, 28, 40, 0.7f);
    gfx::rect(g, px, py, pw, ph, ui::SURFACE, 28);
    gfx::hgrad(g, px + 48, py + 24, 56, 4, ui::A1, ui::A2, 2);
    gfx::text(g, ui::F_BIG, tr(logoutPanel ? K::SignOutTitle : K::SignInTitle), px + 48, py + 48, ui::TEXT);
    if (logoutPanel) {
      gfx::text(g, ui::F_KEY, gfx::ellipsize(g, ui::F_KEY, account.name, pw - 96), px + 48, py + 106, ui::A2);
      gfx::textBox(g, ui::F_BODY, gfx::ellipsize(g,ui::F_BODY,tr(K::SignOutBody),pw-96), px + 48, py + 152, pw-96, ui::MUTED);
      if (accountBusy) spinner(g, ui::W / 2, py + 218, 14, t);
      float hx = hint(g, px + 48, py + ph - 58, "A", XA, tr(K::Confirm));
      hint(g, hx, py + ph - 58, "B", XB, tr(K::Cancel));
    } else {
      gfx::text(g, ui::F_BODY, tr(K::SignInStep), ui::W / 2, py + 114, ui::MUTED, 1);
      gfx::text(g, ui::F_BIG, tr(K::DeviceURL), ui::W / 2, py + 148, ui::TEXT, 1);
      gfx::text(g, ui::F_SMALL, tr(K::EnterCode), ui::W / 2, py + 214, ui::MUTED, 1);
      if (!account.code.empty() && loginError != K::LoginExpired) {
        float w = gfx::textWidth(g, ui::F_CODE, account.code);
        gfx::textGrad(g, ui::F_CODE, account.code, (ui::W - w) / 2, py + 252, ui::A1, ui::A2);
      } else if (accountBusy) spinner(g, ui::W / 2, py + 286, 22, t);
      gfx::text(g, ui::F_BODY, tr(loginError != K::None ? loginError : K::LoginWaiting), ui::W / 2, py + 352, ui::MUTED, 1);
      float hx = px + 48;
      if (loginError != K::None) hx = hint(g, hx, py + ph - 58, "A", XA, tr(K::Refresh));
      hint(g, hx, py + ph - 58, "B", XB, tr(K::Back));
    }
  }
  if (searchUI.filterOpen) searchUI.drawFilters(g,settings.lang);
  playerNoticeDraw(now);
  if (!toast.empty() && now < toastUntil) {
    std::string shown = gfx::ellipsize(g, ui::F_BODY, toast, ui::W - 100);
    float w = gfx::textWidth(g, ui::F_BODY, shown) + 52;
    gfx::rect(g, ui::W / 2 - w / 2, 24, w, 46, ui::SURFACE2, 23);
    gfx::text(g, ui::F_BODY, shown, ui::W / 2, 36, ui::TEXT, 1);
  }

  if (sleepPanel || sleepWarning || shutdownAt) {
    gfx::rect(g, 0, 0, ui::W, ui::H, {0, 0, 0, 0.65f});
    float x = (ui::W-720)/2, y = sleepPanel && !sleepWarning && !shutdownAt ? 190 : 242, w = 720;
    float h = sleepPanel && !sleepWarning && !shutdownAt ? 252 : 226;
    gfx::shadow(g, x, y + 12, w, h, 24, 32, 0.6f);
    gfx::rect(g, x, y, w, h, ui::SURFACE, 24);
    std::string title = shutdownAt ? tr(K::SleepShuttingDown) : sleepWarning ? std::string(tr(K::SleepWarning)) +
        std::to_string((int)std::ceil(sleepLeft)) + tr(K::SleepSeconds) : tr(K::SleepTimer);
    gfx::text(g, ui::F_BIG, title, x + 36, y + 28, ui::TEXT);
    if (!shutdownAt) {
      if (!sleepWarning) {
        gfx::rect(g, x + 30, y + 90, w - 60, 62, ui::TEXT, 12);
        gfx::text(g, ui::F_KEY, sleepLabel(sleepChoice), x + 50, y + 108, ui::INK);
        gfx::text(g, ui::F_KEY, "<   >", x + w - 50, y + 108, ui::INK, 2);
        gfx::text(g, ui::F_SMALL, tr(K::SleepXbox), x + 36, y + 166, ui::MUTED);
      } else gfx::text(g, ui::F_KEY, tr(K::SleepXbox), x + 36, y + 92, ui::A2);
      float hy = y + h - 48, hx = x + 36;
      if (!sleepWarning) hx = hint(g, hx, hy, tr(K::BtnHorizontal), XG, tr(K::ChangeValue));
      hx = hint(g, hx, hy, "A", XA, tr(sleepWarning ? K::SleepAdd : K::SleepApply));
      hint(g, hx, hy, "B", XB, tr(K::Back));
    }
  }

  if ((screen==HOME && !phonePanel && !loginPanel && !logoutPanel && !nav.menu && !localUI.resume && localUI.clear<0 && !sleepPanel && !sleepWarning && !shutdownAt) || (screen==SETTINGS && updateUI.open)) updateUI.draw(g,settings.lang);

  if (g.arabicAttempted && !g.arabicAtlas.img) {
    gfx::rect(g,ui::X0,8,ui::W-ui::X0-32,88,ui::SURFACE2,14);
    float fy=18;
    for (const auto& line:gfx::wrap(g,ui::F_SMALL,i18n::get(settings.lang==i18n::Lang::AR ? i18n::Lang::EN : settings.lang,K::ArabicFontMissing),ui::W-ui::X0-64,2)) {
      gfx::text(g,ui::F_SMALL,line,ui::X0+16,fy,ui::TEXT); fy+=g.fonts[ui::F_SMALL].lineH+2;
    }
  }
}

// ---- entrada dos painéis por cima das telas (cada um captura o controle enquanto aberto)
inline void UiContext::sleepWarningInput() {
  if (in.hit(Input::A) && !sleepPending) requestSleep("/api/sleep/add?min=15");
  if (in.hit(Input::B) && !sleepPending) requestSleep("/api/sleep/cancel");
}

inline void UiContext::sleepPanelInput(uint64_t now) {
  int direction = in.hit(Input::LEFT) ? -1 : in.hit(Input::RIGHT) ? 1 : 0;
  sleepChoice = (sleepChoice + direction + 8) % 8;
  if (in.hit(Input::A) && !sleepPending) { applySleep(); sleepPanel = false; }
  if (in.hit(Input::B)) sleepPanel = false;
  playerUI.touch(now);
}

inline void UiContext::loginInput(uint64_t now) {
  if (in.hit(Input::B)) { loginPanel = false; accountAt = now + 60000; }
  if (in.hit(Input::A) && !accountBusy && loginError != K::None) {
    loginError = K::None;
    requestAccount(account.state == Account::PENDING ? "/api/account" : "/api/account/login");
  }
}

inline void UiContext::logoutInput() {
  if (in.hit(Input::B) && !accountBusy) logoutPanel = false;
  if (in.hit(Input::A) && !accountBusy) requestAccount("/api/account/logout");
}

inline void UiContext::phoneInput() {
  if (in.hit(Input::A) || in.hit(Input::B)) phonePanel = false;
}

inline void UiContext::filtersInput() {
  if (searchUI.filterInput(in) && screen==NAV && !nav.pages.empty() && nav.page().kind=="search") {
    auto& p=nav.page();
    if (p.filters!=searchUI.filters) { p.filters=searchUI.filters; p.filterLabel.clear(); p.sel=0; p.scroll=0; nav.load(navWorker,settings.lang); }
  }
}

inline void UiContext::localInput(uint64_t now) {
  if(in.hit(Input::UP))localUI.selected=0;
  if(in.hit(Input::DOWN))localUI.selected=1;
  if(in.hit(Input::B)){localUI.resume=false;localUI.clear=-1;}
  else if(in.hit(Input::A)){
    if(localUI.resume){auto v=localUI.pending;double pos=localUI.selected?0:localUI.resumeAt;localUI.resume=false;startVideo(v,pos,false);}
    else {if(localUI.selected){localUI.library.clear(localUI.clear==0);toast=tr(K::LocalSaving);toastUntil=now+2500;}localUI.clear=-1;}
  }
}

inline void UiContext::videoMenuInput(uint64_t now) {
  const auto entries = nav.menuEntries();
  if (in.hit(Input::UP) && nav.menuSel > 0) nav.menuSel--;
  if (in.hit(Input::DOWN) && nav.menuSel + 1 < (int)entries.size()) nav.menuSel++;
  nav.menuSel = std::min(nav.menuSel, (int)entries.size() - 1);
  if (in.hit(Input::B)) nav.menu = false;
  if (in.hit(Input::A) && !nav.busy) {
    const auto item = nav.menuItem;
    switch (entries[(size_t)nav.menuSel].kind) {
      case Navigation::MenuKind::Channel:
        nav.menu = false;
        if (!item.channelId.empty()) openNav("channel", item.channelId);
        else openChannelForVideo(item.id);
        break;
      case Navigation::MenuKind::Local:
        localUI.library.toggle(LocalLibraryUI::entry(item.video())); nav.menu = false; toast = tr(K::LocalSaving); toastUntil = now + 2500;
        break;
      case Navigation::MenuKind::Later:
        nav.action(navWorker, "/api/nav/action?action=later&id=" + item.id + "&lang=" + i18n::code(settings.lang));
        break;
      case Navigation::MenuKind::Radio: nav.menu = false; openNav("radio", item.id, "", true); break;
      case Navigation::MenuKind::Refresh: nav.menu = false; refreshPage(now); break;
    }
  }
}

// "Atualizar" não é mais um botão: vive no Menu (#40). Reproduz o que o X fazia no Início e nas páginas de navegação.
inline void UiContext::refreshPage(uint64_t now) {
  if (screen == HOME) {
    Shelf& cur = inResults ? results : cats[(size_t)cat];
    cur.feed = Feed{}; requestFeed(cur, inResults ? 1 : cat == 0 ? 0 : 3, true, 0, true);
    cur.sel = 0; scroll = 0; zone = 0; toast = tr(K::Refreshing); toastUntil = now + 2500;
  } else if (screen == NAV && !nav.pages.empty()) {
    nav.page().sel = 0; nav.page().scroll = 0; nav.load(navWorker, settings.lang, true);
  }
}

inline void UiContext::openPageMenu() { nav.menuItem = NavItem{}; nav.menuSel = 0; nav.menu = true; }

// Páginas vazias ou com erro não têm vídeo em foco: o Menu abre só as opções da página. Roda depois das telas, que
// consomem o Menu quando há vídeo em foco.
inline void UiContext::pageMenuFallback() {
  const int menuKey = ctl::inputIndex<Input>(ctl::button(ctl::Ctx::Collection, ctl::Act::Options));
  if (!in.fired[menuKey]) return;
  const bool page = (screen == HOME || (screen == NAV && !nav.pages.empty())) && !railFocus && !nav.menu && !nav.unsubscribe &&
      !phonePanel && !loginPanel && !logoutPanel && !sleepPanel && !shutdownAt && !searchUI.filterOpen && !localUI.resume && localUI.clear < 0;
  if (page) { in.fired[menuKey] = false; openPageMenu(); }
}

inline void UiContext::unsubscribeInput() {
  if (in.hit(Input::B)) nav.unsubscribe = false;
  if (in.hit(Input::A) && !nav.busy) {
    nav.unsubscribe = false;
    nav.action(navWorker, "/api/nav/action?action=subscribe&id=" + nav.page().channelId + "&value=0&lang=" + i18n::code(settings.lang));
  }
}

// ---- o que as páginas de navegação têm em comum (Navigation::draw escolhe a página; aqui o fundo, o vazio e o rodapé)
inline void Navigation::drawBanner(NavDraw& d) {
  auto& g = d.g; auto& p = d.p;
  if (p.banner.empty()) return;
  if (const auto* cover = d.thumbs.get(d.worker, p.banner, d.frame, p.direct, true)) {
    gfx::image(g, cover->tex, 96, 0, Thumbs::bannerWidth, Thumbs::bannerHeight);
    gfx::hgrad(g, 96, 0, 780, Thumbs::bannerHeight, {0,0,0,0.55f}, {0,0,0,0});
    gfx::vgrad(g, 96, 0, Thumbs::bannerWidth, Thumbs::bannerHeight, {0,0,0,0.10f}, ui::BG);
  }
}

inline void Navigation::drawEmpty(NavDraw& d, float top) {
  auto& g = d.g; auto& p = d.p;
  bool home = p.musicHome(), channel = p.kind == "channel";
  if ((home ? p.items.empty() : p.rows.empty()) && !p.loading && !p.more) {
    std::string msg = !p.error.empty() ? p.error : d.tr(channel && p.tab == "live" ? K::NoLive : K::Empty);
    float y = top + 42;
    for (const auto& line : gfx::wrap(g, ui::F_H2, msg, ui::W-200, 3)) { gfx::textBox(g, ui::F_H2, line, 136, y, ui::W-200, ui::MUTED); y += 34; }
  }
}

inline void Navigation::drawFooter(NavDraw& d) {
  auto& g = d.g; auto& p = d.p;
  auto tr = [&](K k) { return d.tr(k); };
  bool home = p.musicHome(), channel = p.kind == "channel", playlist = playlistKind(p);
  gfx::rect(g, 96, 646, ui::W-96, 74, ui::BG);
  if (p.loading) { spinner(g, ui::W-64, 146, 12, d.now / 1000.0); gfx::text(g, ui::F_CHAN, tr(p.offset ? K::MoreLoading : K::Loading), ui::W-324, 130, ui::MUTED); }
  const ctl::Ctx ctx = p.music || home ? ctl::Ctx::Music : channel ? ctl::Ctx::Channel : p.kind == "search" ? ctl::Ctx::Search : ctl::Ctx::Collection;
  float hx = ctlHint(d.g, 136, 676, ctx, ctl::Act::Open, d.lang, p.zone == 0 ? channel ? p.subscribed ? K::Subscribed : K::Subscribe : playlist ? K::PlayAll : K::Open : K::Open);
  hx = ctlHint(d.g, hx, 676, ctx, ctl::Act::Back, d.lang);
  // O X ainda não tem a dica: a F61 troca o X de atualizar por "Assistir mais tarde" e põe a dica de volta. O Menu abre as
  // opções do vídeo em foco ou, sem vídeo (página vazia ou com erro), as da página, e "Atualizar" mora nele.
  hx = ctlHint(d.g, hx, 676, ctx, ctl::Act::Options, d.lang, K::MoreOptions);
  ctlHint(d.g, hx, 676, ctx, ctl::Act::Search, d.lang);
}

inline void Navigation::drawMenu(gfx::Gfx& g, i18n::Lang lang) {
  auto tr = [&](K k) { return i18n::get(lang, k); };
  const auto entries = menuEntries();
  const bool page = pageMenu();
  const float rowTop = page ? 236.f : 320.f, rowEnd = rowTop + (entries.size() - 1) * 60 + 50, boxH = rowEnd + 64 - 150, left = (ui::W-740)/2;
  gfx::rect(g, 0, 0, ui::W, 720, {0,0,0,0.8f});
  gfx::rect(g, left, 150, 740, boxH, ui::SURFACE, 28);
  gfx::text(g, ui::F_BIG, tr(K::CardMenu), left+38, 180, ui::TEXT);
  if (!page) {
    gfx::text(g, ui::F_KEY, gfx::ellipsize(g, ui::F_KEY, menuItem.title, 662), left+38, 235, ui::TEXT);
    gfx::text(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, menuItem.channel + " · " + fmtTime(menuItem.duration), 662), left+38, 272, ui::MUTED);
  }
  for (size_t i = 0; i < entries.size(); i++) {
    const float y = rowTop + i * 60;
    gfx::rect(g, left+34, y, 672, 50, (int)i == menuSel ? ui::TEXT : ui::SURFACE2, 16);
    gfx::text(g, ui::F_KEY, tr(entries[i].label), left+58, y + 14, (int)i == menuSel ? ui::INK : ui::TEXT);
  }
  const float hy = rowEnd + 24;
  float hx = ctlHint(g, left+38, hy, ctl::Ctx::Panel, ctl::Act::Open, lang);
  ctlHint(g, hx, hy, ctl::Ctx::Panel, ctl::Act::Back, lang);
}
