// Passos do volume do app (0..100 de 5 em 5).
#include "../../client/src/player_volume.hpp"
#include <cassert>
#include <cstdio>
int main() {
  using volume::stepped;
  static_assert(stepped(100, 1) == 100 && stepped(0, -1) == 0);
  assert(stepped(50, 1) == 55 && stepped(50, -1) == 45);
  assert(stepped(52, 1) == 55 && stepped(52, -1) == 50);   // fora da grade anda até a vizinha
  assert(stepped(98, 1) == 100 && stepped(3, -1) == 0);
  assert(stepped(-20, 1) == 5 && stepped(500, -1) == 95);  // valor absurdo do arquivo é limitado antes
  int v = 100; for (int i = 0; i < 40; ++i) v = stepped(v, -1);
  assert(v == 0);
  for (int i = 0; i < 40; ++i) v = stepped(v, 1);
  assert(v == 100);
  std::puts("PASS volume steps");
}
