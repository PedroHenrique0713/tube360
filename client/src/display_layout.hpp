#pragma once
#include <algorithm>

namespace display {
struct Layout {
  float width = 1280, scale = 1, insetX = 0, insetY = 0;
  bool sd = false, narrow = false, large = false;
  int columns(int preferred) const { return narrow ? 2 : large ? std::min(3, preferred) : preferred; }
};
inline Layout layout(unsigned width, unsigned height, bool largeText = false) {
  Layout out;
  if (!width || !height) return out;
  out.width = float(width) * 720 / height;
  out.sd = height <= 576; out.narrow = out.width < 1100; out.large = largeText || out.sd;
  out.scale = height / 720.f;
  // 5% em cada borda protege texto e foco do overscan; vídeo continua usando a tela inteira.
  if (out.sd) { out.scale *= .9f; out.insetX = width * .05f; out.insetY = height * .05f; }
  return out;
}
struct VideoRect { float x, y, w, h, u0 = 0, v0 = 0, u1 = 1, v1 = 1; };
inline VideoRect videoRect(float w, float h, float sourceW, float sourceH, bool fill) {
  if (sourceW <= 0 || sourceH <= 0) return {0, 0, w, h};
  float scale = std::min(w / sourceW, h / sourceH);
  if (!fill) return {(w-sourceW*scale)/2, (h-sourceH*scale)/2, sourceW*scale, sourceH*scale};
  scale = std::max(w / sourceW, h / sourceH);
  float u = w / (sourceW*scale), v = h / (sourceH*scale);
  return {0, 0, w, h, (1-u)/2, (1-v)/2, (1+u)/2, (1+v)/2};
}
}
