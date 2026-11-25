#pragma once
#include <algorithm>
#include <cstdint>

namespace playback {
struct QualityLoad {
  bool networkLimited,overloaded;
  QualityLoad(double decode,double copy,bool upload,bool waiting,bool fresh,double mbps,double required):
    networkLimited(waiting && fresh && required>0 && mbps>0 && mbps<required*1.10),
    overloaded(decode>.90 || decode+copy>.95 || upload || networkLimited){}
};
inline bool cadenceBeforeResolution(int height,double sourceRate,int presentation,int lowerHeight) {
  return height>=720 && sourceRate>30.1 && presentation>30 && lowerHeight<height;
}
inline bool presentationPressure(int height,int fps,bool slow,double copy) {
  // A fila cheia e 720p30 barato não provam que baixar pixels resolverá um atraso da UI.
  return slow && (height>720 || fps>30 || copy>.15);
}
struct QualityPressure {
  uint64_t settleUntil=0,pressureMs=0;
  unsigned windows=0;
  explicit QualityPressure(uint64_t now=0){changed(now);}
  void changed(uint64_t now){settleUntil=now+8000;pressureMs=0;windows=0;}
  bool settled(uint64_t now,uint64_t elapsed) const {return now>=elapsed && now-elapsed>=settleUntil;}
  bool sample(uint64_t now,uint64_t elapsed,bool running,bool measured,bool starving,bool overloaded) {
    if(!running || elapsed>10000){changed(now);return false;}
    if(!settled(now,elapsed) || elapsed<500 || !measured || !starving || !overloaded){pressureMs=0;windows=0;return false;}
    pressureMs+=elapsed;++windows;
    return pressureMs>=5000 && windows>=3;
  }
};
struct QualityRecovery {
  uint64_t goodMs=0,holdUntil=30000,lastUpgrade=0;
  unsigned failures=0;
  explicit QualityRecovery(uint64_t now=0):holdUntil(now+30000){}
  void changed(uint64_t now,bool up) {
    goodMs=0;
    if(up){lastUpgrade=now;holdUntil=now+30000;}
    else {
      if(lastUpgrade && now-lastUpgrade<45000)failures=std::min(3u,failures+1);
      holdUntil=now+(failures ? (60000u<<failures) : 30000u);
    }
  }
  bool sample(uint64_t now,uint64_t elapsed,bool running,int queue,bool behind,int64_t drops,
              double decodeFraction,double workScale,double mbps,double nextMbps,bool networkFresh) {
    // Projeta o custo da próxima fonte; banda do link nominal não é uma medição.
    bool good=running && elapsed<=3000 && queue>=3 && !behind && drops<=1 &&
        decodeFraction>0 && workScale>0 && decodeFraction*workScale<.70 &&
        networkFresh && nextMbps>0 && mbps>nextMbps*1.7;
    goodMs=good ? goodMs+elapsed : 0;
    return goodMs>=20000 && now>=holdUntil;
  }
};
}
