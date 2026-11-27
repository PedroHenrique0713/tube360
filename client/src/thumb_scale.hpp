#pragma once
#include <algorithm>
#include <cstddef>

// Reduz uma imagem RGBA tirando a média dos pixels de origem de cada pixel de destino (fonte maior que o destino);
// o vizinho mais próximo serrilhava a miniatura HD reduzida para o tamanho do cartão (#15).
inline void boxScale(const unsigned char* src, int w, int h, int cropX, int cropY, int cropW, int cropH, unsigned char* dst, int dstStride, int dw, int dh) {
  for (int y = 0; y < dh; ++y) {
    int y0 = cropY + y * cropH / dh, y1 = std::max(y0 + 1, cropY + (y + 1) * cropH / dh);
    for (int x = 0; x < dw; ++x) {
      int x0 = cropX + x * cropW / dw, x1 = std::max(x0 + 1, cropX + (x + 1) * cropW / dw);
      unsigned sum[4] = {0, 0, 0, 0}; unsigned n = 0;
      for (int yy = y0; yy < y1 && yy < h; ++yy) for (int xx = x0; xx < x1 && xx < w; ++xx) {
        const unsigned char* p = src + 4 * ((size_t)yy * w + xx);
        sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; sum[3] += p[3]; n++;
      }
      unsigned char* o = dst + (size_t)y * dstStride + 4 * x;
      if (!n) { o[0] = o[1] = o[2] = 0; o[3] = 255; continue; }
      for (int c = 0; c < 4; ++c) o[c] = (unsigned char)((sum[c] + n / 2) / n);
    }
  }
}
