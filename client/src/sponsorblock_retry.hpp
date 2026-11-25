#pragma once
#include "yt/sponsorblock.hpp"

namespace sponsorblock {
struct RetryPlan {
    static constexpr uint64_t settleMs=3000, delayMs=3000;
    unsigned attempt=0;
    bool waiting=false, playing=false;
    uint64_t retryAt=0, playingAt=0;
    void playback(bool active, uint64_t now) {
        if (active && !playing) playingAt=now;
        playing=active;
    }
    void begin() { ++attempt; waiting=false; }
    bool finish(const yt::sponsorblock::Diagnostic& d, uint64_t now) {
        using S=yt::sponsorblock::State;
        bool transient=d.state==S::NETWORK || (d.state==S::TLS && d.transport==35) ||
            (d.state==S::HTTP && (d.http==408 || d.http==429 || d.http==500 || d.http==502 || d.http==503 || d.http==504));
        waiting=attempt<2 && !d.cached && transient;
        retryAt=now+delayMs;
        return waiting;
    }
    bool due(uint64_t now) const {
        return waiting && playing && now>=retryAt && now>=playingAt && now-playingAt>=settleMs;
    }
};
}
