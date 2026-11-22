#pragma once
#include <string>
#include "net.hpp"

struct Account {
  enum State { SIGNED_OUT, PENDING, SIGNED_IN } state = SIGNED_OUT;
  std::string name, code, url;
  bool signedIn() const { return state == SIGNED_IN; }
  bool parse(std::string line) {
    size_t nl = line.find('\n'); if (nl != std::string::npos) line.resize(nl);
    auto f = net::split(line, '\t');
    if (f.size() < 5 || f[0] != "ok" || (f[1] != "signed_out" && f[1] != "pending" && f[1] != "signed_in")) return false;
    state = f[1] == "signed_in" ? SIGNED_IN : f[1] == "pending" ? PENDING : SIGNED_OUT;
    name = f[2]; code = f[3]; url = f[4]; return true;
  }
};
