// Texto de URL e de linhas separadas por TAB: o que o app usa para montar caminhos e ler listas.
#pragma once
#include <cctype>
#include <string>
#include <vector>

namespace net {

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

inline std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out; std::string cur;
  for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else if (c != '\r') cur += c; }
  out.push_back(cur);
  return out;
}

}  // namespace net
