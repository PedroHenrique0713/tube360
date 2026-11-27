// Controle pelo celular (issue #46): a lógica sem socket nem SDL. Pareamento por código mostrado na TV, sessões por token
// aleatório, fila limitada de ações e as rotas HTTP. O socket fica em remote_net.hpp e a entrada do app em ui_remote.hpp.
// Nada que vem da rede executa além do alfabeto fechado de `Action`; o texto da busca é limitado e sanitizado aqui.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace phonectl {

enum class Action { Up, Down, Left, Right, Ok, Back, PlayPause, VolumeUp, VolumeDown, Search };
struct Event { Action action = Action::Ok; std::string text; };

inline constexpr size_t kMaxQueue = 32;       // ações pendentes; acima disso o toque é descartado (a TV não acompanha)
inline constexpr size_t kMaxSessions = 4;     // celulares pareados ao mesmo tempo
inline constexpr size_t kMaxText = 100;       // bytes da busca
inline constexpr size_t kMaxRequest = 4096;   // cabeçalho + corpo
inline constexpr uint64_t kSessionIdleMs = 30 * 60 * 1000;
inline constexpr int kMaxFailures = 5;        // códigos errados antes do bloqueio
inline constexpr uint64_t kLockoutMs = 30 * 1000;

struct Request { std::string method, path, body, token; bool custom = false; };
struct Reply { int status = 200; std::string type = "application/json", body; };

// Só rede local: 10/8, 172.16/12, 192.168/16, link-local e loopback.
inline bool privateAddress(uint32_t hostOrderIp) {
  uint32_t a = hostOrderIp >> 24, b = (hostOrderIp >> 16) & 255;
  return a == 10 || a == 127 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 169 && b == 254);
}

// UTF-8 válido, sem controles, espaços colapsados, no máximo `kMaxText` bytes sem cortar um caractere ao meio. Vazio = recusa.
inline std::string sanitizeText(const std::string& in) {
  std::string out;
  bool space = true;
  for (size_t i = 0; i < in.size();) {
    unsigned char c = (unsigned char)in[i];
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (!n || i + n > in.size()) { ++i; continue; }
    bool ok = true;
    for (size_t k = 1; k < n; ++k) if (((unsigned char)in[i + k] >> 6) != 2) ok = false;
    if (n == 2 && c < 0xC2) ok = false;                    // sobrelongo
    if (n == 3 && c == 0xE0 && (unsigned char)in[i + 1] < 0xA0) ok = false;
    if (n == 3 && c == 0xED && (unsigned char)in[i + 1] >= 0xA0) ok = false;  // substitutos
    if (n == 4 && (c > 0xF4 || (c == 0xF0 && (unsigned char)in[i + 1] < 0x90) || (c == 0xF4 && (unsigned char)in[i + 1] >= 0x90))) ok = false;
    if (!ok) { ++i; continue; }
    if (n == 1) {
      if (c <= 0x20 || c == 0x7F) { if (!space) out += ' '; space = true; ++i; continue; }
      if (c == '<' || c == '>') { ++i; continue; }
    } else if (n == 2 && c == 0xC2 && (unsigned char)in[i + 1] < 0xA0) { ++i; n = 1; continue; }  // controles C1
    if (out.size() + n > kMaxText) break;
    out.append(in, i, n); space = false; i += n;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

inline std::string jsonEscape(const std::string& s) {
  std::string o;
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  return o;
}

inline std::string formValue(const std::string& body, const std::string& key) {
  size_t pos = 0;
  while (pos <= body.size()) {
    size_t end = body.find('&', pos); if (end == std::string::npos) end = body.size();
    std::string pair = body.substr(pos, end - pos);
    size_t eq = pair.find('=');
    if (eq != std::string::npos && pair.compare(0, eq, key) == 0) {
      std::string raw = pair.substr(eq + 1), o;
      for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '%' && i + 2 < raw.size() + 0 && std::isxdigit((unsigned char)raw[i + 1]) && std::isxdigit((unsigned char)raw[i + 2])) {
          o += (char)std::strtol(raw.substr(i + 1, 2).c_str(), nullptr, 16); i += 2;
        } else if (raw[i] == '+') o += ' ';
        else o += raw[i];
      }
      return o;
    }
    pos = end + 1;
  }
  return {};
}

