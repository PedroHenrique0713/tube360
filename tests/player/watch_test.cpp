// Vigia do player: o que conta como travado e, principalmente, o que não conta. Sem SDL.
#include "../../client/src/player_watch.hpp"
#include <cassert>
#include <cstdio>

using P = PlayerWatch::Phase;
static PlayerWatch::Sample at(uint64_t ms, P phase, double pos = 0) {
  PlayerWatch::Sample s; s.now = ms; s.inPlayer = true; s.phase = phase; s.position = pos; return s;
}

int main() {
  { // CONNECTING eterno: só depois do limite
    PlayerWatch w; w.update(at(0, P::Connecting));
    assert(!w.update(at(119000, P::Connecting)) && !w.stuck);
    assert(w.update(at(120000, P::Connecting)) && w.stuck);
    assert(!w.update(at(130000, P::Connecting)) && w.stuck);          // continua marcado, sem novo evento
    assert(w.update(at(131000, P::Playing, 1)) && !w.stuck);          // voltou a tocar: limpa
  }
  { // carregamento lento que termina antes do limite não marca
    PlayerWatch w; w.update(at(0, P::Connecting));
    w.update(at(100000, P::Connecting)); w.update(at(100100, P::Playing, 0));
    assert(!w.update(at(150000, P::Playing, 49)) && !w.stuck);
  }
  { // decodificação travada: posição parada com o vídeo "tocando"
    PlayerWatch w; w.update(at(0, P::Playing, 10));
    w.update(at(10000, P::Playing, 20));
    assert(!w.update(at(54000, P::Playing, 20)));
    assert(w.update(at(55000, P::Playing, 20)) && w.stuck);
    assert(w.update(at(56000, P::Playing, 21)) && !w.stuck);
  }
  { // pausa longa nunca marca, e retomar não herda o tempo parado
    PlayerWatch w; w.update(at(0, P::Playing, 10));
    auto s = at(1000, P::Playing, 10); s.paused = true;
    for (uint64_t t = 1000; t < 600000; t += 1000) { s.now = t; assert(!w.update(s)); }
    s.paused = false; s.now = 600000; assert(!w.update(s));
    s.now = 640000; assert(!w.update(s) && !w.stuck);
  }
  { // live com posição parada não marca; Nest e busca idem
    for (int kind = 0; kind < 3; ++kind) {
      PlayerWatch w; w.update(at(0, P::Playing, 5));
      auto s = at(0, P::Playing, 5);
      s.live = kind == 0; s.nest = kind == 1; s.seeking = kind == 2;
      for (uint64_t t = 1000; t < 300000; t += 1000) { s.now = t; assert(!w.update(s)); }
    }
  }
  { // BUFFERING: só marca quando passa do limite sem nenhuma posição nova
    PlayerWatch w; w.update(at(0, P::Buffering, 3));
    assert(!w.update(at(179000, P::Buffering, 3)));
    assert(w.update(at(180000, P::Buffering, 3)) && w.stuck);
  }
  { // fora do player ou ocioso limpa e rearma
    PlayerWatch w; w.update(at(0, P::Connecting)); w.update(at(130000, P::Connecting));
    assert(w.stuck);
    auto s = at(131000, P::Connecting); s.inPlayer = false;
    assert(w.update(s) && !w.stuck);
    assert(!w.update(at(300000, P::Connecting)));                     // contagem recomeça do zero
  }
  { // fim de stream / áudio mais curto: posição parada, mas o pipeline terminou ou ainda produz quadros
    PlayerWatch w; auto s = at(0, P::Playing, 20); w.update(s);
    s.streamEnded = true;
    for (uint64_t t = 1000; t < 200000; t += 1000) { s.now = t; assert(!w.update(s)); }
    PlayerWatch v; auto f = at(0, P::Playing, 20); v.update(f);
    for (uint64_t t = 1000; t < 200000; t += 1000) { f.now = t; f.frames = t; assert(!v.update(f)); }  // decodificando, relógio parado
  }
  { // leitura de rede ainda andando: abertura lenta não é travada; sem nenhuma atividade é
    PlayerWatch w; auto s = at(0, P::Connecting); w.update(s);
    for (uint64_t t = 1000; t < 300000; t += 1000) { s.now = t; s.bytes = t; assert(!w.update(s)); }
    for (uint64_t t = 300000; t < 419000; t += 1000) { s.now = t; assert(!w.update(s)); }
    s.now = 420000; assert(w.update(s) && w.stuck);
  }
  { // travado de verdade: sem posição, sem bytes, sem quadros, stream não terminou
    PlayerWatch w; auto s = at(0, P::Playing, 20); s.bytes = 5; s.frames = 7; w.update(s);
    s.now = 44000; assert(!w.update(s));
    s.now = 45000; assert(w.update(s) && w.stuck);
  }
  std::puts("PASS player watch: 10 scenarios");
}
