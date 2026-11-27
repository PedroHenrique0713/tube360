// Mapa único do controle (issue #40: um papel por botão). Cada tela pergunta aqui o que cada botão faz no seu contexto e
// qual o rótulo da dica; mudar um papel é mudar uma linha da tabela, e o comportamento e a dica mudam juntos.
// Parte pura (sem desenho): as funções de dica ficam em ui_cards.hpp (`ctlHint`, `ctlFooter`).
#pragma once
#include <initializer_list>
#include "i18n.hpp"

namespace ctl {
using K = i18n::Key;

// Onde o usuário está. Painel = qualquer janela por cima (conta, timer, caixa de som, confirmações, menu de vídeo).
enum class Ctx { Home, Collection, Channel, Search, Player, Shorts, Music, Settings, Panel, Update, PlayerShelf, Count };
enum class Btn { A, B, X, Y, LB, RB, LT, RT, Menu, View, Count };
// O que o botão faz. Pares (PrevTab/NextTab...) têm uma dica só, "LB RB" ou "LT RT".
enum class Act {
  None, Open, PlayPause, Back, WatchLater, AddQueue, Search, Quality, Options, SongVideo,
  PrevTab, NextTab, PrevItem, NextItem, PageUp, PageDown, SeekBack, SeekForward, VolumeDown, VolumeUp, Refresh, Count
};

struct Table { Act a[(int)Btn::Count]; };
//                       A               B          X                Y              LB             RB             LT               RT                 Menu           View
static constexpr Table kMap[(int)Ctx::Count] = {
  /* Home       */ {{Act::Open,      Act::Back, Act::WatchLater, Act::Search,  Act::PrevTab,  Act::NextTab,  Act::PageUp,   Act::PageDown,    Act::Options, Act::None}},
  /* Collection */ {{Act::Open,      Act::Back, Act::WatchLater, Act::Search,  Act::PrevTab,  Act::NextTab,  Act::PageUp,   Act::PageDown,    Act::Options, Act::None}},
  /* Channel    */ {{Act::Open,      Act::Back, Act::WatchLater, Act::Search,  Act::PrevTab,  Act::NextTab,  Act::PageUp,   Act::PageDown,    Act::Options, Act::None}},
  /* Search     */ {{Act::Open,      Act::Back, Act::WatchLater, Act::Search,  Act::PrevTab,  Act::NextTab,  Act::PageUp,   Act::PageDown,    Act::Options, Act::None}},
  /* Player     */ {{Act::PlayPause, Act::Back, Act::WatchLater, Act::Quality, Act::PrevItem, Act::NextItem, Act::SeekBack, Act::SeekForward, Act::Options, Act::None}},
  /* Shorts     */ {{Act::PlayPause, Act::Back, Act::WatchLater, Act::None,    Act::PrevItem, Act::NextItem, Act::VolumeDown, Act::VolumeUp,   Act::Options, Act::None}},
  /* Music      */ {{Act::Open,      Act::Back, Act::AddQueue,   Act::Search,  Act::PrevItem, Act::NextItem, Act::PageUp,   Act::PageDown,    Act::Options, Act::SongVideo}},
  /* Settings   */ {{Act::Open,      Act::Back, Act::None,       Act::None,    Act::PrevTab,  Act::NextTab,  Act::None,     Act::None,        Act::None,    Act::None}},
  /* Panel      */ {{Act::Open,      Act::Back, Act::None,       Act::Search,  Act::None,     Act::None,     Act::None,     Act::None,        Act::None,    Act::None}},
  /* Update     */ {{Act::Open,      Act::Back, Act::None,       Act::None,    Act::None,     Act::None,     Act::None,     Act::None,        Act::Refresh, Act::None}},
  /* PlayerShelf*/ {{Act::Open,      Act::Back, Act::WatchLater, Act::None,    Act::PrevTab,  Act::NextTab,  Act::None,     Act::None,        Act::Options, Act::None}},
};

constexpr Act act(Ctx c, Btn b) { return kMap[(int)c].a[(int)b]; }
// Qual botão faz a ação neste contexto (Count se nenhum).
constexpr Btn button(Ctx c, Act a) {
  for (int i = 0; i < (int)Btn::Count; ++i) if (kMap[(int)c].a[i] == a) return (Btn)i;
  return Btn::Count;
}
constexpr bool has(Ctx c, Act a) { return button(c, a) != Btn::Count; }

// Segunda metade de um par: não tem dica própria (a da primeira mostra "LB RB" ou "LT RT").
constexpr bool pairSecond(Act a) { return a == Act::NextTab || a == Act::NextItem || a == Act::PageDown || a == Act::SeekForward || a == Act::VolumeUp; }

// Rótulo da dica (texto do app; K::None se a ação não tem dica).
constexpr K label(Act a) {
  switch (a) {
    case Act::Open: return K::Open;
    case Act::PlayPause: return K::CtlPlayPause;
    case Act::Back: return K::Back;
    case Act::WatchLater: return K::WatchLater;
    case Act::AddQueue: return K::CtlAddQueue;
    case Act::Search: return K::Search;
    case Act::Quality: return K::PlayerQuality;
    case Act::Options: return K::PlayerOptions;
    case Act::SongVideo: return K::CtlSongVideo;
    case Act::PrevTab: case Act::NextTab: return K::CtlSwitchTab;
    case Act::PrevItem: case Act::NextItem: return K::CtlPrevNext;
    case Act::PageUp: case Act::PageDown: return K::CtlPage;
    case Act::SeekBack: case Act::SeekForward: return K::CtlSeek;
    case Act::VolumeDown: case Act::VolumeUp: return K::CtlVolume;
    case Act::Refresh: return K::CtlCheckAgain;
    default: return K::None;
  }
}

// Botão do mapa -> índice do `Input` do main.cpp (Menu = START, View = BACK).
template <class I> constexpr int inputIndex(Btn b) {
  switch (b) {
    case Btn::A: return I::A; case Btn::B: return I::B; case Btn::X: return I::X; case Btn::Y: return I::Y;
    case Btn::LB: return I::LB; case Btn::RB: return I::RB; case Btn::LT: return I::LT; case Btn::RT: return I::RT;
    case Btn::Menu: return I::START; default: return I::BACK;
  }
}
// O botão que faz `a` neste contexto acabou de ser apertado? (consome o aperto, como `in.hit`)
template <class I> bool hit(I& in, Ctx c, Act a) {
  Btn b = button(c, a);
  return b != Btn::Count && in.hit(inputIndex<I>(b));
}
// Quem aperta o botão `b` no contexto `c` e o que isso é.
template <class I> Act pressed(I& in, Ctx c, Btn b) { return in.hit(inputIndex<I>(b)) ? act(c, b) : Act::None; }
}  // namespace ctl
