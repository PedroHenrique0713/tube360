// Controle pelo celular: o servidor HTTP mínimo (sockets BSD, um pedido por vez, numa thread própria). Só aceita a rede local
// e fecha cada conexão em 2 s: um cliente lento não prende o laço (que nem toca nesta thread; fala só pela fila do Core).
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <SDL3/SDL.h>
#include "remote_control.hpp"

namespace phonectl {

inline constexpr int kFirstPort = 8135;  // DIAL do cast usa 8134; tenta mais 4 se estiver ocupada
inline constexpr int kPortTries = 5;

struct Net {
  std::atomic_bool quit{false}, enabled{false};
  std::atomic<int> port{0};      // 0 = não está ouvindo
  int firstPort = kFirstPort;    // testes usam outra

  static bool sendAll(int fd, const std::string& s) {
    size_t off = 0;
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    while (off < s.size()) {
      ssize_t n = ::send(fd, s.data() + off, s.size() - off, flags);
      if (n <= 0) return false;
      off += (size_t)n;
    }
    return true;
  }
  static const char* reason(int s) { return s == 200 ? "OK" : s == 400 ? "Bad Request" : s == 401 ? "Unauthorized" : s == 403 ? "Forbidden" : s == 404 ? "Not Found" : s == 429 ? "Too Many Requests" : "Error"; }

  static void serveOne(int fd, Core& core, uint32_t peer) {
    timeval tv{1, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    const uint64_t deadline = SDL_GetTicks() + 2000;
    std::string raw; char buf[1024];
    Reply reply{400, "application/json", "{\"error\":\"bad request\"}"};
    for (;;) {
      long missing = missingBytes(raw);
      if (missing < 0) break;
      if (missing == 0) {
        Request r;
        if (parseRequest(raw, r)) reply = core.handle(r, peer);
        break;
      }
      if (SDL_GetTicks() > deadline) break;
      ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) return;
      raw.append(buf, (size_t)n);
    }
    std::string head = "HTTP/1.1 " + std::to_string(reply.status) + " " + reason(reply.status) + "\r\nContent-Type: " + reply.type +
        "\r\nContent-Length: " + std::to_string(reply.body.size()) + "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'\r\nConnection: close\r\n\r\n";
    sendAll(fd, head + reply.body);
  }

  // Corpo da thread. Volta quando `quit`. Ligar e desligar `enabled` abre e fecha a porta.
  void run(Core& core) {
    while (!quit) {
      if (!enabled) { SDL_Delay(150); continue; }
      int fd = ::socket(AF_INET, SOCK_STREAM, 0);
      int chosen = 0;
      if (fd >= 0) {
        int yes = 1; ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        for (int i = 0; i < kPortTries && !chosen; ++i) {
          sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((uint16_t)(firstPort + i));
          if (::bind(fd, (sockaddr*)&a, sizeof(a)) == 0 && ::listen(fd, 4) == 0) chosen = firstPort + i;
        }
      }
      if (!chosen) {
        if (fd >= 0) ::close(fd);
        for (int i = 0; i < 20 && !quit && enabled; ++i) SDL_Delay(150);
        continue;
      }
      port = chosen;
      while (!quit && enabled) {
        fd_set rs; FD_ZERO(&rs); FD_SET(fd, &rs); timeval tv{0, 200000};
        if (::select(fd + 1, &rs, nullptr, nullptr, &tv) <= 0) continue;
        sockaddr_in peer{}; socklen_t size = sizeof(peer);
        int c = ::accept(fd, (sockaddr*)&peer, &size);
        if (c < 0) continue;
        serveOne(c, core, ntohl(peer.sin_addr.s_addr));
        ::close(c);
      }
      port = 0; ::close(fd);
      if (!enabled) core.reset();
    }
  }
};

}  // namespace phonectl
