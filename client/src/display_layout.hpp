#pragma once
#include <algorithm>

namespace display {
struct Layout {
  float width = 1280, scale = 1, scaleX = 1, insetX = 0, insetY = 0;
  bool sd = false, narrow = false, large = false;
  int columns(int preferred) const { return narrow ? 2 : large ? std::min(3, preferred) : preferred; }
};
inline Layout layout(unsigned width, unsigned height, bool largeText = false,float outputAspect = 0) {
  Layout out;
  if (!width || !height) return out;
  // PAL/NTSC podem usar pixels não quadrados; o painel informa o aspecto físico.
  out.width = outputAspect>0 ? outputAspect*720 : float(width)*720/height;
  out.sd = height <= 576; out.narrow = out.width < 1100; out.large = largeText || out.sd;
  out.scale = height / 720.f;
  out.scaleX = width/out.width;
  // 5% em cada borda protege texto e foco do overscan; vídeo continua usando a tela inteira.
  if (out.sd) { out.scale *= .9f; out.scaleX *= .9f; out.insetX = width * .05f; out.insetY = height * .05f; }
  return out;
}
struct VideoRect { float x, y, w, h, u0 = 0, v0 = 0, u1 = 1, v1 = 1; };
struct CommentsPanel {
  float x, y = 58, w, h = 536, metaY, bodyY, lineH;
  int lines;
};
inline CommentsPanel commentsPanel(float width, float authorLine, float metaLine, float bodyLine) {
  CommentsPanel p;
  p.w = std::min(1080.f,width-80); p.x = (width-p.w)/2;
  p.metaY = 86+std::max(33.f,authorLine);
  p.bodyY = p.metaY+std::max(38.f,metaLine+12);
  p.lineH = std::max(27.f,bodyLine+4);
  p.lines = std::max(1,std::min(10,int((p.h-70-p.bodyY)/p.lineH)));
  return p;
}
// Afastamento, em texels da fonte, das quatro leituras bilineares da luma ao reduzir `ratio` (fonte/saída) vezes.
// 1:1 fica em bilinear puro; em 1,5 (1080p→720p) são 0,45 (eram 0,375 fixos); acima disso cresce 0,3 por vez, até 0,9.
inline float downscaleTap(float ratio) {
  return ratio <= 1.5f ? std::max(0.f, .9f * (ratio - 1)) : std::min(.9f, .3f * ratio);
}
// `pixelAspect` é largura/altura física de cada pixel do framebuffer (1 = quadrado): o vídeo conserva a proporção na tela, não no framebuffer.
inline VideoRect videoRect(float w, float h, float sourceW, float sourceH, bool fill, float pixelAspect = 1) {
  if (sourceW <= 0 || sourceH <= 0) return {0, 0, w, h};
  if (!(pixelAspect > 0)) pixelAspect = 1;
  float boxW = w * pixelAspect;
  float scale = std::min(boxW / sourceW, h / sourceH);
  if (!fill) return {(boxW-sourceW*scale)/2/pixelAspect, (h-sourceH*scale)/2, sourceW*scale/pixelAspect, sourceH*scale};
  scale = std::max(boxW / sourceW, h / sourceH);
  float u = boxW / (sourceW*scale), v = h / (sourceH*scale);
  return {0, 0, w, h, (1-u)/2, (1-v)/2, (1+u)/2, (1+v)/2};
}
}