// Lê um pedido HTTP/1.x já recebido por inteiro. Falso = malformado ou grande demais.
inline bool parseRequest(const std::string& raw, Request& r) {
  if (raw.size() > kMaxRequest) return false;
  size_t headEnd = raw.find("\r\n\r\n");
  if (headEnd == std::string::npos) return false;
  size_t eol = raw.find("\r\n");
  std::string line = raw.substr(0, eol);
  size_t a = line.find(' '), b = line.rfind(' ');
  if (a == std::string::npos || b <= a) return false;
  r.method = line.substr(0, a); r.path = line.substr(a + 1, b - a - 1);
  if (r.path.empty() || r.path[0] != '/' || r.path.size() > 64) return false;
  size_t q = r.path.find('?'); if (q != std::string::npos) r.path.resize(q);
  size_t contentLength = 0, pos = eol + 2;
  while (pos < headEnd) {
    size_t e = raw.find("\r\n", pos); std::string h = raw.substr(pos, e - pos); pos = e + 2;
    size_t c = h.find(':'); if (c == std::string::npos) continue;
    std::string name = h.substr(0, c), value = h.substr(c + 1);
    while (!value.empty() && value.front() == ' ') value.erase(0, 1);
    for (char& ch : name) ch = (char)std::tolower((unsigned char)ch);
    if (name == "content-length") { contentLength = (size_t)std::strtoul(value.c_str(), nullptr, 10); if (contentLength > 512) return false; }
    else if (name == "x-t360") { r.custom = true; r.token = value.substr(0, 64); }
  }
  r.body = raw.substr(headEnd + 4);
  if (r.body.size() < contentLength) return false;
  r.body.resize(contentLength);
  return true;
}
// Quantos bytes ainda faltam para o pedido ficar completo (0 = completo, -1 = inaceitável).
inline long missingBytes(const std::string& raw) {
  if (raw.size() > kMaxRequest) return -1;
  size_t headEnd = raw.find("\r\n\r\n");
  if (headEnd == std::string::npos) return 1;
  std::string head = raw.substr(0, headEnd);
  for (char& ch : head) ch = (char)std::tolower((unsigned char)ch);
  size_t cl = head.find("content-length:"), need = 0;
  if (cl != std::string::npos) need = (size_t)std::strtoul(head.c_str() + cl + 15, nullptr, 10);
  if (need > 512) return -1;
  size_t have = raw.size() - headEnd - 4;
  return have >= need ? 0 : (long)(need - have);
}

class Core {
 public:
  using Random = std::function<void(unsigned char*, size_t)>;
  using Clock = std::function<uint64_t()>;
  using Page = std::function<std::string()>;
  Core(Random random, Clock clock, Page page) : random_(std::move(random)), clock_(std::move(clock)), page_(std::move(page)) { newCode(); }

  void setCodeForTest(const std::string& c) { std::lock_guard<std::mutex> l(m_); code_ = c; }  // só bancada de PC
  std::string code() { std::lock_guard<std::mutex> l(m_); return code_; }
  size_t sessions() { std::lock_guard<std::mutex> l(m_); expire(); return sessions_.size(); }
  void setNowPlaying(const std::string& title, bool playing, bool paused) {
    std::lock_guard<std::mutex> l(m_); title_ = sanitizeText(title); playing_ = playing; paused_ = paused;
  }
  // Desligar a opção: ninguém continua pareado e o código muda.
  void reset() { std::lock_guard<std::mutex> l(m_); sessions_.clear(); queue_.clear(); failures_ = 0; lockedUntil_ = 0; newCode(); }
  std::vector<Event> drain() {
    std::lock_guard<std::mutex> l(m_);
    std::vector<Event> out(queue_.begin(), queue_.end()); queue_.clear(); return out;
  }
  size_t pending() { std::lock_guard<std::mutex> l(m_); return queue_.size(); }

