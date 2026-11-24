#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace audio {
inline constexpr double speeds[] = {.25,.5,.75,1,1.25,1.5,1.75,2};
inline constexpr const char* speedLabels[] = {"0.25x","0.5x","0.75x","1x","1.25x","1.5x","1.75x","2x"};
inline int speedIndex(double speed) { for (int i=0;i<8;++i) if (speed==speeds[i]) return i; return 3; }

// WSOLA estéreo, 20 ms de janela e 5 ms de busca a 44,1 kHz. Memória fixa, só no worker de áudio.
class Tempo {
  static constexpr int hop=441, window=hop*2, search=220, capacity=8192;
  std::array<float,capacity*2> input{};
  std::array<float,hop*2> tail{};
  int64_t base=0, received=0, emitted=0;
  double next=0, rate=1;
  bool first=true;
  float sample(int64_t frame, int channel) const {
    return frame<base || frame<0 || frame>=received ? 0 : input[size_t(frame%capacity)*2+channel];
  }
public:
  void reset(double speed) { base=received=emitted=0; next=0; rate=std::clamp(speed,.25,2.0); first=true; }
  template<class Sink> bool push(const float* pcm, int count, Sink sink, bool finish=false) {
    if (count<0 || count>capacity-window-search || received-base+count>capacity) return false;
    for (int i=0;i<count;++i) for (int c=0;c<2;++c) input[size_t((received+i)%capacity)*2+c]=pcm[i*2+c];
    received+=count;
    std::array<float,hop*2> output;
    while (finish ? emitted<int64_t(std::llround(received/rate)) : int64_t(std::ceil(next))+search+window<=received) {
      int64_t center=int64_t(std::llround(next)), chosen=center;
      if (!first) {
        double best=-1e300;
        int64_t lo=std::max(base,center-search), hi=std::min(received-window,center+search);
        // Busca grossa e refinamento: correlação normalizada, quatro amostras por passo.
        auto score=[&](int64_t pos) {
          double dot=0, energy=1e-12;
          for (int i=0;i<hop;i+=4) for (int c=0;c<2;++c) {
            double v=sample(pos+i,c); dot+=tail[i*2+c]*v; energy+=v*v;
          }
          return dot/std::sqrt(energy);
        };
        for (int64_t pos=lo;pos<=hi;pos+=4) { double s=score(pos); if (s>best) { best=s; chosen=pos; } }
        int64_t candidate=chosen;
        for (int64_t pos=std::max(lo,candidate-3);pos<=std::min(hi,candidate+3);++pos) {
          double s=score(pos); if (s>best) { best=s; chosen=pos; }
        }
      }
      int n=finish ? int(std::min<int64_t>(hop,int64_t(std::llround(received/rate))-emitted)) : hop;
      for (int i=0;i<hop;++i) for (int c=0;c<2;++c) {
        float fade=float(i)/hop;
        output[i*2+c]=first ? sample(chosen+i,c) : tail[i*2+c]*(1-fade)+sample(chosen+i,c)*fade;
        tail[i*2+c]=sample(chosen+hop+i,c);
      }
      if (!sink(output.data(),n)) return false;
      emitted+=n; first=false; next+=hop*rate;
      base=std::max<int64_t>(base,std::max<int64_t>(0,int64_t(std::floor(next))-search));
    }
    return true;
  }
};
inline bool repeatAtEnd(bool loop, bool live, bool endTimer) { return loop && !live && !endTimer; }
}
