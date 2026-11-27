// Controle pelo celular (#46): pareamento, recusa sem código, sanitização, fila limitada, injeção pelo mapa e o servidor de verdade
// com um cliente TCP falso (inclusive cliente lento e inundação). Build: tests/remote/run.py
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include "ui_remote.hpp"
#include "remote_page.hpp"

using namespace phonectl;
struct FakeInput { enum { UP, DOWN, LEFT, RIGHT, A, B, X, Y, START, BACK, LB, RB, LT, RT, N }; bool fired[N] = {};
  bool hit(int b) { bool f = fired[b]; fired[b] = false; return f; } };
int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHOU: %s (linha %d)\n", #c, __LINE__); ++fails; } } while (0)

static uint64_t fakeNow = 1000;
static Core make(unsigned char seed = 7) {
  static unsigned char counter; counter = seed;
  return Core([](unsigned char* b, size_t n) { for (size_t i = 0; i < n; ++i) b[i] = (unsigned char)(counter++ * 37 + i * 11); },
              [] { return fakeNow; }, [] { return std::string("<html>page</html>"); });
}
static Request post(const std::string& path, const std::string& body, const std::string& token = "pair") { return {"POST", path, body, token, true}; }
static std::string tokenOf(const Reply& r) { size_t a = r.body.find("\"token\":\""); return a == std::string::npos ? "" : r.body.substr(a + 9, 32); }
constexpr uint32_t LAN = (192u << 24) | (168u << 16) | 5;

static std::string http(int port, const std::string& req, int waitMs = 1500) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(fd, (sockaddr*)&a, sizeof(a)) != 0) { ::close(fd); return "CONNECT_FAIL"; }
  timeval tv{waitMs / 1000, (waitMs % 1000) * 1000}; ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ::send(fd, req.data(), req.size(), 0);
  std::string out; char buf[2048]; ssize_t n;
  while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) out.append(buf, (size_t)n);
  ::close(fd); return out;
}
static std::string rq(const char* m, const char* p, const std::string& body = "", const char* token = nullptr, const std::string& host = "127.0.0.1") {
  std::string r = std::string(m) + " " + p + " HTTP/1.1\r\nHost: " + host + "\r\n";
  if (token) r += std::string("X-T360: ") + token + "\r\n";
  r += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body; return r;
}

