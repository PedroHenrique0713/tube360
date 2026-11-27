// Controle pelo celular: do evento recebido ao botão do mapa (controls.hpp) e o painel com endereço e código de pareamento.
#pragma once
#include <functional>
#include <string>
#include "controls.hpp"
#include "remote_control.hpp"
#include "remote_net.hpp"

namespace phonectl {

// O que o celular não consegue pedir só com um botão do mapa neste contexto. Cada função é preenchida pelo main.cpp.
struct Ports {
  std::function<void()> playPause;                   // pausa/retoma o que está tocando (no vídeo e na música)
  std::function<void(int)> volume;                   // passo de volume (±1) onde o mapa não tem LT/RT de volume
  std::function<void(const std::string&)> search;    // busca já sanitizada
};

// Direções e A valem em toda tela; Voltar e o volume saem do mapa do contexto; o resto vai pelas portas.
template <class I> void inject(const Event& e, I& in, ctl::Ctx ctx, const Ports& p) {
  auto press = [&](ctl::Btn b) { in.fired[ctl::inputIndex<I>(b)] = true; };
  switch (e.action) {
    case Action::Up: in.fired[I::UP] = true; break;
    case Action::Down: in.fired[I::DOWN] = true; break;
    case Action::Left: in.fired[I::LEFT] = true; break;
    case Action::Right: in.fired[I::RIGHT] = true; break;
    case Action::Ok: press(ctl::Btn::A); break;
    case Action::Back: { ctl::Btn b = ctl::button(ctx, ctl::Act::Back); press(b == ctl::Btn::Count ? ctl::Btn::B : b); break; }
    case Action::PlayPause: if (p.playPause) p.playPause(); break;
    case Action::VolumeUp: case Action::VolumeDown: {
      bool up = e.action == Action::VolumeUp;
      ctl::Btn b = ctl::button(ctx, up ? ctl::Act::VolumeUp : ctl::Act::VolumeDown);
      if (b != ctl::Btn::Count) press(b);
      else if (p.volume) p.volume(up ? 1 : -1);
      break;
    }
    case Action::Search: if (!e.text.empty() && p.search) p.search(e.text); break;
  }
}

// Endereço da TV na rede local ("" se não houver rede).
inline std::string localAddress() {
#ifdef __XSDK__
  in_addr address{}; char ip[INET_ADDRSTRLEN]{};
  return !xsdk_net_address(&address) && address.s_addr && address.s_addr != htonl(INADDR_BROADCAST) &&
      ::inet_ntop(AF_INET, &address, ip, sizeof(ip)) ? std::string(ip) : std::string{};
#else
  int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return {};
  sockaddr_in remote{}; remote.sin_family = AF_INET; remote.sin_port = htons(9); ::inet_pton(AF_INET, "192.0.2.1", &remote.sin_addr);
  sockaddr_in local{}; socklen_t size = sizeof(local);
  bool ok = ::connect(fd, (sockaddr*)&remote, sizeof(remote)) == 0 && ::getsockname(fd, (sockaddr*)&local, &size) == 0;
  ::close(fd);
  char ip[INET_ADDRSTRLEN]{};
  return ok && local.sin_addr.s_addr && ::inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip)) ? std::string(ip) : std::string{};
#endif
}

}  // namespace phonectl