  Reply handle(const Request& r, uint32_t peerHostOrderIp) {
    if (!privateAddress(peerHostOrderIp)) return error(403, "local network only");
    if (r.method == "GET" && (r.path == "/" || r.path == "/index.html")) return {200, "text/html; charset=utf-8", page_()};
    if (!r.custom) return error(400, "bad request");  // exige cabeçalho próprio: um site aberto no celular não consegue disparar isto sem preflight
    std::lock_guard<std::mutex> l(m_);
    uint64_t now = clock_();
    expire();
    if (r.method == "POST" && r.path == "/pair") return pair(r, now);
    Session* s = find(r.token);
    if (!s) return error(401, "not paired");
    s->last = now;
    if (r.method == "GET" && r.path == "/state") {
      return {200, "application/json", std::string("{\"playing\":") + (playing_ ? "true" : "false") + ",\"paused\":" + (paused_ ? "true" : "false") +
              ",\"title\":\"" + jsonEscape(title_) + "\"}"};
    }
    if (r.method == "POST" && r.path == "/cmd") return command(r);
    return error(404, "not found");
  }

 private:
  struct Session { std::string token; uint64_t last = 0; };
  std::mutex m_;
  Random random_; Clock clock_; Page page_;
  std::string code_, title_;
  bool playing_ = false, paused_ = false;
  std::vector<Session> sessions_;
  std::deque<Event> queue_;
  int failures_ = 0; uint64_t lockedUntil_ = 0;

  static Reply error(int status, const char* why) { return {status, "application/json", std::string("{\"error\":\"") + why + "\"}"}; }
  void newCode() {
    unsigned char b[4]; random_(b, sizeof(b));
    uint32_t v = ((uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]) % 1000000u;
    char text[8]; std::snprintf(text, sizeof(text), "%06u", v); code_ = text;
  }
  void expire() {
    uint64_t now = clock_();
    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(), [&](const Session& s) { return now - s.last > kSessionIdleMs; }), sessions_.end());
  }
  static bool same(const std::string& a, const std::string& b) {  // tempo constante para o mesmo tamanho
    if (a.size() != b.size()) return false;
    unsigned char d = 0; for (size_t i = 0; i < a.size(); ++i) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
  }
  Session* find(const std::string& token) {
    if (token.size() != 32) return nullptr;
    Session* hit = nullptr;
    for (auto& s : sessions_) if (same(s.token, token)) hit = &s;
    return hit;
  }
  Reply pair(const Request& r, uint64_t now) {
    if (now < lockedUntil_) return error(429, "locked");
    std::string given = formValue(r.body, "code");
    if (!same(given, code_)) {
      if (++failures_ >= kMaxFailures) { failures_ = 0; lockedUntil_ = now + kLockoutMs; newCode(); return error(429, "locked"); }
      return error(403, "wrong code");
    }
    unsigned char b[16]; random_(b, sizeof(b));
    std::string token; char h[3];
    for (unsigned char c : b) { std::snprintf(h, sizeof(h), "%02x", c); token += h; }
    if (sessions_.size() >= kMaxSessions) sessions_.erase(sessions_.begin());
    sessions_.push_back({token, now});
    failures_ = 0; newCode();  // cada celular novo precisa do código novo que está na TV
    return {200, "application/json", "{\"token\":\"" + token + "\"}"};
  }
  Reply command(const Request& r) {
    std::string a = formValue(r.body, "a");
    Event e;
    if (a == "up") e.action = Action::Up; else if (a == "down") e.action = Action::Down;
    else if (a == "left") e.action = Action::Left; else if (a == "right") e.action = Action::Right;
    else if (a == "ok") e.action = Action::Ok; else if (a == "back") e.action = Action::Back;
    else if (a == "playpause") e.action = Action::PlayPause;
    else if (a == "volup") e.action = Action::VolumeUp; else if (a == "voldown") e.action = Action::VolumeDown;
    else if (a == "search") {
      e.action = Action::Search; e.text = sanitizeText(formValue(r.body, "q"));
      if (e.text.empty()) return error(400, "empty text");
    } else return error(400, "unknown action");
    if (queue_.size() >= kMaxQueue) return error(429, "busy");
    queue_.push_back(std::move(e));
    return {200, "application/json", "{\"ok\":true}"};
  }
};

}  // namespace phonectl
