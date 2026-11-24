#pragma once
#include "service.hpp"

namespace updates {
struct UI {
    Status status;
    unsigned revision=0;
    bool open=false;
    string title,banner;
    std::vector<string> notes;
    i18n::Lang language=i18n::Lang::EN;
    void poll(Service& service,gfx::Gfx& g,i18n::Lang lang) {
        unsigned next=service.poll(status,revision);
        if(next!=revision || lang!=language) {
            revision=next;language=lang;
            auto full=string(i18n::get(lang,K::UpdateAvailable))+status.app.version;
            title=gfx::ellipsize(g,ui::F_H2,full,712);
            banner=gfx::ellipsize(g,ui::F_SMALL,full,392);
            notes=gfx::wrap(g,ui::F_BODY,lang==i18n::Lang::PT?status.app.notesPt:status.app.notesEn,712,5);
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
            gfx::rect(g,740,588,476,56,ui::SURFACE2,16);
            hint(g,760,605,"LB",XG,status.state==State::READY?tr(K::UpdateReady):banner.c_str());
            return;
        }
        gfx::rect(g,0,0,1280,720,{0,0,0,0.78f});
        gfx::shadow(g,244,188,792,360,24,32,0.6f);
        gfx::rect(g,244,178,792,360,ui::SURFACE,24);
        gfx::rect(g,280,211,60,4,ui::A1,2);
        gfx::text(g,ui::F_H2,title,280,236,ui::TEXT);
        float y=286;
        for(const auto& line:notes){gfx::text(g,ui::F_BODY,line,280,y,ui::MUTED);y+=25;}
        K message= status.state==State::READY?K::UpdateReady:status.state==State::ERROR?(status.error=="setup"?K::UpdateSetup:K::UpdateFailed):status.state==State::DOWNLOADING?K::UpdateDownloading:K::UpdateNextStart;
        gfx::text(g,ui::F_SMALL,tr(message),280,438,ui::A2);
        if(status.state==State::DOWNLOADING){gfx::rect(g,280,470,712,4,ui::SURFACE2,2);gfx::rect(g,280,470,712*status.progress/100.f,4,ui::A1,2);}
        float x=280;
        if(status.state==State::AVAILABLE || status.state==State::ERROR)x=hint(g,x,490,"A",XA,tr(K::UpdateDownload));
        hint(g,x,490,"B",XB,tr(K::Back));
    }
};
}
