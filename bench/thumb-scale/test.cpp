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
  std::puts("boxScale: checkerboard averages, 1280x720 -> 320x180 stays inside the destination");
}
