#pragma once
#include "cast/discovery.hpp"

struct SpeakerPicker {
  bool open = false;
  int selected = 0;
  nest::SearchResult result;
  void begin(nest::Discovery& finder) { open=true; selected=0; finder.search(); finder.poll(result); }
  bool poll(nest::Discovery& finder) {
    bool changed=finder.poll(result);
    selected=std::clamp(selected,0,(int)result.devices.size());
    return changed;
  }
  nest::Config choice() const { return selected ? result.devices[selected-1] : nest::Config{}; }
  void draw(gfx::Gfx& g,i18n::Lang lang,const nest::Config& current) {
    auto tr=[&](K k) { return i18n::get(lang,k); };
    float x=ui::X0,w=ui::W-x-64;
    gfx::text(g,ui::F_BIG,gfx::ellipsize(g,ui::F_BIG,tr(K::SearchSpeakers),w),x,32,ui::TEXT);
    bool busy=result.state==nest::SearchState::Searching;
    gfx::text(g,ui::F_SMALL,gfx::ellipsize(g,ui::F_SMALL,tr(busy ? K::SpeakerSearching : K::SpeakerSearchHelp),w),x,92,ui::MUTED);
    if (busy) {
      gfx::rect(g,x,120,w,3,ui::SURFACE2,1);
      float travel=float(SDL_GetTicks()%1400)/1400;
      gfx::hgrad(g,x+travel*(w-160),120,160,3,ui::A1,ui::A2);
    }
    int visible=ui::large ? 4 : 5, first=std::max(0,selected-visible+1), count=(int)result.devices.size()+1;
    float step=ui::large ? 110 : 86;
    for (int i=first;i<std::min(count,first+visible);++i) {
      float y=142+(i-first)*step,h=step-9;
      if (i==selected) gfx::rect(g,x-3,y-3,w+6,h+6,ui::A1,20);
      gfx::rect(g,x,y,w,h,i==selected ? ui::SURFACE2 : ui::SURFACE,18);
      const nest::Config* device=i ? &result.devices[i-1] : nullptr;
      bool active=device ? !current.host.empty() && (current.uuid.empty() ? device->host==current.host : device->uuid==current.uuid) : current.host.empty();
      float badge=active ? gfx::textWidth(g,ui::F_SMALL,tr(K::SpeakerSelected))+36 : 0;
      gfx::text(g,ui::F_BODY,gfx::ellipsize(g,ui::F_BODY,device ? device->name : tr(K::SpeakerNone),w-56-badge),x+26,y+12,ui::TEXT);
      if (device) gfx::text(g,ui::F_SMALL,gfx::ellipsize(g,ui::F_SMALL,device->model+"  ·  "+device->host,w-56),x+26,y+(ui::large ? 60 : 44),ui::MUTED);
      if (active) gfx::text(g,ui::F_SMALL,tr(K::SpeakerSelected),x+w-24,y+17,ui::A2,2);
    }
    K status=busy ? K::SpeakerSearching : result.state==nest::SearchState::NetworkError ? K::SpeakerNetworkError : result.state==nest::SearchState::Incomplete ? K::SpeakerIncomplete : result.state==nest::SearchState::SaveError ? K::SaveFailed : result.state==nest::SearchState::Cancelled ? K::SpeakerCancelled : result.devices.empty() ? K::SpeakerEmpty : K::SavedAutomatically;
    float y=602;
    for (const auto& line:gfx::wrap(g,ui::F_SMALL,tr(status),w,2)) { gfx::text(g,ui::F_SMALL,line,x,y,ui::MUTED); y+=ui::large ? 26 : 22; }
    float hx=hint(g,x,678,tr(K::BtnVertical),XG,tr(K::ChooseRow));
    hx=hint(g,hx,678,"A",XA,tr(K::SpeakerChoose));
    hx=hint(g,hx,678,"X",XX,tr(K::SpeakerSearchAgain));
    hint(g,hx,678,"B",XB,tr(K::Back));
  }
};
