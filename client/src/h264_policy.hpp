#pragma once
#include <algorithm>
#include <cmath>

namespace playback {
inline bool source1080p30(int height,double fps) {
  return height==1080 && std::isfinite(fps) && fps>0 && fps<=30.1;
}
struct H264Policy {
  int threads;
  bool spread, skipLoop;
};
inline H264Policy h264Policy(int height,double fps,bool fullFilter,int threads=4) {
  bool profile=source1080p30(height,fps);
  return {profile ? 6 : threads,profile,profile && !fullFilter};
}
// F39, 137 real, máscara 7/ALL/6 FRAME/0,2,4,1,3,5. É uma previsão,
// não um gate: pressão real, falhas anteriores e banda continuam mandando.
inline constexpr double measured1080p30Ms=22.567982;
inline double projectedDecodeFraction(double measured,double pixelScale,bool nextFast1080,
                                      double nextFps,double observed1080Ms=0) {
  if (!nextFast1080) return measured*pixelScale;
  // Com a fila cheia, os workers executam durante a espera do produtor; send/receive
  // podem medir quase zero. Essa amostra não reduz o custo isolado medido no Xenon.
  double cost=std::max(measured1080p30Ms,observed1080Ms);
  return cost*nextFps/1000.0;
}
}
