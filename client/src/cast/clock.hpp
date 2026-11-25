#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace nest {
// A assimetria da rede é desconhecida: o menor RTT limita melhor o erro do ponto médio.
// A janela não guarda JSON nem aloca; vivacidade e progresso são medidos fora deste filtro.
class ClockFilter {
    struct Sample { double position=0, midpoint=0, received=0, rtt=0, rate=0; };
    std::array<Sample,32> samples{};
    unsigned next=0;
    double began=0;
public:
    double minimumRtt=0;
    void reset() { samples={}; next=0; began=minimumRtt=0; }
    bool ready(double now) const { return began && (minimumRtt<=200 || now-began>=1500); }
    bool update(double position,double midpoint,double received,double rtt,double rate,double& estimate) {
        if (!std::isfinite(position) || !std::isfinite(rtt) || rtt<0 || rtt>1000) return false;
        if (!began) began=received;
        samples[next]={position,midpoint,received,rtt,rate}; next=(next+1)%samples.size();
        const Sample* best=nullptr;
        for (const auto& sample:samples) {
            if (!sample.received || received-sample.received>8000) continue;
            if (!best || sample.rtt<best->rtt || (sample.rtt==best->rtt && sample.received>best->received)) best=&sample;
        }
        if (!best) return false;
        minimumRtt=best->rtt;
        estimate=best->position+(received-best->midpoint)*best->rate/1000;
        return true;
    }
};
}
