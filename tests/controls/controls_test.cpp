// Valida o mapa único de botões (client/src/controls.hpp, issue #40). Build: g++ -std=c++20 -Iclient/src
#include <cstdio>
#include "controls.hpp"
struct FakeInput { enum { UP, DOWN, LEFT, RIGHT, A, B, X, Y, START, BACK, LB, RB, LT, RT, N }; bool fired[N] = {};
  bool hit(int b) { bool f = fired[b]; fired[b] = false; return f; } };
int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHOU: %s (linha %d)\n", #c, __LINE__); ++fails; } } while (0)
int main() {
  using namespace ctl;
  for (int c = 0; c < (int)Ctx::Count; ++c) {
    Ctx cx = (Ctx)c;
    CHECK(act(cx, Btn::B) == Act::Back);                 // B volta em todo lugar
    CHECK(act(cx, Btn::View) != Act::Back);
    for (int a = 1; a < (int)Act::Count; ++a) {          // nenhuma ação em dois botões
      int n = 0; for (int b = 0; b < (int)Btn::Count; ++b) n += act(cx, Btn(b)) == Act(a);
      CHECK(n <= 1);
    }
    for (int b = 0; b < (int)Btn::Count; ++b) {          // toda ação mapeada tem rótulo
      Act a = act(cx, Btn(b)); CHECK(a == Act::None || label(a) != i18n::Key::None);
    }
    CHECK(act(cx, Btn::X) != Act::Search && act(cx, Btn::Y) != Act::WatchLater);  // X nunca é busca, Y nunca é salvar
    CHECK(act(cx, Btn::View) == Act::None || cx == Ctx::Music);                    // View só faz algo na Música
  }
  CHECK(act(Ctx::Home, Btn::X) == Act::WatchLater && act(Ctx::Music, Btn::X) == Act::AddQueue);
  CHECK(act(Ctx::Player, Btn::Y) == Act::Quality && act(Ctx::Home, Btn::Y) == Act::Search);
  CHECK(act(Ctx::Player, Btn::LT) == Act::SeekBack && act(Ctx::Collection, Btn::LT) == Act::PageUp);
  CHECK(act(Ctx::Player, Btn::LB) == Act::PrevItem && act(Ctx::Home, Btn::LB) == Act::PrevTab);
  CHECK(button(Ctx::Music, Act::SongVideo) == Btn::View && !has(Ctx::Home, Act::SongVideo));
  CHECK(act(Ctx::Settings, Btn::Y) == Act::None && act(Ctx::Settings, Btn::LT) == Act::None);  // o que a tela não faz sai do mapa
  CHECK(button(Ctx::Panel, Act::Refresh) == Btn::Menu && act(Ctx::Settings, Btn::LB) == Act::PrevTab);
  FakeInput in; in.fired[FakeInput::START] = true;
  CHECK(!hit(in, Ctx::Home, Act::SongVideo));
  CHECK(hit(in, Ctx::Home, Act::Options) && !hit(in, Ctx::Home, Act::Options));
  in.fired[FakeInput::BACK] = true; CHECK(hit(in, Ctx::Music, Act::SongVideo));
  std::printf(fails ? "%d falhas\n" : "controls: ok\n", fails);
  return fails != 0;
}
