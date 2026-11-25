#pragma once
#include "search_worker.hpp"

struct SearchUI {
    yt::search::Filters filters, draft;
    bool filterOpen=false, listFocus=false;
    int filterRow=0, keyRow=0, keyCol=0, selected=0;
    search::Worker worker;
    struct Entry { std::string text, shown; bool recent=false; };
    std::vector<Entry> entries;
    std::vector<std::string> remote;
    std::string lastQuery, lastLanguage;
    bool lastActive=false;
    uint64_t fontRevision=0;
    float listWidth=0;
    i18n::Lang cachedLanguage=i18n::Lang::EN;
    std::string heading;
    void resetFocus() { keyRow=keyCol=selected=0; listFocus=false; }
    static K value(const yt::search::Filters& f,int row) {
        const K types[]={K::AllResults,K::Videos,K::Channel,K::Playlists,K::Live};
        const K durations[]={K::SearchAny,K::SearchShort,K::SearchMedium,K::SearchLong};
        const K dates[]={K::SearchAny,K::SearchToday,K::SearchWeek,K::SearchMonth,K::SearchYear};
        const K orders[]={K::SearchRelevance,K::SearchViews};
        return row==0 ? types[f.type] : row==1 ? durations[f.duration] : row==2 ? dates[f.date] : orders[f.order];
    }
    static std::string summary(const yt::search::Filters& f,i18n::Lang lang) {
        std::string out;
        const int values[]={f.type,f.duration,f.date,f.order};
        for (int row=0;row<4;++row) if (values[row]) {
            if (!out.empty()) out+=" · ";
            const K shortDuration[]={K::SearchAny,K::SearchShortLabel,K::SearchMediumLabel,K::SearchLongLabel};
            out+=i18n::get(lang,row==1 ? shortDuration[f.duration] : value(f,row));
        }
        return out.empty() ? i18n::get(lang,K::SearchRelevance) : out;
    }
    void beginFilters(yt::search::Filters current) { draft=current; draft.normalize(); filterRow=0; filterOpen=true; }
    bool filterInput(Input& in) {
        if (in.hit(Input::B)) { filterOpen=false; return false; }
        if (in.hit(Input::UP)) filterRow=filterRow>=4 ? 3 : std::max(0,filterRow-1);
        if (in.hit(Input::DOWN)) filterRow=std::min(4,filterRow+1);
        bool left=in.hit(Input::LEFT), right=in.hit(Input::RIGHT), a=in.hit(Input::A);
        if (filterRow<4) {
            if (left) draft.change(filterRow,-1);
            if (right || a) draft.change(filterRow,1);
        } else {
            if (left || right) filterRow=filterRow==4 ? 5 : 4;
            if (a) { filters=filterRow==5 ? yt::search::Filters{} : draft; filterOpen=false; return true; }
        }
        if (in.hit(Input::X)) draft={};
        return false;
    }
    void update(gfx::Gfx& g,const std::string& query,const std::vector<std::string>& recent,i18n::Lang lang,bool active) {
        const char* language=i18n::code(lang);
        bool changed=active!=lastActive || query!=lastQuery || lastLanguage!=language;
        if (changed) {
            lastActive=active; lastQuery=query; lastLanguage=language; remote.clear();
            if (worker.thread) worker.select(active ? query : "",language);
        }
        changed=worker.poll(remote) || changed;
        float width=ui::narrow ? ui::W-200 : ui::W-910;
        changed=changed || fontRevision!=g.fontRevision || listWidth!=width || cachedLanguage!=lang;
        if (!changed) return;
        fontRevision=g.fontRevision; listWidth=width; cachedLanguage=lang;
        entries.clear();
        if (active) for (const auto& text:remote) entries.push_back({text,{},false});
        for (const auto& text:recent) {
            if (std::none_of(entries.begin(),entries.end(),[&](const Entry& e) { return e.text==text; })) entries.push_back({text,{},true});
        }
        for (auto& e:entries) e.shown=gfx::ellipsize(g,ui::F_BODY,e.text,width-(e.recent ? 126 : 36));
        heading=gfx::ellipsize(g,ui::F_H2,i18n::get(lang,remote.empty() ? K::RecentSearches : K::SearchSuggestions),width);
        selected=std::clamp(selected,0,std::max(0,int(entries.size())-1));
        if (entries.empty()) listFocus=false;
    }
    std::string keyboardInput(Input& in,std::string& query,const char* const* keys) {
        auto backspace=[&]() { if (!query.empty()) { size_t i=query.size()-1; while (i && ((unsigned char)query[i]&0xc0)==0x80) --i; query.resize(i); } };
        std::string chosen;
        if (listFocus) {
            if (in.hit(Input::UP)) selected=std::max(0,selected-1);
            if (in.hit(Input::DOWN)) selected=std::min(std::max(0,int(entries.size())-1),selected+1);
            if (in.hit(Input::LEFT)) listFocus=false;
            if (in.hit(Input::A) && selected<int(entries.size())) chosen=entries[selected].text;
        } else {
            int cols=keyRow<4 ? 10 : 3;
            if (in.hit(Input::UP) && keyRow>0) { if (keyRow==4) keyCol=keyCol*3+1; --keyRow; }
            if (in.hit(Input::DOWN) && keyRow<4) { ++keyRow; if (keyRow==4) keyCol=std::min(2,keyCol/4); }
            if (in.hit(Input::LEFT) && keyCol>0) --keyCol;
            if (in.hit(Input::RIGHT)) {
                if (keyCol+1<cols) ++keyCol;
                else if (!entries.empty()) { listFocus=true; selected=0; }
            }
            if (in.hit(Input::A)) {
                if (keyRow<4) {
                    const char* row=keys[keyRow]; int ci=0; size_t i=0;
                    while (row[i]) { size_t start=i; gfx::utf8(row,i); if (ci++==keyCol) { if (query.size()<256) query.append(row+start,i-start); break; } }
                } else if (keyCol==0) { if (query.size()<256) query+=' '; }
                else if (keyCol==1) backspace();
                else chosen=query;
            }
        }
        if (in.hit(Input::X)) backspace();
        if (in.hit(Input::Y) && query.size()<256) query+=' ';
        if (in.hit(Input::START)) chosen=query;
        return chosen;
    }
    void draw(gfx::Gfx& g,const std::string& query,const char* const* keys,i18n::Lang lang,bool music,bool railFocus,double t) {
        auto tr=[&](K key) { return i18n::get(lang,key); };
        gfx::rect(g,0,0,ui::W,ui::H,ui::alpha(ui::BG,0.55f));
        gfx::textBox(g,ui::F_BIG,tr(K::Search),ui::X0,40,ui::W-200,ui::TEXT);
        if (!music) hint(g,ui::W-250,52,"LB",XG,tr(K::SearchFilters));
        float fx=ui::X0, fy=112, fw=ui::W-200;
        gfx::rect(g,fx,fy,fw,60,ui::SURFACE2,30);
        icon(g,1,fx+34,fy+30,ui::MUTED,ui::SURFACE2);
        float width=fw-90;
        if (query.empty()) gfx::textBox(g,ui::F_KEY,gfx::ellipsize(g,ui::F_KEY,tr(music ? K::MusicSearch : K::SearchPrompt),width),fx+62,fy+17,width,ui::FAINT);
        else gfx::textBox(g,ui::F_KEY,gfx::ellipsize(g,ui::F_KEY,query,width),fx+62,fy+17,width,ui::TEXT);
        if (std::fmod(t,1.0)<0.55) gfx::rect(g,g.rtlText ? fx+fw-18 : fx+fw-22,fy+16,3,28,ui::A1,1.5f);
        const float kx=ui::X0, ky=200, kw=58, kh=54, kg=8;
        for (int rr=0;rr<4;++rr) {
            const char* row=keys[rr]; size_t i=0; int c=0;
            while (row[i]) {
                size_t start=i; gfx::utf8(row,i); std::string ch(row+start,i-start);
                bool focus=!listFocus && !railFocus && keyRow==rr && keyCol==c;
                float x=kx+c*(kw+kg), y=ky+rr*(kh+kg);
                gfx::rect(g,x,y,kw,kh,focus ? ui::TEXT : ui::SURFACE2,14);
                gfx::text(g,ui::F_KEY,ch,x+kw/2,y+15,focus ? ui::INK : ui::TEXT,1); ++c;
            }
        }
        const K acts[]={K::Space,K::Delete,K::Search}; float aw=(652-16)/3.f;
        for (int c=0;c<3;++c) {
            bool focus=!listFocus && !railFocus && keyRow==4 && keyCol==c;
            float x=kx+c*(aw+kg), y=ky+4*(kh+kg);
            gfx::rect(g,x,y,aw,kh,focus ? ui::TEXT : c==2 ? ui::A1 : ui::SURFACE2,14);
            gfx::text(g,ui::F_BODY,tr(acts[c]),x+aw/2,y+16,focus ? ui::INK : ui::TEXT,1);
        }
        float rx=ui::narrow ? ui::X0 : 846, top=ui::narrow ? 536 : 198;
        gfx::textBox(g,ui::F_H2,heading,rx,top,listWidth,ui::TEXT);
        int visible=ui::narrow ? 1 : 8;
        int first=listFocus ? std::max(0,selected-visible+1) : 0;
        float rowHeight=ui::large ? 49 : 45;
        for (int i=first;i<std::min(first+visible,int(entries.size()));++i) {
            const auto& entry=entries[i]; bool focus=listFocus && !railFocus && i==selected;
            float y=top+40+(i-first)*rowHeight;
            gfx::rect(g,rx,y,listWidth,rowHeight-5,focus ? ui::TEXT : ui::SURFACE2,16);
            gfx::textBox(g,ui::F_BODY,entry.shown,rx+16,y+9,listWidth-(entry.recent ? 126 : 36),focus ? ui::INK : ui::TEXT);
            if (entry.recent) gfx::textBox(g,ui::F_TINY,tr(K::SearchRecentTag),rx+listWidth-104,y+12,92,focus ? ui::INK : ui::MUTED);
        }
        if (entries.empty()) gfx::textBox(g,ui::F_SMALL,gfx::ellipsize(g,ui::F_SMALL,tr(K::RecentEmpty),listWidth),rx,top+40,listWidth,ui::FAINT);
        float hx=hint(g,ui::X0,ui::H-40,"A",XA,tr(listFocus ? K::Search : K::Type));
        hx=hint(g,hx,ui::H-40,"X",XX,tr(K::Delete));
        hx=hint(g,hx,ui::H-40,"Menu",XG,tr(K::Search));
        hx=hint(g,hx,ui::H-40,"B",XB,tr(K::Back));
        if (lang==i18n::Lang::AR) hint(g,ui::X0,ui::H-82,"RB",XG,tr(K::KeyboardToggle));
    }
    void drawFilters(gfx::Gfx& g,i18n::Lang lang) {
        auto tr=[&](K key) { return i18n::get(lang,key); };
        gfx::rect(g,0,0,ui::W,720,{0,0,0,0.88f});
        float w=std::min(860.f,ui::W-160), x=(ui::W-w)/2;
        gfx::rect(g,x,54,w,612,ui::SURFACE,28);
        gfx::textBox(g,ui::F_BIG,tr(K::SearchFilters),x+32,80,w-64,ui::TEXT);
        const K labels[]={K::SearchType,K::SearchDuration,K::SearchDate,K::SearchOrder};
        for (int row=0;row<4;++row) {
            float y=146+row*86; bool focus=filterRow==row, disabled=draft.disabled(row);
            gfx::rect(g,x+26,y,w-52,78,focus ? ui::TEXT : ui::SURFACE2,15);
            gfx::Color color=focus ? ui::INK : disabled ? ui::FAINT : ui::TEXT;
            gfx::textBox(g,ui::F_CHAN,tr(labels[row]),x+46,y+8,w-92,color);
            gfx::textBox(g,ui::F_KEY,tr(value(draft,row)),x+46,y+38,w-92,color);
        }
        float bw=(w-66)/2;
        for (int i=0;i<2;++i) {
            bool focus=filterRow==4+i; float bx=x+26+i*(bw+14);
            gfx::rect(g,bx,504,bw,62,focus ? ui::TEXT : ui::SURFACE2,20);
            gfx::textBox(g,ui::F_KEY,tr(i ? K::SearchClear : K::SearchApply),bx+20,523,bw-40,focus ? ui::INK : ui::TEXT);
        }
        float hx=hint(g,x+32,610,tr(K::BtnVertical),XG,tr(K::ChooseRow));
        hx=hint(g,hx,610,tr(K::BtnHorizontal),XG,tr(K::ChangeValue));
        if (filterRow>=4) hx=hint(g,hx,610,"A",XA,tr(filterRow==5 ? K::SearchClear : K::SearchApply));
        hint(g,hx,610,"B",XB,tr(K::Back));
    }
};
