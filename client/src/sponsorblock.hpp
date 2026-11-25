#pragma once
#include "yt/sponsorblock.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace sponsorblock {
enum Mode { OFF, BUTTON, AUTO, COUNT };
struct Action {
    enum Kind { NONE, SKIP, UNDO } kind=NONE;
    double target=0;
    explicit operator bool() const { return kind!=NONE; }
};
// Estado por abertura, preservado nos seeks/trocas de som/qualidade do mesmo vídeo.
struct Playback {
    yt::sponsorblock::Segments segments;
    std::array<bool,yt::sponsorblock::maxSegments> handled{};
    double undoStart=0, undoEnd=0;
    uint64_t undoUntil=0;
    bool undoPending=false;
    void reset() { segments.clear(); handled.fill(false); undoUntil=0; undoPending=false; }
    bool usable(const yt::sponsorblock::Segment& s, double duration) const {
        if (!std::isfinite(duration) || duration<=0 || s.start>=duration || s.end>duration+1) return false;
        return s.duration<=0 || std::abs(s.duration-duration)<=std::max(1.0,std::min(5.0,(s.end-s.start)*.05));
    }
    struct Range { double start=0, end=0; explicit operator bool() const { return end>start; } };
    Range active(double pos, double duration) const {
        Range range;
        for (size_t i=0;i<segments.size();++i) {
            const auto& s=segments[i];
            if (handled[i] || !usable(s,duration)) continue;
            if (range && s.start<=range.end) range.end=std::min(std::max(range.end,s.end),duration);
            else {
                if (range && range.start<=pos && pos<range.end) return range;
                range={s.start,std::min(s.end,duration)};
            }
        }
        return range.start<=pos && pos<range.end ? range : Range{};
    }
    bool canUndo(uint64_t now) const { return undoUntil && now<undoUntil; }
    Action skip(Range range, uint64_t now, bool automatic) {
        if (!range) return {};
        if (automatic) for (size_t i=0;i<segments.size();++i)
            if (segments[i].start<range.end && segments[i].end>range.start) handled[i]=true;
        undoStart=range.start; undoEnd=range.end; undoUntil=automatic ? now+6000 : 0;
        undoPending=automatic;
        return {Action::SKIP,range.end};
    }
    Action update(double pos, double duration, uint64_t now, int mode, bool ready) {
        if (mode==OFF) { undoUntil=0; undoPending=false; return {}; }
        // Os seis segundos começam depois do seek, mesmo se a nova faixa demorar a abrir.
        if (undoPending) { undoUntil=now+6000; if (ready) undoPending=false; }
        if (!ready) return {};
        if (canUndo(now) && (pos<undoStart-.5 || pos>undoEnd+8)) undoUntil=0;
        if (mode!=AUTO) return {};
        return skip(active(pos,duration),now,true);
    }
    Action press(double pos, double duration, uint64_t now, int mode) {
        if (mode==AUTO && canUndo(now)) { undoUntil=0; undoPending=false; return {Action::UNDO,undoStart}; }
        return mode==BUTTON ? skip(active(pos,duration),now,false) : Action{};
    }
};
}
