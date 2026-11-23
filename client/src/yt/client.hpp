// Uma instância por worker de rede. O dono pode cancelar uma operação pela flag.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace yt {
using std::string;
struct Response {
    long status = 0;
    string body, error;
};
// Pedido com identidade própria: user agent, cabeçalhos extras e os cookies que o worker já recebeu.
struct Request {
    string url, body, agent;
    bool form = false;
    std::vector<string> headers;
};
struct Transport {
    virtual ~Transport() = default;
    virtual Response request(const string& url, const string& body = {}, bool form = false) = 0;
    virtual Response send(const Request& r) { return request(r.url, r.body, r.form); }
};
std::unique_ptr<Transport> https(const std::atomic_bool* cancel = nullptr);

struct Card { string id, title; };
struct Track { string url, language, name; };
struct Cue { int startMs, durationMs; string text; };
struct Format { string url; int itag = 0, height = 0; double fps = 0; };
struct OfferCount {
    unsigned listed = 0, direct = 0, muxedAac = 0;
};
struct MediaOffers { OfferCount avc720, avc480, aac; };
struct Video {
    string id, title, status, reason, hls;
    int ttl = 0;
    Format video, audio;
    bool muxed = false;
    std::vector<Track> captions;
};
struct Page { std::vector<Card> cards; string error; };
class Client {
    Transport& net;
    string context;
    // Sessão de visitante (cookies ficam no transporte) e o signatureTimestamp do player JS atual.
    string visitor, playerJs;
    int sts = 0;
    bool session(const string& id, string& error);
    Response post(const string& route, const string& fields);
    Page page(const string& route, const string& fields, const string& exclude = {});
public:
    explicit Client(Transport& t) : net(t) {}
    Page home();
    Page search(const string& query);
    Page next(const string& id);
    Video player(const string& id, int height = 720, MediaOffers* offers = nullptr);
    bool captions(const Track&, std::vector<Cue>&, string& error);
};

// Tempos em segundos de um relógio monotônico do chamador. Uma chamada poll faz no máximo um POST.
struct DeviceCode {
    string device, user, verificationUrl;
    int interval = 5;
    int64_t expires = 0, nextPoll = 0;
};
struct Tokens { string access, refresh; int64_t expires = 0; };
class DeviceLogin {
    Transport& net;
    string clientId, secret;
public:
    DeviceLogin(Transport& t, string id, string clientSecret)
        : net(t), clientId(id), secret(clientSecret) {}
    string start(int64_t now, DeviceCode&);
    string poll(int64_t now, DeviceCode&, Tokens&);
    string refresh(int64_t now, Tokens&);
};
}
