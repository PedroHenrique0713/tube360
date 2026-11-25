#pragma once
#include <algorithm>
#include <cstdint>

namespace playback {
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
