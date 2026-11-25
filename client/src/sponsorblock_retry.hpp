#pragma once
#include "yt/sponsorblock.hpp"
#include <algorithm>

namespace sponsorblock {
struct RetryPlan {
    static constexpr uint64_t initialRetryMs=3000, delayMs=250;
    unsigned attempt=0;
    bool waiting=false, playing=false;
    uint64_t retryAt=0;
    uint64_t selectedAt=0;
    void playback(bool active, uint64_t) { playing=active; }
    void begin() { ++attempt; waiting=false; }
    bool finish(const yt::sponsorblock::Diagnostic& d, uint64_t now) {
        using S=yt::sponsorblock::State;
        bool transient=d.state==S::NETWORK || (d.state==S::TLS && d.transport==35) ||
            (d.state==S::HTTP && (d.http==408 || d.http==429 || d.http==500 || d.http==502 || d.http==503 || d.http==504));
        waiting=attempt<2 && !d.cached && transient;
        retryAt=std::max(now+delayMs,selectedAt+initialRetryMs);
        return waiting;
    }
    bool due(uint64_t now) const {
        // O relógio da abertura continua mesmo sem primeiro quadro. Esperar PLAYING
        // perdia justamente o trecho inicial quando a abertura ficava lenta.
        return waiting && now>=retryAt;
    }
};
}
