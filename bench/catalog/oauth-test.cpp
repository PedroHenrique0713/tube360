#include "yt/catalog.hpp"
#include <deque>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace yt::catalog;
using yt::string;
namespace {
int checks = 0;
void check(bool ok, const char* what) {
  ++checks;
  if (!ok) { std::cerr << "FAIL " << what << '\n'; std::exit(1); }
}
string read(const string& dir, const string& name) {
  std::ifstream f(dir+"/"+name); check(bool(f),"fixture exists");
  std::ostringstream bytes; bytes << f.rdbuf(); return bytes.str();
}
Object headers(const yt::Request& r) {
  Object out{{"user-agent",J(r.agent)}};
  if (!r.body.empty()) out["content-type"] = J(string("application/json"));
  for (const auto& h : r.headers) {
    auto colon = h.find(':'); check(colon != string::npos,"header syntax");
    string key = h.substr(0,colon), value = h.substr(colon+1);
    std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c) { return std::tolower(c); });
    value.erase(0,value.find_first_not_of(' '));
    if (!value.empty()) out[key] = J(value);
  }
  return out;
}
struct Http : yt::Transport {
  struct Step { string url, reply; int status; J oracle; };
  std::deque<Step> steps;
  std::vector<yt::Request> sent;
  void add(string url, string reply, int status = 200, J oracle = {}) { steps.push_back({url,reply,status,oracle}); }
  yt::Response request(const string& url, const string& body, bool form) override { return send({url,body,{},form,{}}); }
  yt::Response send(const yt::Request& r) override {
    check(!steps.empty(),"no unexpected bootstrap or retry");
    auto s = steps.front(); steps.pop_front(); sent.push_back(r);
    check(r.url == s.url,"host, route and query match oracle");
    if (s.oracle.is<Object>()) {
      check(!r.body.empty() && str(s.oracle,"method") == "POST","method matches oracle");
      J body; check(parse(r.body,body),"native request parses");
      check(body == get(s.oracle,"body"),"complete JSON equals real youtubei.js output");
      auto actual = headers(r), expected = get(s.oracle,"headers").get<Object>();
      check(str(J(actual),"authorization") == "Bearer console-fixture-token","console fake token sent");
      check(str(J(expected),"authorization") == "Bearer fixture-token","server fake token captured");
      actual.erase("authorization"); expected.erase("authorization");
      check(actual == expected,"all effective headers equal; only token differs");
      check(std::find(r.headers.begin(),r.headers.end(),"Cookie:") != r.headers.end(),"cookie jar explicitly suppressed");
    }
    return {s.status,s.reply,{}};
  }
};
const string sw = "https://www.youtube.com/sw.js_data";
const string guest = "https://music.youtube.com/";
const string browse = "youtubei/v1/browse?prettyPrint=false";
std::map<string,string> q(string kind = "music") { return {{"kind",kind},{"lang","pt"}}; }
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  string dir = argv[1], session = read(dir,"tv-session.txt"), home = read(dir,"music-home.json");
  J capture; check(parse(read(dir,"oauth-requests.json"),capture),"oracle parses");
  const auto& requests = array(get(capture,"requests")); check(requests.size() == 8,"nonempty oracle with every route");
  Http h; Client c(h); check(c.setToken("console-fixture-token"),"set fake console token");
  for (auto invalid : {string("bad\r\nheader"),string("bad token"),string("bad\0token",9),string(8193,'x'),string("bad\x7f")})
    check(!c.setToken(invalid),"unsafe token rejected without changing active account");
  auto exchange = [&](size_t i, const string& file) { const auto& r = requests.at(i); h.add(str(r,"url"),read(dir,file),200,r); };
  h.add(sw,session); exchange(0,"music-home.json");
  auto page = c.browse(q()); check(page.error.empty() && !page.items.empty(),"authenticated Home decoded");
  auto more = q(); more["offset"] = std::to_string(page.items.size());
  exchange(1,"continuation.json"); check(!c.browse(more).items.empty(),"authenticated continuation decoded");
  auto charts = q(); charts["q"] = "charts"; exchange(2,"music-explore.json"); exchange(3,"music-charts.json");
  check(!c.browse(charts).items.empty(),"authenticated charts decoded");
  auto search = q("music_search"); search["q"] = "jazz instrumental"; exchange(4,"music-search.json");
  check(!c.browse(search).items.empty(),"authenticated search decoded");
  auto radio = q("radio"); radio["id"] = "F12audio001"; exchange(5,"radio.json");
  check(!c.browse(radio).items.empty(),"authenticated radio decoded");
  exchange(6,"lyrics-next.json"); exchange(7,"lyrics.json");
  check(c.handle("/api/nav/lyrics?id=F12audio001&lang=pt").find("Letra de teste") != string::npos,"authenticated lyrics decoded");
  check(h.steps.empty() && h.sent.size() == 9 && c.authError.empty(),"one shared bootstrap; every captured request consumed");
  for (int status : {401,403}) {
    Http fallback; Client f(fallback); f.setToken("console-fixture-token");
    fallback.add(sw,session); fallback.add(str(requests[0],"url"),"{}",status,requests[0]);
    fallback.add(guest,read(dir,"music-context.html")); fallback.add(guest+browse,home);
    check(!f.browse(q()).items.empty() && f.authError == "browse HTTP "+std::to_string(status),"refused token keeps public Music available");
    auto requestHeaders = headers(fallback.sent.back());
    check(!requestHeaders.count("authorization") && !requestHeaders.count("cookie"),"guest retry has no token and uses its own cookie jar");
    auto fresh = q(); fresh["fresh"] = "1"; fallback.add(guest+browse,home);
    check(!f.browse(fresh).items.empty(),"no repeated rejected token");
    f.setToken("new-fake-token"); fallback.add(sw,session); fallback.add(str(requests[0],"url"),home);
    check(!f.browse(q()).items.empty() && f.authError.empty(),"refresh clears rejection and public cache");
    check(str(J(headers(fallback.sent.back())),"authorization") == "Bearer new-fake-token","refresh token reaches Music");
    f.setToken(""); fallback.add(guest,read(dir,"music-context.html")); fallback.add(guest+browse,home);
    check(!f.browse(q()).items.empty() && fallback.steps.empty(),"logout restores public context");
  }
  for (int status : {429,500,503}) {
    Http fail; Client f(fail); f.setToken("console-fixture-token"); fail.add(sw,session);
    fail.add(str(requests[0],"url"),"{}",status,requests[0]);
    check(f.browse(q()).error == "HTTP "+std::to_string(status) && f.authError.empty() && fail.steps.empty(),"transient errors never demote account");
  }
  for (auto malformed : {string(")]}'[]"),string(")]}'{}"),string("bad"),string(")]}'")+string(81,'[')+string(81,']')}) {
    Http fail; Client f(fail); f.setToken("console-fixture-token"); fail.add(sw,malformed);
    check(f.browse(q()).error == "invalid_context" && fail.steps.empty(),"malformed session fails before sending token");
  }
  Http legacy; Client w(legacy); w.setToken("console-fixture-token");
  legacy.add("https://www.youtube.com/",read(dir,"web-context.html"));
  legacy.add("https://www.youtube.com/"+browse,"{}",401); legacy.add("https://www.youtube.com/"+browse,read(dir,"playlist.json"));
  auto playlist = q("playlist"); playlist["id"] = "PLF12"; check(!w.browse(playlist).items.empty(),"legacy web rejection reproduced");
  legacy.add(sw,session); legacy.add(str(requests[0],"url"),home,200,requests[0]);
  check(!w.browse(q()).items.empty() && legacy.steps.empty() && w.authError.empty(),"rejection by web client does not suppress Music account or leave a stale warning");
  std::cout << checks << " OAuth checks passed\n";
}
