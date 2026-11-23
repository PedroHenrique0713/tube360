// HTTP/1.0 mínimo por sockets BSD bloqueantes: o mesmo código no Fedora e no 360 (sockets do xsdk).
// Só fala com o servidor da casa (docs/PROTOCOLO.md), sem TLS.
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace net {

struct Server { std::string ip = "0.0.0.0"; int port = 8091; };  // server.txt ao lado do .xex: "<IP do PC> <porta>"

inline std::atomic_bool serverEnabled{true};

inline int connectTo(const Server& s, int timeoutMs) {
  if (!serverEnabled) return -1;
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)s.port);
  if (inet_pton(AF_INET, s.ip.c_str(), &a.sin_addr) != 1) { ::close(fd); return -1; }
  timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  if (::connect(fd, (sockaddr*)&a, sizeof(a)) != 0) { ::close(fd); return -1; }
  return fd;
}

inline bool sendAll(int fd, const std::string& s) {
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

// Percent-encoding UTF-8 para query string.
inline std::string urlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string o;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
  }
  return o;
}

inline std::string urlDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
    else if (s[i] == '+') o += ' ';
    else o += s[i];
  }
  return o;
}

struct Response { int status = 0; std::string headers; std::vector<uint8_t> body; };

// Não fecha o fd: o player também pode interromper a espera pelos cabeçalhos durante uma busca ou saída.
inline bool readGet(int fd, const Server& s, const std::string& path, Response& r, std::vector<uint8_t>& extra) {
  if (!sendAll(fd, "GET " + path + " HTTP/1.0\r\nHost: " + s.ip + "\r\nUser-Agent: Tube360/1.0\r\n\r\n")) return false;
  std::vector<uint8_t> buf;
  uint8_t tmp[8192];
  for (;;) {
    ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
    if (n <= 0) return false;
    buf.insert(buf.end(), tmp, tmp + n);
    for (size_t i = 0; i + 3 < buf.size(); i++) {
      if (std::memcmp(&buf[i], "\r\n\r\n", 4) == 0) {
        r.headers.assign((const char*)buf.data(), i);
        std::sscanf(r.headers.c_str(), "HTTP/%*d.%*d %d", &r.status);
        extra.assign(buf.begin() + (long)i + 4, buf.end());
        return true;
      }
    }
    if (buf.size() > 65536) return false;
  }
}

inline int openGet(const Server& s, const std::string& path, int timeoutMs, Response& r, std::vector<uint8_t>& extra) {
  int fd = connectTo(s, timeoutMs);
  if (fd >= 0 && !readGet(fd, s, path, r, extra)) { ::close(fd); return -1; }
  return fd;
}

inline std::string header(const Response& r, const char* name) {
  std::string h = r.headers, key = std::string("\r\n") + name + ":";
  for (auto& c : h) c = (char)std::tolower((unsigned char)c);
  for (auto& c : key) c = (char)std::tolower((unsigned char)c);
  size_t p = h.find(key);
  if (p == std::string::npos) return "";
  p += key.size();
  size_t e = r.headers.find("\r\n", p);
  std::string v = r.headers.substr(p, e == std::string::npos ? std::string::npos : e - p);
  while (!v.empty() && v[0] == ' ') v.erase(0, 1);
  return v;
}

// GET inteiro (respostas pequenas: feed, miniatura, poll).
inline bool get(const Server& s, const std::string& path, Response& r, int timeoutMs = 8000, size_t maxBytes = 4 << 20) {
  std::vector<uint8_t> extra;
  int fd = openGet(s, path, timeoutMs, r, extra);
  if (fd < 0) return false;
  r.body = std::move(extra);
  uint8_t tmp[16384];
  for (;;) {
    ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
    if (n < 0) { ::close(fd); return false; }
    if (n == 0) break;
    r.body.insert(r.body.end(), tmp, tmp + n);
    if (r.body.size() > maxBytes) break;
  }
  ::close(fd);
  return r.status == 200;
}

inline std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out; std::string cur;
  for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else if (c != '\r') cur += c; }
  out.push_back(cur);
  return out;
}

}  // namespace net
