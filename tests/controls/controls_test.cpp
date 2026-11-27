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
    // View só faz algo onde o Menu virou pausa (player, fileira, Shorts, Tocando agora) e onde já era o modo Música/Vídeo
    CHECK(act(cx, Btn::View) == Act::None || cx == Ctx::Player || cx == Ctx::PlayerShelf || cx == Ctx::Shorts || cx == Ctx::Music || cx == Ctx::MusicNow);
    // Menu = pausar/retomar exatamente onde o vídeo ou a música tocam; em nenhum outro contexto
    const bool playing = cx == Ctx::Player || cx == Ctx::PlayerShelf || cx == Ctx::Shorts || cx == Ctx::Music || cx == Ctx::MusicNow;
    CHECK((act(cx, Btn::Menu) == Act::Pause) == playing);
  }
  CHECK(act(Ctx::Home, Btn::X) == Act::WatchLater && act(Ctx::Music, Btn::X) == Act::AddQueue);
  CHECK(act(Ctx::Player, Btn::Y) == Act::Quality && act(Ctx::Home, Btn::Y) == Act::Search);
  CHECK(act(Ctx::Player, Btn::LT) == Act::SeekBack && act(Ctx::Collection, Btn::LT) == Act::PageUp);
  CHECK(act(Ctx::Player, Btn::LB) == Act::PrevItem && act(Ctx::Home, Btn::LB) == Act::PrevTab);
  CHECK(button(Ctx::MusicNow, Act::SongVideo) == Btn::View && !has(Ctx::Home, Act::SongVideo) && !has(Ctx::Music, Act::SongVideo));
  CHECK(act(Ctx::Settings, Btn::Y) == Act::None && act(Ctx::Settings, Btn::LT) == Act::None);  // o que a tela não faz sai do mapa
  CHECK(button(Ctx::Update, Act::Refresh) == Btn::Menu && !has(Ctx::Panel, Act::Refresh) && act(Ctx::Settings, Btn::LB) == Act::PrevTab);
  CHECK(act(Ctx::Shorts, Btn::LT) == Act::VolumeDown && act(Ctx::Shorts, Btn::RT) == Act::VolumeUp && pairSecond(Act::VolumeUp));  // volume nos Shorts
  CHECK(act(Ctx::PlayerShelf, Btn::LB) == Act::PrevTab && act(Ctx::PlayerShelf, Btn::X) == Act::WatchLater);  // abas da fileira do player
  CHECK(act(Ctx::MusicNow, Btn::LB) == Act::PrevTab && act(Ctx::MusicNow, Btn::RB) == Act::NextTab);  // Tocando agora: LB/RB trocam A seguir, Letra e Relacionados
  CHECK(act(Ctx::MusicNow, Btn::LT) == Act::PrevItem && act(Ctx::MusicNow, Btn::RT) == Act::NextItem);  // LT/RT: faixa anterior e próxima
  CHECK(!has(Ctx::MusicNow, Act::VolumeDown) && !has(Ctx::MusicNow, Act::VolumeUp) && act(Ctx::MusicNow, Btn::View) == Act::SongVideo);  // o volume sai dos gatilhos
  CHECK(act(Ctx::MusicPage, Btn::X) == Act::AddQueue && act(Ctx::MusicPage, Btn::LB) == Act::PrevTab && act(Ctx::Music, Btn::LB) == Act::PrevItem);  // páginas de Música x Tocando agora
  // Start pausa e View abre as opções no player, na fileira e nos Shorts; o Menu do Início e das coleções continua sendo as opções
  for (Ctx c : {Ctx::Player, Ctx::PlayerShelf, Ctx::Shorts}) CHECK(button(c, Act::Pause) == Btn::Menu && button(c, Act::Options) == Btn::View && act(c, Btn::A) != Act::Pause);
  CHECK(button(Ctx::Music, Act::Options) == Btn::View && button(Ctx::MusicNow, Act::Pause) == Btn::Menu && !has(Ctx::Home, Act::Pause) && !has(Ctx::Collection, Act::Pause));
  CHECK(label(Act::Pause) == label(Act::PlayPause) && !pairSecond(Act::Pause));
  FakeInput in; in.fired[FakeInput::START] = true;
  CHECK(!hit(in, Ctx::Home, Act::SongVideo));
  CHECK(hit(in, Ctx::Home, Act::Options) && !hit(in, Ctx::Home, Act::Options));
  in.fired[FakeInput::START] = true; CHECK(hit(in, Ctx::Player, Act::Pause) && !hit(in, Ctx::Player, Act::Options) && !hit(in, Ctx::Player, Act::Pause));
  in.fired[FakeInput::BACK] = true; CHECK(hit(in, Ctx::Player, Act::Options) && !hit(in, Ctx::Player, Act::Pause));
  in.fired[FakeInput::BACK] = true; CHECK(hit(in, Ctx::MusicNow, Act::SongVideo));
  std::printf(fails ? "%d falhas\n" : "controls: ok\n", fails);
  return fails != 0;
}