int main() {
  // ---- sanitização do texto
  CHECK(sanitizeText("  hello   world \n") == "hello world");
  CHECK(sanitizeText("a<script>b") == "ascriptb");
  CHECK(sanitizeText(std::string("x\0y\x01z", 5)) == "x y z");
  CHECK(sanitizeText("caf\xc3\xa9 \xe2\x82\xac") == "caf\xc3\xa9 \xe2\x82\xac");
  CHECK(sanitizeText("bad\xff\xfe" "ok") == "badok");
  CHECK(sanitizeText(std::string(500, 'a')).size() == kMaxText);
  { std::string t; for (int i = 0; i < 80; ++i) t += "\xc3\xa9"; std::string o = sanitizeText(t); CHECK(o.size() <= kMaxText && o.size() % 2 == 0); }  // não corta caractere no meio
  CHECK(sanitizeText("   \t ").empty());
  CHECK(sanitizeText("\xed\xa0\x80").empty());  // substituto UTF-16 inválido
  // ---- rede local
  CHECK(privateAddress(LAN) && privateAddress((10u << 24) | 1) && privateAddress((172u << 24) | (20u << 16)) && privateAddress(0x7f000001u));
  CHECK(!privateAddress((8u << 24) | (8u << 16) | (8u << 8) | 8) && !privateAddress((172u << 24) | (32u << 16)) && !privateAddress((200u << 24)));
  // ---- pareamento
  {
    Core c = make(); c.openPairing(); std::string code = c.code();
    CHECK(code.size() == 6 && code.find_first_not_of("0123456789") == std::string::npos);
    Request page{"GET", "/", "", "", false};
    CHECK(c.handle(page, LAN).status == 200 && c.handle(page, LAN).type.rfind("text/html", 0) == 0);
    CHECK(c.handle(page, (8u << 24) | 8).status == 403);                                         // fora da rede local
    CHECK(c.handle({"POST", "/cmd", "a=ok", "x", false}, LAN).status == 400);                      // sem o cabeçalho próprio
    CHECK(c.handle(post("/cmd", "a=ok", "pair"), LAN).status == 401);                              // sem sessão
    CHECK(c.handle(post("/cmd", "a=ok", "00000000000000000000000000000000"), LAN).status == 401);  // token inventado
    CHECK(c.pending() == 0);
    auto bad = c.handle(post("/pair", "code=" + std::string(code == "000000" ? "111111" : "000000")), LAN);
    CHECK(bad.status == 403 && c.sessions() == 0);
    auto ok = c.handle(post("/pair", "code=" + code), LAN);
    std::string tok = tokenOf(ok);
    CHECK(ok.status == 200 && tok.size() == 32 && c.sessions() == 1);
    CHECK(c.code() != code);                                                                      // o código muda a cada pareamento
    CHECK(c.handle(post("/pair", "code=" + code), LAN).status == 423);                            // o código velho não vale mais
    CHECK(c.handle(post("/cmd", "a=ok", tok), LAN).status == 200 && c.pending() == 1);
    CHECK(c.handle(post("/cmd", "a=format_disk", tok), LAN).status == 400 && c.pending() == 1);  // alfabeto fechado
    CHECK(c.handle(post("/cmd", "a=search&q=" + std::string("%3Cb%3Elo-fi%20beats"), tok), LAN).status == 200);
    CHECK(c.handle(post("/cmd", "a=search&q=%20%20", tok), LAN).status == 400);
    auto ev = c.drain();
    CHECK(ev.size() == 2 && ev[0].action == Action::Ok && ev[1].action == Action::Search && ev[1].text == "blo-fi beats" && c.pending() == 0);
    c.setNowPlaying("Song \"x\"", true, false);
    auto st = c.handle({"GET", "/state", "", tok, true}, LAN);
    CHECK(st.status == 200 && st.body.find("\\\"x\\\"") != std::string::npos);
    // sessão expira por inatividade
    fakeNow += kSessionIdleMs + 1;
    CHECK(c.handle(post("/cmd", "a=ok", tok), LAN).status == 401 && c.sessions() == 0);
    fakeNow += 1000;
  }
  // ---- pareamento só com o painel aberto, 5 erros mudam o código sem trancar o dono
  {
    Core c = make(5);
    const uint32_t ATT = (192u << 24) | (168u << 16) | 99, OWN = (192u << 24) | (168u << 16) | 7;
    CHECK(!c.pairingOpen() && c.handle(post("/pair", "code=" + c.code()), OWN).status == 423 && c.sessions() == 0);  // painel fechado
    c.openPairing(); std::string first = c.code();
    CHECK(c.pairingOpen());
    c.closePairing(); CHECK(c.handle(post("/pair", "code=" + first), OWN).status == 423);
    c.openPairing(); CHECK(c.code() != first);                                                    // código novo a cada abertura
    fakeNow += kPairingOpenMs + 1; CHECK(!c.pairingOpen() && c.handle(post("/pair", "code=" + c.code()), OWN).status == 423);  // fecha sozinho
    c.openPairing(); std::string code = c.code(); unsigned notices = c.wrongCodeNotices();
    int last = 0; for (int i = 0; i < kMaxFailures; ++i) last = c.handle(post("/pair", "code=999999"), ATT).status;
    CHECK(last == 429 && c.code() != code && c.wrongCodeNotices() == notices + 1);              // 5 erros: código novo e aviso
    CHECK(c.handle(post("/pair", "code=" + c.code()), ATT).status == 429);                        // o atacante espera, mesmo com o código certo
    CHECK(c.handle(post("/pair", "code=" + c.code()), OWN).status == 200 && c.sessions() == 1);   // o dono pareia no meio do ataque
    fakeNow += kIpBlockMs + 1;
    c.openPairing(); CHECK(c.handle(post("/pair", "code=" + c.code()), ATT).status == 200);        // passado o minuto o atacante volta a poder tentar
    // erros de IPs diferentes também chegam a 5 e mudam o código
    c.openPairing(); std::string before = c.code();
    for (uint32_t k = 0; k < (uint32_t)kMaxFailures; ++k) c.handle(post("/pair", "code=111111"), (10u << 24) | (k + 1));
    CHECK(c.code() != before);
  }
  // ---- Host
  CHECK(hostAllowed("192.168.0.5", "192.168.0.5", 8135) && hostAllowed("192.168.0.5:8135", "192.168.0.5", 8135));
  CHECK(!hostAllowed("evil.example:8135", "192.168.0.5", 8135) && !hostAllowed("192.168.0.5:9999", "192.168.0.5", 8135) && !hostAllowed("", "192.168.0.5", 8135) && !hostAllowed("192.168.0.5", "", 8135));
  { Core c = make(2); Request r{"GET", "/", "", "", false}; r.hostOk = false; CHECK(c.handle(r, LAN).status == 400); }
  CHECK(sanitizeText("a\xe2\x80\x8b" "b\xe2\x80\xae" "c\xe2\x81\xa6" "d\xef\xbb\xbf" "e") == "abcde");   // ZWSP, override RTL, isolate, BOM
  CHECK(sanitizeText("\xe2\x82\xac") == "\xe2\x82\xac");
  // ---- fila limitada
  {
    Core c = make(9); c.openPairing(); auto tok = tokenOf(c.handle(post("/pair", "code=" + c.code()), LAN));
    int busy = 0; for (int i = 0; i < 100; ++i) busy += c.handle(post("/cmd", "a=down", tok), LAN).status == 429;
    CHECK(c.pending() == kMaxQueue && busy == 100 - (int)kMaxQueue);
    CHECK(c.drain().size() == kMaxQueue);
    // mais de 4 celulares: o mais antigo cai
    std::string first = tok;
    for (size_t i = 0; i < kMaxSessions; ++i) { c.openPairing(); tokenOf(c.handle(post("/pair", "code=" + c.code()), LAN)); }
    CHECK(c.sessions() == kMaxSessions && c.handle(post("/cmd", "a=ok", first), LAN).status == 401);
    c.reset(); CHECK(c.sessions() == 0);
  }
  // ---- parser HTTP
  { Request r; CHECK(parseRequest("POST /cmd HTTP/1.1\r\nX-T360: abc\r\nContent-Length: 4\r\n\r\na=okEXTRA", r) && r.body == "a=ok" && r.token == "abc" && r.custom);
    CHECK(!parseRequest("GET / HTTP/1.1\r\nContent-Length: 99999\r\n\r\n", r));
    CHECK(!parseRequest(std::string("GET /") + std::string(5000, 'a') + " HTTP/1.1\r\n\r\n", r));
    CHECK(!parseRequest("garbage", r));
    CHECK(missingBytes("POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\nabc") == 7 && missingBytes("GET / HTTP/1.1\r\n") == 1); }
  // ---- injeção pelo mapa (nenhuma tecla crua: o botão sai de controls.hpp)
  {
    int volume = 0, pauses = 0; std::string searched;
    Ports p; p.playPause = [&] { ++pauses; }; p.volume = [&](int s) { volume += s; }; p.search = [&](const std::string& q) { searched = q; };
    FakeInput in;
    inject<FakeInput>({Action::Up, ""}, in, ctl::Ctx::Home, p); CHECK(in.hit(FakeInput::UP));
    inject<FakeInput>({Action::Ok, ""}, in, ctl::Ctx::Player, p); CHECK(in.hit(FakeInput::A));
    inject<FakeInput>({Action::Back, ""}, in, ctl::Ctx::Music, p); CHECK(in.hit(FakeInput::B));
    inject<FakeInput>({Action::VolumeUp, ""}, in, ctl::Ctx::Shorts, p); CHECK(in.hit(FakeInput::RT) && volume == 0);   // Shorts: RT do mapa
    inject<FakeInput>({Action::VolumeDown, ""}, in, ctl::Ctx::Shorts, p); CHECK(in.hit(FakeInput::LT));
    inject<FakeInput>({Action::VolumeUp, ""}, in, ctl::Ctx::Player, p); CHECK(volume == 1);                              // sem LT/RT de volume: porta
    inject<FakeInput>({Action::VolumeDown, ""}, in, ctl::Ctx::Home, p); CHECK(volume == 0);
    inject<FakeInput>({Action::PlayPause, ""}, in, ctl::Ctx::Player, p); CHECK(pauses == 1);
    inject<FakeInput>({Action::Search, "x y"}, in, ctl::Ctx::Home, p); CHECK(searched == "x y");
    inject<FakeInput>({Action::Search, ""}, in, ctl::Ctx::Home, p); CHECK(searched == "x y");
    for (int b = 0; b < FakeInput::N; ++b) CHECK(!in.fired[b]);
  }
  // ---- servidor de verdade
  {
    Core core = make(21); core.openPairing();
    Net net; net.firstPort = 18135; net.enabled = true;
    std::thread th([&] { net.run(core); });
    for (int i = 0; i < 50 && !net.port; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int port = net.port; CHECK(port == 18135);
    std::string page = http(port, rq("GET", "/"));
    CHECK(page.find("200 OK") != std::string::npos && page.find("page") != std::string::npos && page.find("frame-ancestors 'none'") != std::string::npos);
    CHECK(http(port, rq("POST", "/cmd", "a=ok", "pair")).find("401") != std::string::npos);
    CHECK(http(port, rq("POST", "/cmd", "a=ok")).find("400") != std::string::npos);  // sem o cabeçalho
    CHECK(http(port, rq("GET", "/", "", nullptr, "evil.example:" + std::to_string(port))).find("400 Bad Request") != std::string::npos);  // Host de outro nome (rebinding)
    CHECK(http(port, rq("GET", "/", "", nullptr, "127.0.0.1:" + std::to_string(port))).find("200 OK") != std::string::npos);
    CHECK(http(port, "GET / HTTP/1.0\r\n\r\n").find("400 Bad Request") != std::string::npos);                                    // sem Host
    CHECK(http(port, "GET / HTTP/1.1\r\nHost: x\r\nContent-Length: 99999\r\n\r\n").find("400") != std::string::npos);
    CHECK(http(port, "lixo sem estrutura\r\n\r\n").find("400") != std::string::npos);
    CHECK(http(port, rq("POST", "/pair", "code=000000", "pair")).find("403") != std::string::npos);
    std::string r = http(port, rq("POST", "/pair", "code=" + core.code(), "pair"));
    std::string tok = r.find("\"token\":\"") != std::string::npos ? r.substr(r.find("\"token\":\"") + 9, 32) : "";
    CHECK(tok.size() == 32);
    CHECK(http(port, rq("POST", "/cmd", "a=right", tok.c_str())).find("200 OK") != std::string::npos);
    CHECK(core.pending() == 1);
    // cliente lento (segura a conexão aberta sem terminar o pedido): a fila e o laço do app seguem livres
    int slow = ::socket(AF_INET, SOCK_STREAM, 0); sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::connect(slow, (sockaddr*)&a, sizeof(a)); ::send(slow, "POST /cmd HTTP/1.1\r\nContent-Le", 31, 0);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) { core.drain(); core.pending(); core.setNowPlaying("t", true, false); }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 100);                                                                              // o laço do app nunca espera o servidor
    auto t1 = std::chrono::steady_clock::now();
    CHECK(http(port, rq("GET", "/"), 4000).find("200 OK") != std::string::npos);                  // o cliente lento solta em <= ~2 s
    auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t1).count();
    CHECK(waited < 3500);
    ::close(slow);
    // inundação: 300 comandos, a fila segura no teto e a TV só vê o que cabe
    int ok200 = 0, busy = 0;
    for (int i = 0; i < 300; ++i) { auto x = http(port, rq("POST", "/cmd", "a=down", tok.c_str())); ok200 += x.find("200 OK") != std::string::npos; busy += x.find("429") != std::string::npos; }
    CHECK(core.pending() <= kMaxQueue && ok200 + busy == 300 && busy > 0);
    // desligar a opção fecha a porta e derruba as sessões
    net.enabled = false;
    for (int i = 0; i < 50 && net.port; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(net.port == 0 && http(18135, rq("GET", "/"), 300) == "CONNECT_FAIL" && core.sessions() == 0);
    net.quit = true; th.join();
  }
  // ---- página: 4 idiomas, sem recurso externo
  for (auto lang : {i18n::Lang::EN, i18n::Lang::PT, i18n::Lang::ES, i18n::Lang::AR}) {
    std::string h = page(lang);
    CHECK(h.find("http://") == std::string::npos && h.find("https://") == std::string::npos && h.find("<script src") == std::string::npos);
    CHECK(h.find(std::string("lang=\"") + i18n::code(lang) + "\"") != std::string::npos && h.size() < 12000);
    CHECK((h.find("dir=\"rtl\"") != std::string::npos) == (lang == i18n::Lang::AR));
  }
  std::printf(fails ? "%d falhas\n" : "remote: ok\n", fails);
  return fails != 0;
}
