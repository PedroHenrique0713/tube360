// Controle pelo celular: o servidor HTTP mínimo (sockets BSD numa thread própria, até 16 conexões lidas em paralelo por select).
// Só aceita a rede local e corta cada conexão em 2 s: um cliente lento não prende os outros nem o laço do app (que não toca nesta
// thread; fala só pela fila do Core).
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <vector>
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
  static const char* reason(int s) { return s == 200 ? "OK" : s == 423 ? "Locked" : s == 400 ? "Bad Request" : s == 401 ? "Unauthorized" : s == 403 ? "Forbidden" : s == 404 ? "Not Found" : s == 429 ? "Too Many Requests" : "Error"; }

  struct Conn { int fd; uint32_t peer; std::string raw; uint64_t deadline; };
  static constexpr size_t kMaxConns = 16;      // conexões abertas ao mesmo tempo; as que passam disso são fechadas na hora
  static constexpr uint64_t kReadMs = 2000;    // tempo total para o pedido chegar inteiro (cliente que goteja é cortado)

  static Reply process(int fd, const std::string& raw, Core& core, uint32_t peer, int port) {
    Reply reply{400, "application/json", "{\"error\":\"bad request\"}"};
    Request r;
    if (!parseRequest(raw, r)) return reply;
    std::string localIp;
    sockaddr_in me{}; socklen_t size = sizeof(me); char ip[INET_ADDRSTRLEN]{};
    if (::getsockname(fd, (sockaddr*)&me, &size) == 0 && ::inet_ntop(AF_INET, &me.sin_addr, ip, sizeof(ip))) localIp = ip;
    r.hostOk = hostAllowed(r.host, localIp, port);
    return core.handle(r, peer);
  }
  static void respond(int fd, const Reply& reply) {
    timeval tv{1, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    std::string head = "HTTP/1.1 " + std::to_string(reply.status) + " " + reason(reply.status) + "\r\nContent-Type: " + reply.type +
        "\r\nContent-Length: " + std::to_string(reply.body.size()) + "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'none'; frame-ancestors 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'\r\nConnection: close\r\n\r\n";
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
      std::vector<Conn> conns;
      while (!quit && enabled) {
        fd_set rs; FD_ZERO(&rs); FD_SET(fd, &rs); int top = fd;
        for (const auto& c : conns) { FD_SET(c.fd, &rs); top = std::max(top, c.fd); }
        timeval tv{0, 100000};
        if (::select(top + 1, &rs, nullptr, nullptr, &tv) < 0) continue;
        uint64_t now = SDL_GetTicks();
        if (FD_ISSET(fd, &rs)) {
          sockaddr_in peer{}; socklen_t size = sizeof(peer);
          int c = ::accept(fd, (sockaddr*)&peer, &size);
          if (c >= 0) { if (conns.size() >= kMaxConns) ::close(c); else conns.push_back({c, ntohl(peer.sin_addr.s_addr), {}, now + kReadMs}); }
        }
        for (size_t i = 0; i < conns.size();) {
          Conn& c = conns[i];
          bool done = false;
          if (FD_ISSET(c.fd, &rs)) {
            char buf[1024]; ssize_t n = ::recv(c.fd, buf, sizeof(buf), 0);
            if (n <= 0) done = true;  // fechou sem terminar o pedido
            else {
              c.raw.append(buf, (size_t)n);
              long missing = missingBytes(c.raw);
              if (missing < 0) { respond(c.fd, {400, "application/json", "{\"error\":\"bad request\"}"}); done = true; }
              else if (missing == 0) { respond(c.fd, process(c.fd, c.raw, core, c.peer, chosen)); done = true; }
            }
          }
          if (!done && now >= c.deadline) done = true;
          if (done) { ::close(c.fd); conns.erase(conns.begin() + (long)i); } else ++i;
        }
      }
      for (const auto& c : conns) ::close(c.fd);
      port = 0; ::close(fd);
      if (!enabled) core.reset();
    }
  }
};

}  // namespace phonectl
