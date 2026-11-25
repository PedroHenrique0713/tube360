#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

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
  struct FailedRung { int itag,fps; unsigned failures=0; uint64_t until=0; };
  std::vector<FailedRung> failed;
  uint64_t goodMs=0,holdUntil=30000,lastUpgrade=0;
  unsigned failures=0;
  explicit QualityRecovery(uint64_t now=0):holdUntil(now+30000){}
  void rejected(uint64_t now,int itag,int fps,bool cpu) {
    auto i=std::find_if(failed.begin(),failed.end(),[&](const FailedRung& rung){return rung.itag==itag && rung.fps==fps;});
    if(i==failed.end()){failed.push_back({itag,fps});i=failed.end()-1;}
    i->failures=std::min(5u,i->failures+1);
    // CPU que já falhou não é uma sondagem nova após seek, pausa ou 45 segundos.
    i->until=now+std::min<uint64_t>(960000,uint64_t(cpu?120000:30000)<<(i->failures-1));
    goodMs=0;
  }
  uint64_t retryAfter(int itag,int fps) const {
    for(const auto& rung:failed)if(rung.itag==itag && rung.fps==fps)return rung.until;
    return 0;
  }
  void changed(uint64_t now,bool up) {
    goodMs=0;
    if(up){lastUpgrade=now;holdUntil=now+30000;}
    else {
      if(lastUpgrade && now-lastUpgrade<45000)failures=std::min(3u,failures+1);
      holdUntil=now+(failures ? (60000u<<failures) : 30000u);
    }
  }
  bool sample(uint64_t now,uint64_t elapsed,bool running,int queue,bool behind,int64_t drops,
              double decodeFraction,double workScale,double mbps,double nextMbps,bool networkFresh,uint64_t rungUntil=0) {
    // Projeta o custo da próxima fonte; banda do link nominal não é uma medição.
    bool good=running && elapsed<=3000 && queue>=3 && !behind && drops<=1 &&
        decodeFraction>0 && workScale>0 && decodeFraction*workScale<.70 &&
        networkFresh && nextMbps>0 && mbps>nextMbps*1.7;
    goodMs=good ? goodMs+elapsed : 0;
    return goodMs>=20000 && now>=holdUntil && now>=rungUntil;
  }
};
inline double qualityWorkScale(int width,int height,double fps,int nextWidth,int nextHeight,double nextFps) {
  return width>0 && height>0 && fps>0 && nextWidth>0 && nextHeight>0 && nextFps>0 ?
      double(nextWidth)*nextHeight*nextFps/(double(width)*height*fps) : 0;
}
inline double qualityDecodeFraction(double msPerFrame,double fps) {
  return msPerFrame>0 && fps>0 ? msPerFrame*fps/1000 : 0;
}
}
