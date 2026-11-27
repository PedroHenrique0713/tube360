// Fila da Música (music_queue.hpp): fim de faixa, próxima, anterior, reordenar, aleatório e janela da lista.
// Compilar e rodar: c++ -std=c++20 -Iclient/src tests/f65/queue_test.cpp -o /tmp/f65-queue && /tmp/f65-queue
#include "music_queue.hpp"
#include <cstdio>
#include <cstdlib>
#include <set>

struct Item { std::string id; };
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
static std::vector<Item> make(int n) { std::vector<Item> q; for (int i = 0; i < n; ++i) q.push_back({std::string(1, char('a' + i))}); return q; }
static std::string order(const std::vector<Item>& q) { std::string s; for (auto& i : q) s += i.id; return s; }

int main() {
  using namespace musicqueue;
  size_t to = 99;
  // fim da faixa
  CHECK(advance(0, 3, OFF, to) && to == 1);
  CHECK(!advance(2, 3, OFF, to));
  CHECK(advance(2, 3, ALL, to) && to == 0);
  CHECK(advance(1, 3, ONE, to) && to == 1);
  CHECK(!advance(0, 0, ALL, to));
  // "próxima" apertada: repetir uma faixa não prende
  CHECK(next(1, 3, ONE, to) && to == 2);
  CHECK(!next(2, 3, ONE, to));
  CHECK(next(2, 3, ALL, to) && to == 0);
  // anterior
  CHECK(previous(2, 3, OFF, 10, to) && to == 2);
  CHECK(previous(2, 3, OFF, 1, to) && to == 1);
  CHECK(previous(0, 3, OFF, 1, to) && to == 0);
  CHECK(previous(0, 3, ALL, 1, to) && to == 2);
  // reordenar acompanha a faixa que toca
  auto q = make(4); size_t index = 1;
  CHECK(move(q, index, 2, -1) == 1 && order(q) == "acbd" && index == 2);   // subiu por cima da que toca
  CHECK(move(q, index, 2, +1) == 3 && order(q) == "acdb" && index == 3);   // a que toca desceu junto
  CHECK(move(q, index, 0, -1) == 0 && order(q) == "acdb");                  // no topo não sai da lista
  CHECK(move(q, index, 3, +1) == 3);                                         // no fim também não
  CHECK(move(q, index, 9, +1) == 9);
  // aleatório: mesma coleção, o que já tocou e a atual ficam, a ordem original volta
  q = make(8); index = 2; std::vector<Item> saved;
  shuffle(q, saved, index, 12345);
  CHECK(order(q).substr(0, 3) == "abc");
  std::set<char> set; for (auto& i : q) set.insert(i.id[0]);
  CHECK(set.size() == 8);
  CHECK(order(q) != "abcdefgh");
  size_t before = index; index = 5;                                          // tocando outra do embaralhado
  char playing = q[5].id[0];
  unshuffle(q, saved, index);
  CHECK(order(q) == "abcdefgh" && q[index].id[0] == playing && before == 2);
  q = make(2); shuffle(q, saved, 0, 1); CHECK(order(q) == "ab");
  // janela da lista
  CHECK(windowTop(0, 0, 8, 20) == 0);
  CHECK(windowTop(9, 0, 8, 20) == 2);
  CHECK(windowTop(3, 6, 8, 20) == 3);
  CHECK(windowTop(19, 0, 8, 20) == 12);
  CHECK(windowTop(2, 5, 8, 4) == 0);
  if (failures) { std::printf("%d falhas\n", failures); return 1; }
  std::printf("ok\n"); return 0;
}
