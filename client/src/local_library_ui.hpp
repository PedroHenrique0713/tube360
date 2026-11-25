#pragma once
#include "local_library_worker.hpp"
struct LocalLibraryUI {
  local::Library library;
  local::Writer writer;
  bool corrupt=false, resume=false;
  int clear=-1, selected=0;
  Video pending;
  double resumeAt=0;
  uint64_t submitted=0,completed=0,checkpoint=0;
  explicit LocalLibraryUI(const std::string& assets):writer(assets+"local-library.json") {
    corrupt=library.load(assets+"local-library.json")==local::Library::Corrupt;
    submitted=library.revision;
  }
  static local::Entry entry(const Video& v){return {v.id,v.title,v.channel,v.dur,v.pos};}
  void record(const Video& v, double pos, double duration, bool ended) {
    auto e=entry(v);e.position=pos;e.duration=int(std::clamp(duration,0.0,604800.0));library.record(std::move(e),ended);
  }
  bool tick(std::string& toast, uint64_t& until, uint64_t now, i18n::Lang lang) {
    if(submitted!=library.revision){submitted=library.revision;writer.submit(library);}
    bool ok;
    if(!writer.poll(completed,ok))return false;
    LOG("local: save revision=%llu ok=%d later=%u history=%u",(unsigned long long)completed,ok,unsigned(library.later.size()),unsigned(library.history.size()));
    toast=i18n::get(lang,ok?K::LocalSaved:K::LocalSaveFailed);until=now+(ok?2500:7000);return true;
  }
  void draw(gfx::Gfx& g,i18n::Lang lang) {
    if(!resume && clear<0)return;
    auto tr=[&](K k){return i18n::get(lang,k);};
    float w=std::min(760.f,ui::W-160),x=(ui::W-w)/2,y=154;
    gfx::rect(g,0,0,ui::W,720,{0,0,0,.86f});gfx::rect(g,x,y,w,430,ui::SURFACE,24);
    float ty=y+28;
    K title=resume?K::ResumeLocal:clear==0?K::ClearLocalHistory:K::ClearLocalLater;
    for(const auto& line:gfx::wrap(g,ui::F_H2,tr(title),w-64,2)){gfx::textBox(g,ui::F_H2,line,x+32,ty,w-64,ui::TEXT);ty+=38;}
    std::string detail=resume?pending.title:tr(K::ConfirmLocalClear);
    ty=y+112;
    for(const auto& line:gfx::wrap(g,ui::F_KEY,detail,w-64,2)){gfx::textBox(g,ui::F_KEY,line,x+32,ty,w-64,ui::MUTED);ty+=ui::large?34:28;}
    if(resume)gfx::text(g,ui::F_CHAN,fmtTime(resumeAt),x+32,y+196,ui::A2);
    for(int i=0;i<2;++i){float by=y+236+i*66;gfx::rect(g,x+26,by,w-52,56,i==selected?ui::TEXT:ui::SURFACE2,16);
      K key=resume?(i==0?K::ContinueWatching:K::StartBeginning):(i==0?K::Back:K::StartupExit);
      // A ação destrutiva usa o rótulo da própria configuração.
      const char* label=!resume && i==1?tr(clear==0?K::ClearLocalHistory:K::ClearLocalLater):tr(key);
      gfx::textBox(g,ui::F_KEY,gfx::ellipsize(g,ui::F_KEY,label,w-100),x+48,by+14,w-96,i==selected?ui::INK:ui::TEXT);
    }
    float hx=hint(g,x+32,y+384,"A",XA,tr(K::Open));hint(g,hx,y+384,"B",XB,tr(K::Back));
  }
};
