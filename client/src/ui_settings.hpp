// Configurações: as linhas de ajuste, o seletor de caixas de som e o SponsorBlock (entrada e desenho da tela SETTINGS).
#pragma once
#include "ui_context.hpp"

// Seções da tela (proposta A8): cada seção lista os ids das linhas; o id é o índice em `labels` de settingsDraw.
namespace settings_ui {
struct Section { K title; std::vector<int> rows; };
inline const std::vector<Section>& sections() {
  static const std::vector<Section> all = {
    {K::SecAccount, {0}}, {K::SecVideo, {2,9,12,10,15,7,19}}, {K::SecSound, {3,4,13,18}}, {K::SecScreen, {5,1,11}},
    {K::SecTimer, {6}}, {K::SecPrivacy, {8,16,17,20}}, {K::SecSystem, {14}}};
  return all;
}
inline int sectionOf(int row) {
  const auto& all = sections();
  for (size_t i = 0; i < all.size(); ++i) for (int id : all[i].rows) if (id == row) return (int)i;
  return 0;
}
inline bool changesWithArrows(int row) { return (row > 0 && row < 13) || row == 18 || row == 19 || row == 20; }
}  // namespace settings_ui

inline std::optional<int> UiContext::settingsInput(uint64_t now) {
  if (sponsorUI.open) {
    if (sponsorUI.input(in,settings.sponsorBlock)) saveSettings();
  } else if (speakerPicker.open) {
    if (in.hit(Input::UP)) speakerPicker.selected = std::max(0,speakerPicker.selected-1);
    if (in.hit(Input::DOWN)) speakerPicker.selected = std::min((int)speakerPicker.result.devices.size(),speakerPicker.selected+1);
    if (ctl::hit(in, ctl::Ctx::Panel, ctl::Act::Search)) speakerPicker.begin(pl.directSpeaker.finder());
    if (in.hit(Input::B)) { pl.directSpeaker.finder().cancel(); speakerPicker.open=false; }
    else if (in.hit(Input::A)) {
      auto choice = speakerPicker.choice();
      if (pl.directSpeaker.finder().choose(choice)) {
        speakerChoice=choice; nestAvailable=!choice.host.empty(); nestChecked=true;
        nestName=nestAvailable ? choice.name : tr(K::Nest); nestOut=nestAvailable;
        nestSkip=false; saveSettings(); speakerPicker.open=false;
      } else { toast=tr(K::SaveFailed); toastUntil=now+4000; }
    }
  } else {
    {
      const auto& all = settings_ui::sections();
      int section = settings_ui::sectionOf(settingsRow);
      const auto& rows = all[(size_t)section].rows;
      int index = (int)(std::find(rows.begin(), rows.end(), settingsRow) - rows.begin());
      if (in.hit(Input::UP) && index > 0) settingsRow = rows[(size_t)index-1];
      if (in.hit(Input::DOWN) && index + 1 < (int)rows.size()) settingsRow = rows[(size_t)index+1];
      int jump = ctl::hit(in, ctl::Ctx::Settings, ctl::Act::PrevTab) ? -1 : ctl::hit(in, ctl::Ctx::Settings, ctl::Act::NextTab) ? 1 : 0;
      if (jump) settingsRow = all[(size_t)std::clamp(section + jump, 0, (int)all.size()-1)].rows.front();
    }
    int direction = 0;
    if (in.hit(Input::LEFT)) direction = -1;
    if (in.hit(Input::RIGHT)) direction = 1;
    if (settingsRow == 0 && !accountBusy && in.hit(Input::A)) {
      if (account.signedIn()) logoutPanel = true;
      else beginLogin();
    }
    if (settingsRow == 13 && in.hit(Input::A)) speakerPicker.begin(pl.directSpeaker.finder());
    if (settingsRow == 14 && in.hit(Input::A)) updateUI.show(updater);
    if (settingsRow == 15 && in.hit(Input::A)) sponsorUI.open=true;
    if (settingsRow == 20 && ctl::hit(in, ctl::Ctx::Settings, ctl::Act::Open)) { if (!settings.phoneRemote) { settings.phoneRemote = true; saveSettings(); } remotePanelOpen = true; }
    if ((settingsRow==16 || settingsRow==17) && in.hit(Input::A)){localUI.clear=settingsRow-16;localUI.selected=0;}
    if (direction && settings_ui::changesWithArrows(settingsRow)) {
      bool changed = true;
      if (settingsRow == 1) {
        settings.cols = settings.cols == 4 ? 3 : 4;
        thumbs.configure(g, thumbsWorker, ui::columns(settings.cols)); scroll = 0;
      } else if (settingsRow == 2) {
        bool videoActive=pl.state!=player::IDLE && !pl.playingAudio;
        if(videoActive && !qualityKnown)changed=false;
        else {
          const auto& choices=videoActive?offeredQualities:defaultQualities;
          settings.videoMode=choices.cycle(settings.videoMode,direction);
          if(videoActive && !playing.id.empty()) {
            bool paused=pl.paused;startVideo(playing,pl.live?0:pl.position(),playerFromRemote);pl.setPaused(paused);screen=SETTINGS;
          }
        }
      } else if (settingsRow == 3) {
        if (nestAvailable) { nestOut = !nestOut; if (nestOut && !pl.directSpeaker.configured()) control.add({5, "/api/nest/prepare", 0}); }
        else changed = false;
      } else if (settingsRow == 4) nestOffsetMs = std::clamp(nestOffsetMs + direction * 40, -2000, 2000);
      else if (settingsRow == 5) {
        settings.lang = i18n::cycle(settings.lang,direction);
        boot_event("info","settings.language","lang=%s; bundled Latin atlas unchanged",i18n::code(settings.lang));
        g.rtlText = i18n::rtl(settings.lang);
        rebuildCats(); scroll = 0; toast.clear();
      } else if (settingsRow == 6) { sleepChoice = (sleepChoice + direction + 8) % 8; applySleep(); changed = false; }
      else if (settingsRow == 7) settings.hideShorts = !settings.hideShorts;
      else if (settingsRow == 8) settings.errorReports = !settings.errorReports;
      else if (settingsRow == 9) {
        const int heights[]={480,720,1080};
        int index=settings.outputHeight==480 ? 0 : settings.outputHeight==1080 ? 2 : 1;
        settings.outputHeight=heights[(index+direction+3)%3];
        if(!gfx::fixedOutput())pl.resetQualityMeasurements();
        bool changed=gfx::output(g,settings.outputHeight);
        if(!changed && (!gfx::fixedOutput() || g.outputRejected))return startup::fail(K::StartupVideo,"board-sim: output rejected before GPU teardown");
        if(changed && !loadDisplay())return startup::fail(K::StartupFonts,"font atlas",K::StartupCopy);
        if(!gfx::fixedOutput())pl.resetQualityMeasurements();
        scroll=0; nav.nowId.clear();
        overlayTitle = {}; overlayMeta = {}; overlayChapter = {}; tipLabel = {}; tipValue = {};
        metaChannel.clear(); clockSecond = -1;
        if ((int)g.extent.height != settings.outputHeight) { toast=tr(gfx::fixedOutput()?K::SafeOutputHelp:K::Display1080Help); toastUntil=now+6000; }
      }
      else if (settingsRow == 10) settings.originalAudio=!settings.originalAudio;
      else if (settingsRow == 11) {
        settings.largeText=!settings.largeText;
        pl.resetQualityMeasurements();
        if (!loadDisplay()) return startup::fail(K::StartupFonts,"font atlas",K::StartupCopy);
        pl.resetQualityMeasurements();
        overlayTitle={}; overlayMeta={}; overlayChapter={}; tipLabel={}; tipValue={};
        metaChannel.clear(); clockSecond=-1; scroll=0; nav.nowId.clear();
      }
      else if (settingsRow == 18) {
        settings.volume = std::clamp(settings.volume + direction * 5, 0, 100);  // só o volume da TV; a caixa (Nest) é ajustada ao vivo no player
        pl.setVolumePercent(settings.volume);
      }
      else if (settingsRow == 20) { settings.phoneRemote = !settings.phoneRemote; if (!settings.phoneRemote) remotePanelOpen = false; }
      else if (settingsRow == 19) settings.liveLowLatency = !settings.liveLowLatency;
      else if (settingsRow == 12) {settings.videoFill=!settings.videoFill;boot_event("info","settings.fit","fill=%d; geometry/UV only, no display or atlas recreation",settings.videoFill);}
      if (changed) saveSettings();
    }
    if (in.hit(Input::B)) { screen = HOME; railFocus = true; railSel = 6; }
  }
  return std::nullopt;
}

