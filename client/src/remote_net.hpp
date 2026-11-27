// Controle pelo celular: o servidor HTTP mínimo (sockets BSD numa thread própria, até 16 conexões lidas em paralelo por select).
// Só aceita a rede local e corta cada conexão em 2 s: um cliente lento não prende os outros nem o laço do app (que não toca nesta
// thread; fala só pela fila do Core).
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <functional>
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
  std::atomic<uint32_t> advertisedIp{0};     // endereço que o painel da TV mostra, em ordem de rede (a thread principal grava, esta só lê); 0 = só o da conexão

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

  struct Conn { int fd; uint32_t peer; std::string raw; uint64_t deadline, born; };
  static constexpr size_t kMaxConns = 4;       // conexões abertas ao mesmo tempo (o app já usa até 15 dos 32 TCP do XAM); as que passam disso são fechadas na hora
  static constexpr size_t kMaxPerIp = 2;       // e no máximo 2 por aparelho
  static constexpr uint64_t kReadMs = 2000;    // tempo total para o pedido chegar inteiro (cliente que goteja é cortado)

  static Reply process(int fd, const std::string& raw, Core& core, uint32_t peer, int port, uint32_t advertisedNet) {
    Reply reply{400, "application/json", "{\"error\":\"bad request\"}"};
    Request r;
    if (!parseRequest(raw, r)) return reply;
    std::string localIp;
    sockaddr_in me{}; socklen_t size = sizeof(me); char ip[INET_ADDRSTRLEN]{};
    if (::getsockname(fd, (sockaddr*)&me, &size) == 0 && ::inet_ntop(AF_INET, &me.sin_addr, ip, sizeof(ip))) localIp = ip;
    // vale o endereço por onde a conexão chegou ou o que o painel da TV mostra (o XAM nem sempre informa o endereço local certo)
    if (localIp == "0.0.0.0") localIp.clear();
    std::string advertised; char adv[INET_ADDRSTRLEN]{};
    if (advertisedNet) { in_addr a{}; a.s_addr = advertisedNet; if (::inet_ntop(AF_INET, &a, adv, sizeof(adv))) advertised = adv; }
    r.hostOk = hostAllowed(r.host, localIp, port) || hostAllowed(r.host, advertised, port);
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
      ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);  // o accept nunca prende a thread (conexão resetada entre o poll e o accept)
      port = chosen;
      std::vector<Conn> conns;
      while (!quit && enabled) {
        // poll (não select): o fd_set do newlib tem 64 bits e os sockets do console começam em 35, então um FD_SET acima de 63 escreveria fora dele
        std::vector<pollfd> fds; fds.push_back({fd, POLLIN, 0});
        for (const auto& c : conns) fds.push_back({c.fd, POLLIN, 0});
        int ready = ::poll(fds.data(), (nfds_t)fds.size(), 100);
        if (ready < 0) { if (errno != EINTR) SDL_Delay(100); continue; }  // falha de verdade: espera, sem laço quente
        uint64_t now = SDL_GetTicks();
        if (fds[0].revents & POLLIN) {
          sockaddr_in peer{}; socklen_t size = sizeof(peer);
          int c = ::accept(fd, (sockaddr*)&peer, &size);
          if (c >= 0) {
            uint32_t ip = ntohl(peer.sin_addr.s_addr);
            size_t same = (size_t)std::count_if(conns.begin(), conns.end(), [&](const Conn& o) { return o.peer == ip; });
            bool full = conns.size() >= kMaxConns || same >= kMaxPerIp;
            if (full) {  // lotou: derruba a mais antiga que ainda não mandou nenhum byte há mais de 300 ms (do mesmo IP se foi ele que lotou)
              auto victim = conns.end();
              for (auto it = conns.begin(); it != conns.end(); ++it)
                if (it->raw.empty() && now - it->born > 300 && (same < kMaxPerIp || it->peer == ip) && (victim == conns.end() || it->born < victim->born)) victim = it;
              if (victim != conns.end()) { ::close(victim->fd); conns.erase(victim); full = false; }
            }
            if (full) ::close(c);
            else conns.push_back({c, ip, {}, now + kReadMs, now});
          } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED) SDL_Delay(100);
        }
        for (size_t i = 0; i < conns.size();) {
          Conn& c = conns[i];
          bool done = false;
          short rev = 0;
          for (const auto& f : fds) if (f.fd == c.fd) rev = f.revents;  // o vetor pode ter encolhido por remoções: busca pelo fd
          if (rev & (POLLIN | POLLHUP | POLLERR)) {
            char buf[1024]; ssize_t n = ::recv(c.fd, buf, sizeof(buf), 0);
            if (n <= 0) done = true;  // fechou sem terminar o pedido
            else {
              c.raw.append(buf, (size_t)n);
              long missing = missingBytes(c.raw);
              if (missing < 0) { respond(c.fd, {400, "application/json", "{\"error\":\"bad request\"}"}); done = true; }
              else if (missing == 0) { respond(c.fd, process(c.fd, c.raw, core, c.peer, chosen, advertisedIp.load())); done = true; }
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
