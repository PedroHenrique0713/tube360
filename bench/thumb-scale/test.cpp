// boxScale (#15): reduzir a miniatura HD por média, não por vizinho mais próximo.
#include "thumb_scale.hpp"
#include <cstdio>
#include <vector>
static int fail(const char* m) { std::fprintf(stderr, "FAIL %s\n", m); return 1; }
int main() {
  // 8x8 em xadrez 1 px (preto/branco) reduzido a 2x2: cada saída é a média ~128, não 0 ou 255 (vizinho mais próximo).
  const int w = 8, h = 8; std::vector<unsigned char> src(w * h * 4);
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { unsigned char v = (x + y) & 1 ? 255 : 0; unsigned char* p = &src[4 * (y * w + x)]; p[0] = p[1] = p[2] = v; p[3] = 255; }
  std::vector<unsigned char> out(2 * 2 * 4);
  boxScale(src.data(), w, h, 0, 0, w, h, out.data(), 2 * 4, 2, 2);
  for (int i = 0; i < 4; ++i) if (out[4 * i] < 120 || out[4 * i] > 136 || out[4 * i + 3] != 255) return fail("checkerboard averages to mid grey");
  // 1280x720 sólido -> 320x180 sólido, e o destino com stride maior (linha dentro de um quadro maior) não vaza.
  std::vector<unsigned char> big(1280 * 720 * 4, 200);
  std::vector<unsigned char> dst(400 * 180 * 4, 7);
  boxScale(big.data(), 1280, 720, 0, 0, 1280, 720, dst.data() + 4 * 40, 4 * 400, 320, 180);
  if (dst[4 * 39] != 7 || dst[4 * 360] != 7 || dst[4 * 40] != 200 || dst[4 * (179 * 400 + 359)] != 200) return fail("stride and bounds");
  // sddefault 16:9 (640x480 com 60 px de preto em cima e embaixo) é reconhecido e cortado para 640x360; vídeo 4:3 cheio não.
  {
    std::vector<unsigned char> sd(640 * 480 * 4, 200); for (int y = 0; y < 480; ++y) for (int x = 0; x < 640; ++x) { auto* p = &sd[4 * (y * 640 + x)]; p[3] = 255; if (y < 60 || y >= 420) p[0] = p[1] = p[2] = 0; }
    if (!letterboxed640x480(sd.data(), 640, 480)) return fail("letterboxed sddefault detected");
    std::vector<unsigned char> full(640 * 480 * 4, 200); if (letterboxed640x480(full.data(), 640, 480)) return fail("full 4:3 frame not cropped");
    std::vector<unsigned char> out(320 * 180 * 4); boxScale(sd.data(), 640, 480, 0, 60, 640, 360, out.data(), 4 * 320, 320, 180);
    if (out[0] != 200 || out[4 * (179 * 320 + 319)] != 200) return fail("crop keeps only the 640x360 picture (no black rows)");
  }
  std::puts("boxScale: checkerboard averages, 1280x720 -> 320x180 stays inside the destination");
}
