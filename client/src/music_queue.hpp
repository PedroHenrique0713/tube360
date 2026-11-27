// Fila da Música: o que o fim da faixa, "próxima", "anterior", repetir, aleatório e reordenar fazem com a lista e a posição
// atual. Parte pura (sem desenho nem rede) para ser testada sozinha em tests/f65.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace musicqueue {
enum Repeat { OFF, ALL, ONE };

// Posição que toca quando a faixa acaba sozinha. Repetir uma faixa volta nela mesma; repetir tudo dá a volta no fim.
inline bool advance(size_t index, size_t size, int repeat, size_t& to) {
  if (!size) return false;
  if (repeat == ONE) { to = std::min(index, size - 1); return true; }
  if (index + 1 < size) { to = index + 1; return true; }
  if (repeat == ALL) { to = 0; return true; }
  return false;
}
// "Próxima" apertada pela pessoa: igual a `advance`, só que repetir uma faixa não prende (anda mesmo assim).
inline bool next(size_t index, size_t size, int repeat, size_t& to) { return advance(index, size, repeat == ONE ? OFF : repeat, to); }
// "Anterior": depois de 3 s volta ao início da mesma faixa; no começo, vai para a faixa de trás (dá a volta só com repetir tudo).
inline bool previous(size_t index, size_t size, int repeat, double position, size_t& to) {
  if (!size) return false;
  to = std::min(index, size - 1);
  if (position <= 3 && index > 0) to = index - 1;
  else if (position <= 3 && repeat == ALL) to = size - 1;
  return true;
}

// Troca a faixa `from` com a vizinha (delta -1 ou +1). `index` acompanha a faixa que está tocando. Devolve a nova posição de `from`.
template <class T>
inline size_t move(std::vector<T>& queue, size_t& index, size_t from, int delta) {
  if (from >= queue.size()) return from;
  long to = (long)from + (delta < 0 ? -1 : 1);
  if (to < 0 || to >= (long)queue.size()) return from;
  std::swap(queue[from], queue[(size_t)to]);
  if (index == from) index = (size_t)to;
  else if (index == (size_t)to) index = from;
  return (size_t)to;
}

// Aleatório: embaralha só o que ainda vai tocar (depois da faixa atual) e guarda a ordem original em `saved`.
template <class T>
inline void shuffle(std::vector<T>& queue, std::vector<T>& saved, size_t index, uint32_t seed) {
  saved = queue;
  uint32_t s = seed ? seed : 0x9e3779b9u;
  auto rnd = [&]() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; };
  for (size_t i = queue.size(); i > index + 2; --i) std::swap(queue[i - 1], queue[index + 1 + rnd() % (i - 1 - index)]);
}
// Desliga o aleatório: volta a ordem original e acha a faixa que está tocando nela.
template <class T>
inline void unshuffle(std::vector<T>& queue, std::vector<T>& saved, size_t& index) {
  if (saved.empty()) return;
  std::string id = index < queue.size() ? queue[index].id : std::string();
  queue = saved; saved.clear();
  for (size_t i = 0; i < queue.size(); ++i) if (queue[i].id == id) { index = i; return; }
  index = std::min(index, queue.size() ? queue.size() - 1 : 0);
}

// Primeira linha visível para manter a seleção na janela de `rows` linhas, andando o mínimo.
inline int windowTop(int selected, int top, int rows, int count) {
  int maxTop = std::max(0, count - rows);
  if (selected < top) top = selected;
  if (selected >= top + rows) top = selected - rows + 1;
  return std::clamp(top, 0, maxTop);
}
}  // namespace musicqueue
