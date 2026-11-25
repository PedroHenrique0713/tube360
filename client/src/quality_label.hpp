#pragma once
#include <algorithm>

namespace playback {
// Rótulo de exibição: vídeo recortado conserva a classe da largura (1920×800 = 1080p).
// Não usar para limites de fonte, decodificação, recuperação ou seleção de altura fixa.
inline int labelHeight(int width, int height) {
  if (width<=0 || height<=0) return std::max(0,height);
  int longSide=std::max(width,height), shortSide=std::min(width,height);
  int wide=longSide>=7680 ? 4320 : longSide>=3840 ? 2160 : longSide>=2560 ? 1440 :
      longSide>=1920 ? 1080 : longSide>=1280 ? 720 : longSide>=852 ? 480 :
      longSide>=640 ? 360 : longSide>=426 ? 240 : longSide>=256 ? 144 : 0;
  return std::max(shortSide,wide);
}
}
