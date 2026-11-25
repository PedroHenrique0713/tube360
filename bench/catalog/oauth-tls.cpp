#include "yt/catalog.hpp"
#include <iostream>

using yt::string;
struct Loopback : yt::Transport {
  std::unique_ptr<yt::Transport> tls = yt::https();
  string origin;
  explicit Loopback(const char* port) : origin("https://127.0.0.1:"+string(port)) {}
  yt::Response request(const string& url, const string& body, bool form) override { return send({url,body,{},form,{}}); }
  yt::Response send(const yt::Request& r) override {
    auto forwarded = r;
    const auto slash = r.url.find('/',8);
    forwarded.url = origin + r.url.substr(slash);
    forwarded.headers.push_back("Host: " + r.url.substr(8,slash-8));
    return tls->send(forwarded);
  }
};
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  Loopback net(argv[1]);
  if (net.request("https://www.youtube.com/seed",{},false).status != 200) return 3;
  yt::catalog::Client c(net); c.setToken("console-fixture-token");
  auto page = c.browse({{"kind","music"},{"lang","pt"}});
  if (!page.error.empty() || page.items.empty() || page.musicSource!="tv") { std::cerr << page.error << '\n'; return 4; }
  // O pote precisa continuar existindo para os pedidos públicos posteriores.
  if (net.request("https://www.youtube.com/probe",{},false).status != 200) return 5;
  std::cout << page.items.size() << " Music items via production TLS\n";
}
