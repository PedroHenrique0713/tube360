#pragma once
// Incluído depois das peças Series do main, como os demais painéis nativos.
namespace sponsorblock {
inline K modeLabel(int mode) { return mode==OFF ? K::SponsorOff : mode==AUTO ? K::SponsorAuto : K::SponsorButton; }
struct UI {
    bool open=false;
    i18n::Lang language=i18n::Lang::Count;
    float width=0, fontSize=0, skipWidth=0, undoWidth=0, skippedWidth=0;
    std::array<std::string,COUNT> modes;
    std::string skipLabel, skippedLabel, undoLabel, credit;
    std::vector<std::string> warning, categories;
    void prepare(gfx::Gfx& g, i18n::Lang lang) {
        if (language==lang && width==ui::W && fontSize==g.fonts[ui::F_SMALL].size) return;
        language=lang; width=ui::W; fontSize=g.fonts[ui::F_SMALL].size;
        for (int i=0;i<COUNT;++i) modes[i]=i18n::get(lang,modeLabel(i));
        skipLabel=i18n::get(lang,K::SponsorSkip); undoLabel=i18n::get(lang,K::SponsorUndo);
        skippedLabel=i18n::get(lang,K::SponsorSkipped); credit=i18n::get(lang,K::SponsorCredit);
        skipWidth=gfx::textWidth(g,ui::F_SMALL,skipLabel); undoWidth=gfx::textWidth(g,ui::F_SMALL,undoLabel);
        skippedWidth=gfx::textWidth(g,ui::F_SMALL,skippedLabel);
        warning=gfx::wrap(g,ui::F_BODY,i18n::get(lang,K::SponsorWarning),ui::W-ui::X0-96,3);
        categories=gfx::wrap(g,ui::F_SMALL,i18n::get(lang,K::SponsorCategories),ui::W-ui::X0-96,3);
    }
    bool input(Input& in, int& mode) {
        int direction=(in.hit(Input::UP) || in.hit(Input::LEFT)) ? -1 : (in.hit(Input::DOWN) || in.hit(Input::RIGHT)) ? 1 : 0;
        if (in.hit(Input::B)) open=false;
        if (!direction) return false;
        mode=(mode+direction+COUNT)%COUNT; return true;
    }
    void drawSettings(gfx::Gfx& g, int mode, i18n::Lang lang) {
        prepare(g,lang);
        float x=ui::X0, w=ui::W-x-64;
        gfx::text(g,ui::F_BIG,i18n::get(lang,K::SponsorBlock),x,32,ui::TEXT);
        gfx::text(g,ui::F_SMALL,i18n::get(lang,K::SavedAutomatically),x,90,ui::MUTED);
        float rowH=ui::large ? 70 : 62;
        for (int i=0;i<COUNT;++i) {
            float y=140+i*(rowH+12);
            if (i==mode) gfx::rect(g,x-3,y-3,w+6,rowH+6,ui::A1,20);
            gfx::rect(g,x,y,w,rowH,i==mode ? ui::SURFACE2 : ui::SURFACE,18);
            gfx::text(g,ui::F_BODY,modes[i],x+28,y+(rowH-g.fonts[ui::F_BODY].size)/2,ui::TEXT);
        }
        float y=140+int(COUNT)*(rowH+12)+16;
        for (const auto& line : warning) { gfx::text(g,ui::F_BODY,line,x,y,ui::TEXT); y+=g.fonts[ui::F_BODY].lineH+4; }
        y+=12;
        for (const auto& line : categories) { gfx::text(g,ui::F_SMALL,line,x,y,ui::MUTED); y+=g.fonts[ui::F_SMALL].lineH+2; }
        gfx::text(g,ui::F_SMALL,credit,x,ui::large ? 606 : 614,ui::MUTED);
        float hx=hint(g,x,678,i18n::get(lang,K::BtnVertical),XG,i18n::get(lang,K::ChooseRow));
        hint(g,hx,678,"B",XB,i18n::get(lang,K::Back));
    }
    void timeline(gfx::Gfx& g, const Playback& state, int mode, double duration, float x, float y, float w) {
        if (mode==OFF || duration<=0) return;
        for (const auto& s : state.segments) if (state.usable(s,duration)) {
            float start=x+w*float(s.start/duration), end=x+w*float(std::min(s.end,duration)/duration);
            gfx::rect(g,start,y,std::max(2.f,end-start),4,ui::alpha(ui::A2,.85f),1);
        }
    }
    void hud(gfx::Gfx& g, const Playback& state, int mode, double pos, double duration, uint64_t now, i18n::Lang lang) {
        bool undo=mode==AUTO && state.canUndo(now), skip=mode==BUTTON && bool(state.active(pos,duration));
        if (!undo && !skip) return;
        prepare(g,lang);
        float d=ui::large ? std::max(36.f,g.fonts[ui::F_TINY].size+10) : 24;
        float h=std::max(d,g.fonts[ui::F_SMALL].lineH)+24;
        float w=48+d+8+(undo ? skippedWidth+undoWidth+28 : skipWidth), x=ui::W-40-w, y=44;
        gfx::rect(g,x,y,w,h,{0,0,0,.86f},14);
        if (undo) {
            gfx::text(g,ui::F_SMALL,skippedLabel,x+20,y+(h-g.fonts[ui::F_SMALL].size)/2,ui::TEXT);
            x+=skippedWidth+28;
        }
        buttonGlyph(g,x+20,y+(h-d)/2,"Y",XY);
        gfx::text(g,ui::F_SMALL,undo ? undoLabel : skipLabel,x+20+d+8,y+(h-g.fonts[ui::F_SMALL].size)/2,ui::TEXT);
    }
};
}