inline void UiContext::settingsDraw() {
  if (sponsorUI.open) { glow(); sponsorUI.drawSettings(g,settings.sponsorBlock,settings.lang); }
  else if (speakerPicker.open) { glow(); speakerPicker.draw(g,settings.lang,speakerChoice); }
  else {
    glow();
    gfx::textBox(g, ui::F_BIG, tr(K::Settings), ui::X0, 32, ui::W-ui::X0-64, ui::TEXT);
    if (!ui::large) gfx::textBox(g, ui::F_BODY, tr(K::SettingsIntro), ui::X0, 89, ui::W-ui::X0-64, ui::MUTED);
    else gfx::textBox(g,ui::F_SMALL,gfx::ellipsize(g,ui::F_SMALL,tr(K::SavedAutomatically),ui::W-ui::X0-64),ui::X0,82,ui::W-ui::X0-64,ui::MUTED);
    const K labels[] = {K::YouTubeAccount,K::VideosPerRow,K::DefaultQuality,K::Sound,K::SpeakerSync,K::Language,K::SleepTimer,K::SettingShowShorts,K::SettingErrorReports,K::DisplayOutput,K::AudioPreference,K::LargeText,K::VideoFit,K::SearchSpeakers,K::Updates,K::SponsorBlock,K::ClearLocalHistory,K::ClearLocalLater,K::SettingVolume,K::SettingLiveLowLatency,K::SettingPhoneRemote};
    const auto& all=settings_ui::sections();
    int section=settings_ui::sectionOf(settingsRow);
    // Seções à esquerda, opções da seção atual à direita.
    // A coluna de seções se ajusta ao maior nome (com teto) e espelha em árabe: seções à direita, opções à esquerda.
    float titleW=0;
    for (const auto& sec:all) titleW=std::max(titleW,gfx::textWidth(g,ui::F_CHAN,tr(sec.title)));
    const bool rtl=g.rtlText;
    float listW=std::clamp(titleW+40,170.f,ui::W*(ui::large ? .34f : .30f)), gap=28, edge=64;
    float listX=rtl ? ui::W-edge-listW : ui::X0, optX=rtl ? ui::X0 : listX+listW+gap, optW=rtl ? listX-gap-ui::X0 : ui::W-optX-edge;
    for (size_t i=0;i<all.size();++i) {
      float y=122+i*(ui::large ? 56 : 62);
      bool current=(int)i==section;
      if (current) gfx::rect(g,listX,y,listW,ui::large ? 50 : 54,ui::TEXT,14);
      std::string name=gfx::ellipsize(g,ui::F_CHAN,tr(all[i].title),listW-36);
      gfx::text(g,ui::F_CHAN,name,rtl ? listX+listW-18 : listX+18,y+(ui::large ? 10 : 13),current ? ui::INK : ui::MUTED,rtl ? 2 : 0);
    }
    const auto& rows=all[(size_t)section].rows;
    int visible=ui::large ? 5 : 8, selectedIndex=(int)(std::find(rows.begin(),rows.end(),settingsRow)-rows.begin());
    int count=(int)rows.size(), first=std::clamp(selectedIndex-visible+1,0,std::max(0,count-visible));
    float step=ui::large ? 94 : 59, rowH=step-6;
    for (int k=first;k<std::min(count,first+visible);++k) {
      int i=rows[(size_t)k];
      float x=optX, y=122+(k-first)*step, w=optW;
      bool selected=i==settingsRow, disabled=i==3 && !nestAvailable;
      if (selected) gfx::rect(g,x-3,y-3,w+6,rowH+6,ui::A1,20);
      gfx::rect(g,x,y,w,rowH,selected ? ui::SURFACE2 : ui::SURFACE,18);
      std::string value;
      if (i==0) value=accountFailed ? tr(K::AccountUnavailable) : !accountKnown && accountBusy ? tr(K::Loading) : account.signedIn() ? tr(K::SignedInAs)+account.name : account.state==Account::PENDING ? tr(K::Pending) : tr(K::SignInAction);
      else if (i==1) value=tr(ui::columns(settings.cols)==2 ? K::Two : ui::columns(settings.cols)==3 ? K::Three : K::Four);
      else if (i==2) value=tr(videoModeLabel(settings.videoMode));
      else if (i==3) value=nestAvailable && nestOut ? nestName : tr(K::TV);
      else if (i==4) value=(nestOffsetMs>0 ? "+" : "")+std::to_string(nestOffsetMs)+tr(K::Milliseconds);
      else if (i==5) value=tr(settings.lang==i18n::Lang::AR ? K::Arabic : settings.lang==i18n::Lang::ES ? K::Spanish : settings.lang==i18n::Lang::PT ? K::Portuguese : K::English);
      else if (i==6) value=sleepLabel(sleepChoice);
      else if (i==7) value=tr(settings.hideShorts ? K::SettingShortsHide : K::SettingShortsRow);
      else if (i==8) value=tr(settings.errorReports ? K::SettingOn : K::SettingOff);
      else if (i==9) value=std::to_string(g.extent.width)+"×"+std::to_string(g.extent.height);
      else if (i==10) value=tr(settings.originalAudio ? K::AudioAlwaysOriginal : K::AudioInterface);
      else if (i==11) value=tr(ui::large ? K::Enabled : K::SleepOff);
      else if (i==13) value=tr(K::Open);
      else if (i==14) value=updateUI.settingsValue(settings.lang);
      else if (i==19) value=tr(settings.liveLowLatency ? K::SettingOn : K::SettingOff);
      else if (i==18) value=std::to_string(settings.volume)+"%";
      else if (i==20) value=tr(settings.phoneRemote ? K::SettingOn : K::SettingOff);
      else if(i>=16)value=i18n::localCount(settings.lang,i==16?localUI.library.history.size():localUI.library.later.size(),i==16);
      else if (i==15) value=tr(sponsorblock::modeLabel(settings.sponsorBlock));
      else value=tr(settings.videoFill ? K::VideoFitFill : K::VideoFitLetterbox);
      float right=x+w-66, maxValue=ui::large ? w-112 : w*.48f;
      gfx::textBox(g,ui::F_CHAN,gfx::ellipsize(g,ui::F_CHAN,tr(labels[i]),ui::large ? w-56 : w*.43f),x+28,y+(ui::large ? 10 : 17),ui::large ? w-56 : w*.43f,disabled ? ui::MUTED : ui::TEXT);
      std::string shown=gfx::ellipsize(g,ui::F_KEY,value,maxValue);
      gfx::text(g,ui::F_KEY,shown,ui::large && !g.rtlText ? x+28 : right,y+(ui::large ? 47 : 15),disabled ? ui::MUTED : selected ? ui::A2 : ui::TEXT,ui::large && !g.rtlText ? 0 : 2);
      if (selected && settings_ui::changesWithArrows(i) && !disabled) gfx::text(g,ui::F_KEY,"< >",x+w-20,y+(ui::large ? 47 : 15),ui::MUTED,2);
    }
    K help=(settingsRow==16 || settingsRow==17)?K::LocalLimit:settingsRow==2?K::ManualQualityHelp:settingsRow==9 ? (gfx::fixedOutput()?K::SafeOutputHelp:g.extent.height<=576 ? K::DisplaySDHelp : K::Display1080Help) : settingsRow==11 || settingsRow==1 ? K::LargeTextHelp : settingsRow==4 ? K::SyncHelp : settingsRow==19 ? K::LiveLowLatencyHelp : settingsRow==20 ? K::PhoneRemoteHelp : settingsRow==3 && !nestAvailable ? (nestChecked ? K::NoSpeaker : K::SpeakerChecking) : K::SavedAutomatically;
    float hy=ui::large ? 608 : 610;
    // Com texto grande o "salvas automaticamente" já está no topo, no lugar do subtítulo; o rodapé só mostra ajuda própria da linha.
    if (!(ui::large && help==K::SavedAutomatically))
      for (const auto& line:gfx::wrap(g,ui::F_SMALL,tr(help),ui::W-ui::X0-64,2)) { gfx::textBox(g,ui::F_SMALL,line,ui::X0,hy,ui::W-ui::X0-64,ui::MUTED); hy+=ui::large ? 26 : 22; }
    float hx=hint(g,ui::X0,678,tr(K::BtnVertical),XG,tr(K::ChooseRow));
    if (settings_ui::changesWithArrows(settingsRow)) hx=hint(g,hx,678,tr(K::BtnHorizontal),XG,tr(K::ChangeValue));
    hx=ctlHint(g,hx,678,ctl::Ctx::Settings,ctl::Act::PrevTab,settings.lang,K::CtlSection);
    if (!settings_ui::changesWithArrows(settingsRow)) hx=ctlHint(g,hx,678,ctl::Ctx::Settings,ctl::Act::Open,settings.lang);
    ctlHint(g,hx,678,ctl::Ctx::Settings,ctl::Act::Back,settings.lang);
  }
}
