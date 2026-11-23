#pragma once
#include "catalog.hpp"
#ifndef __XSDK__
#include <fstream>
#include <sstream>
#endif
namespace yt::catalog {
#ifndef __XSDK__
// A reprodução de respostas só existe no PC. Pedido sem gravação falha fechado.
class Replay final : public Transport {
  string directory;
  J manifest;
  string file(const string& name) {
    if (name.empty() || name.find_first_of("/\\") != string::npos || name == "..") return {};
    std::ifstream in(directory+"/"+name,std::ios::binary);
    std::ostringstream bytes; bytes << in.rdbuf(); return bytes.str();
  }
public:
  explicit Replay(string dir) : directory(std::move(dir)) { parse(file("responses.json"),manifest); }
  Response request(const string& url, const string& body = {}, bool form = false) override {
    Request r; r.url = url; r.body = body; r.form = form; return send(r);
  }
  Response send(const Request& r) override {
    J args; if (!r.body.empty()) parse(r.body,args);
    for (const auto& entry : array(get(manifest,"requests"))) {
      if (str(entry,"url") != r.url) continue;
      bool matches = true;
      if (get(entry,"fields").is<Object>()) for (const auto& kv : get(entry,"fields").get<Object>()) if (get(args,kv.first) != kv.second) matches = false;
      if (!matches) continue;
      const auto& authorization = get(entry,"authorization");
      if (authorization.is<string>()) {
        string expected = str(authorization), actual;
        for (const auto& header : r.headers) if (header.compare(0,15,"Authorization: ") == 0) actual = header.substr(15);
        bool accepted = actual == (expected.empty() ? "" : "Bearer "+expected);
        if (const auto* log = std::getenv("XT_CATALOG_AUDIT")) if (auto* f = std::fopen(log,"a")) {
          std::fprintf(f,"%s\t%s\n",r.url.c_str(),accepted ? "authorized_fixture" : "rejected_fixture"); std::fclose(f);
        }
        if (!accepted) return {401,"{}",{}};
      }
      string bytes = file(str(entry,"file"));
      if (bytes.empty()) return {0,{},"replay_file_missing"};
      if (flag(entry,"range")) {
        long long begin = 0, end = 0;
        for (const auto& h : r.headers) if (std::sscanf(h.c_str(),"Range: bytes=%lld-%lld",&begin,&end) == 2) {
          if (begin < 0 || begin >= int64_t(bytes.size()) || end < begin) return {416,{}, {}};
          end = std::min<long long>(end,int64_t(bytes.size())-1);
          return {206,bytes.substr(size_t(begin),size_t(end-begin+1)),{}," bytes "+std::to_string(begin)+"-"+std::to_string(end)+"/"+std::to_string(bytes.size())};
        }
      }
      const auto& status = get(entry,"status");
      return {status.is<double>() ? long(status.get<double>()) : 200,std::move(bytes),{}};
    }
    return {0,{},"unrecorded_request"};
  }
};
#endif
inline std::unique_ptr<Transport> transport(const std::atomic_bool* cancel = nullptr) {
#ifndef __XSDK__
  if (const char* dir = std::getenv("XT_CATALOG_REPLAY")) return std::make_unique<Replay>(dir);
#endif
  return yt::https(cancel);
}
}
