#pragma once
#include "service.hpp"

namespace updates {
struct UI {
    Status status;
    unsigned revision=0;
    bool open=false,large=false;
    string title,banner,installed,size;
    float width=0;
    std::vector<string> messages;
    std::vector<string> notes;
    i18n::Lang language=i18n::Lang::EN;
    void poll(Service& service,gfx::Gfx& g,i18n::Lang lang) {
        unsigned next=service.poll(status,revision);
        if(next!=revision || lang!=language || width!=ui::W || large!=ui::large) {
            revision=next;language=lang;width=ui::W;large=ui::large;
            float content=std::min(712.f,ui::W-160);
            installed=gfx::ellipsize(g,ui::F_SMALL,string(i18n::get(lang,K::UpdateInstalled))+appVersion+" · #"+std::to_string(buildId),content);
            char bytes[48];std::snprintf(bytes,sizeof(bytes),"%.1f MiB",status.app.size/1048576.);
            size=bytes;
            auto full=status.app.build?string(i18n::get(lang,K::UpdateAvailable))+status.app.version:string(i18n::get(lang,K::Updates));
            title=gfx::ellipsize(g,ui::F_H2,full,content);
            auto notice=status.state==State::READY?string(i18n::get(lang,K::UpdateReady)):full;
            if(status.state!=State::READY)notice=string(i18n::get(lang,K::UpdateToastHead))+status.app.version+i18n::get(lang,K::UpdateToastTail);
            banner=gfx::ellipsize(g,ui::F_SMALL,notice,std::min(476.f,ui::W-160)-32);
            notes=gfx::wrap(g,ui::F_BODY,lang==i18n::Lang::PT?status.app.notesPt:status.app.notesEn,content,2);
            K key=status.state==State::IDLE?K::UpdateCurrent:status.state==State::CHECK_FAILED?K::UpdateCheckFailed:status.state==State::CHECKING?K::UpdateChecking:status.state==State::READY?K::UpdateReady:status.state==State::ERROR?(status.error=="setup"?K::UpdateSetup:K::UpdateFailed):status.state==State::DOWNLOADING?K::UpdateDownloading:K::UpdateNextStart;
            messages=gfx::wrap(g,ui::F_SMALL,i18n::get(lang,key),content,3);
        }
    }
    string settingsValue(i18n::Lang) const {
        return status.app.build?status.app.version:string(appVersion);
    }
    void show(Service& service) {open=true;service.requestCheck();}
    bool input(Service& service,Input& in,bool onHome,bool inSettings=false) {
        if(!onHome && !inSettings){open=false;return false;}
        if(!open)return false;
        if(ctl::hit(in,ctl::Ctx::Panel,ctl::Act::Back)){service.cancelDownload();open=false;}
        else if(ctl::hit(in,ctl::Ctx::Panel,ctl::Act::Open)){
            if(status.state==State::AVAILABLE || status.state==State::ERROR)service.requestDownload();
            else if(status.state==State::IDLE || status.state==State::CHECK_FAILED)service.requestCheck();
        }
        return true;
    }
    void draw(gfx::Gfx& g,i18n::Lang lang) {
        auto tr=[&](K k){return i18n::get(lang,k);};
        if(!open) {
            if(!status.app.build)return;
            float w=std::min(476.f,ui::W-160);
            gfx::rect(g,ui::W-w-40,588,w,56,ui::SURFACE2,16);
            gfx::text(g,ui::F_SMALL,banner,ui::W-w-24,607,ui::MUTED);
            return;
        }
        gfx::rect(g,0,0,ui::W,720,{0,0,0,0.78f});
        float panel=std::min(792.f,ui::W-80),left=(ui::W-panel)/2,content=panel-80;
        float line=std::max(25.f,g.fonts[ui::F_BODY].lineH);
        gfx::shadow(g,left,106,panel,520,24,32,0.6f);
        gfx::rect(g,left,96,panel,520,ui::SURFACE,24);
        gfx::rect(g,left+40,124,60,4,ui::A1,2);
        gfx::text(g,ui::F_SMALL,installed,left+40,148,ui::MUTED);
        gfx::text(g,ui::F_H2,title,left+40,196,ui::TEXT);
        if(status.app.build)gfx::text(g,ui::F_SMALL,size,left+40,250,ui::A2);
        float y=298;
        for(const auto& text:notes){gfx::textBox(g,ui::F_BODY,text,left+40,y,content,ui::MUTED);y+=line;}
        y=410;
        for(const auto& text:messages){gfx::textBox(g,ui::F_SMALL,text,left+40,y,content,ui::A2);y+=std::max(24.f,g.fonts[ui::F_SMALL].lineH);}
        if(status.state==State::DOWNLOADING){gfx::rect(g,left+40,526,content,4,ui::SURFACE2,2);gfx::rect(g,left+40,526,content*status.progress/100.f,4,ui::A1,2);}
        float x=left+40;
        if(status.state==State::AVAILABLE || status.state==State::ERROR)x=ctlHint(g,x,564,ctl::Ctx::Panel,ctl::Act::Open,lang,K::UpdateDownload);
        else if(status.state==State::IDLE || status.state==State::CHECK_FAILED)x=ctlHint(g,x,564,ctl::Ctx::Panel,ctl::Act::Open,lang,K::Refresh);
        ctlHint(g,x,564,ctl::Ctx::Panel,ctl::Act::Back,lang,status.state==State::DOWNLOADING?K::Cancel:K::Back);
    }
};
}
