#pragma once
#include "service.hpp"

namespace updates {
struct UI {
    Status status;
    unsigned revision=0;
    bool open=false,large=false;
    string title,banner;
    float width=0;
    std::vector<string> messages;
    std::vector<string> notes;
    i18n::Lang language=i18n::Lang::EN;
    void poll(Service& service,gfx::Gfx& g,i18n::Lang lang) {
        unsigned next=service.poll(status,revision);
        if(next!=revision || lang!=language || width!=ui::W || large!=ui::large) {
            revision=next;language=lang;width=ui::W;large=ui::large;
            float content=std::min(712.f,ui::W-160);
            auto full=string(i18n::get(lang,K::UpdateAvailable))+status.app.version;
            title=gfx::ellipsize(g,ui::F_H2,full,content);
            auto notice=status.state==State::READY?string(i18n::get(lang,K::UpdateReady)):full;
            banner=gfx::ellipsize(g,ui::F_SMALL,notice,392);
            notes=gfx::wrap(g,ui::F_BODY,lang==i18n::Lang::PT?status.app.notesPt:status.app.notesEn,content,3);
            K key=status.state==State::READY?K::UpdateReady:status.state==State::ERROR?(status.error=="setup"?K::UpdateSetup:K::UpdateFailed):status.state==State::DOWNLOADING?K::UpdateDownloading:K::UpdateNextStart;
            messages=gfx::wrap(g,ui::F_SMALL,i18n::get(lang,key),content,2);
        }
    }
    bool input(Service& service,Input& in,bool onHome) {
        if(!onHome){open=false;return false;}
        if(!open && status.state!=State::IDLE && in.hit(Input::LB)){open=true;return true;}
        if(!open)return false;
        if(in.hit(Input::B)){service.cancelDownload();open=false;}
        else if(in.hit(Input::A) && (status.state==State::AVAILABLE || status.state==State::ERROR))service.requestDownload();
        return true;
    }
    void draw(gfx::Gfx& g,i18n::Lang lang) {
        if(status.state==State::IDLE)return;
        auto tr=[&](K k){return i18n::get(lang,k);};
        if(!open) {
            gfx::rect(g,ui::W-516,588,476,56,ui::SURFACE2,16);
            hint(g,ui::W-496,605,"LB",XG,banner.c_str());
            return;
        }
        gfx::rect(g,0,0,ui::W,720,{0,0,0,0.78f});
        float panel=std::min(792.f,ui::W-80),left=(ui::W-panel)/2,content=panel-80;
        float line=std::max(25.f,g.fonts[ui::F_BODY].lineH);
        gfx::shadow(g,left,148,panel,440,24,32,0.6f);
        gfx::rect(g,left,138,panel,440,ui::SURFACE,24);
        gfx::rect(g,left+40,171,60,4,ui::A1,2);
        gfx::text(g,ui::F_H2,title,left+40,196,ui::TEXT);
        float y=250;
        for(const auto& text:notes){gfx::text(g,ui::F_BODY,text,left+40,y,ui::MUTED);y+=line;}
        y=402;
        for(const auto& text:messages){gfx::text(g,ui::F_SMALL,text,left+40,y,ui::A2);y+=std::max(24.f,g.fonts[ui::F_SMALL].lineH);}
        if(status.state==State::DOWNLOADING){gfx::rect(g,left+40,490,content,4,ui::SURFACE2,2);gfx::rect(g,left+40,490,content*status.progress/100.f,4,ui::A1,2);}
        float x=left+40;
        if(status.state==State::AVAILABLE || status.state==State::ERROR)x=hint(g,x,518,"A",XA,tr(K::UpdateDownload));
        hint(g,x,518,"B",XB,tr(K::Back));
    }
};
}
