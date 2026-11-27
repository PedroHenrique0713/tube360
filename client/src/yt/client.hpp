// Uma instância por worker de rede. O dono pode cancelar uma operação pela flag.
#pragma once
#include "membership.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <time.h>

namespace updates { struct Rules; }
namespace yt {
using std::string;
enum class Failure { NONE, NETWORK, DNS, TLS, TLS_DATE, HTTP, JSON, REFUSAL, SESSION, CANCELLED };
struct NetworkTiming {
    double dnsMs=-1, tcpMs=-1, tlsMs=-1, firstByteMs=-1, callbackGapMs=0;
    long osError=0;
    int nonblocking=-1;
    const char* phase="dns";
};
using NetworkTrace=std::function<void(const char*,double)>;
struct Response {
    long status = 0;
    string body, error, contentRange, location;
    long newConnections = 0;
    Failure failure = Failure::NONE;
    bool retryable = false;
    double seconds = 0;
    int receiveBuffer = 0, sendBuffer = 0;
    long transportCode = 0, tlsVerify = -1;
    int tlsCode = 0;
    string transportDetail;
    NetworkTiming timing;
    // Cabeçalhos X-* dos segmentos de live (-1 = ausente): X-Head-Seqnum, X-Sequence-Num, X-Head-Time-Millis, X-Walltime-Ms.
    long long headSeq = -1, segmentSeq = -1, headTimeMs = -1, wallMs = -1;
};
// Pedido com identidade própria: user agent, cabeçalhos extras e os cookies que o worker já recebeu.
struct Request {
    string url, body, agent;
    bool form = false;
    std::vector<string> headers;
    bool freshConnection = false, media = false;
    long readBuffer = 64 * 1024;
    bool update = false;
    size_t maxBytes = 0;
    long connectTimeoutMs = 0, timeoutMs = 0;
    NetworkTrace trace;
};
struct Transport {
    virtual ~Transport() = default;
    virtual Response request(const string& url, const string& body = {}, bool form = false) = 0;
    virtual Response send(const Request& r) { return request(r.url, r.body, r.form); }
    virtual bool cancelled() const { return false; }
    virtual void cancel() {}
    virtual bool backoff(unsigned ms);
};
inline const char* failureName(Failure f) {
    switch (f) {
    case Failure::NETWORK: return "network";
    case Failure::TLS: return "tls";
    case Failure::TLS_DATE: return "tls_date";
    case Failure::DNS: return "dns";
    case Failure::HTTP: return "http";
    case Failure::JSON: return "json";
    case Failure::REFUSAL: return "refusal";
    case Failure::SESSION: return "session";
    case Failure::CANCELLED: return "cancelled";
    default: return "none";
    }
}
inline Failure failureOf(const Response& r) {
    if (r.failure != Failure::NONE) return r.failure;
    if (r.error == "cancelled") return Failure::CANCELLED;
    if (!r.error.empty()) return r.error.find("tls") != string::npos ? Failure::TLS : Failure::NETWORK;
    return r.status != 200 && r.status != 206 ? Failure::HTTP : Failure::NONE;
}
inline bool retryable(const Response& r) {
    if (r.failure != Failure::NONE) return r.retryable;
    if (!r.error.empty()) return failureOf(r)==Failure::NETWORK && r.error!="invalid_media_redirect";
    return r.status==408 || r.status==429 || r.status==500 || r.status==502 || r.status==503 || r.status==504;
}
inline bool Transport::backoff(unsigned ms) {
    const timespec delay{0,25000000};
    for (unsigned elapsed=0; elapsed<ms && !cancelled(); elapsed+=25) nanosleep(&delay,nullptr);
    return !cancelled();
}
std::unique_ptr<Transport> https(const std::atomic_bool* cancel = nullptr);

struct Card { string id, title; Membership members = Membership::None; bool shortForm = false; string channel, views, date; int duration = -1; };  // duration: -1 desconhecida, 0 ao vivo
struct Track { string url, language, name; };
struct Cue { int startMs, durationMs; string text; };
struct Format { string url; int itag = 0, height = 0; double fps = 0; int bitrate = 0, width = 0; };
struct AudioTrack {
    string code, language, name;
    bool original = false, automatic = false, preferred = false;
    Format format;
};
inline string audioCode(const string& language, bool original, bool automatic) {
    return (language.empty() ? "und" : language) + (original ? "-original" : automatic ? "-auto" : "-dub");
}
struct OfferCount {
    unsigned listed = 0, direct = 0, muxedAac = 0;
};
struct MediaOffers { OfferCount avc720, avc480, aac; };
struct Video {
    string id, title, status, reason, hls, channel, channelId, date;
    string playerClient, playerTrace;
    Failure failure = Failure::NONE;
    unsigned playerAttempts = 0;
    int ttl = 0, duration = 0;
    int64_t expiresAt = 0;
    bool highFrameRate720 = false, live = false;
    // Live sem HLS (só adaptiveFormats com live=1): segmentos &sq=N (membros). Vem da conta, com o `n` já resolvido.
    bool liveSeq = false;
    bool nsigSolved = false;  // havia `n` nos links e o serviço o resolveu
    unsigned qualityModes = 1;
    Format video, audio, speakerAudio, fallbackVideo;
    std::vector<Format> lowerVideo, videoLadder;
    std::vector<AudioTrack> audioTracks;
    string selectedAudio;
    bool muxed = false;
    std::vector<Track> captions;
};
struct Page { std::vector<Card> cards; string error; Failure failure = Failure::NONE; };
class Client {
    Transport& net;
    string context, language = "en";
    std::shared_ptr<const updates::Rules> policy;
    void bindRules();
    // Sessão de visitante (cookies ficam no transporte) e o signatureTimestamp do player JS atual.
    string visitor, playerJs, cacheBase;
    int sts = 0;
    Failure sessionFailure = Failure::NONE;
    bool sessionRetryable = false;
    bool session(const string& id, string& error, bool fresh = false);
    bool visitorFallback(string& js);
    Response post(const string& route, const string& fields);
    Page page(const string& route, const string& fields, const string& exclude = {});
public:
    explicit Client(Transport& t, string cache = {}) : net(t), cacheBase(std::move(cache)) {}
    void setLanguage(const string& value);
    Page home();
    Page search(const string& query);
    Page next(const string& id);
    // signatureTimestamp do player TV (o mesmo programa que resolve o `n`). 0 se o serviço não carrega: o chamador cai na mensagem de membros.
    int nsigTimestamp();
    Video player(const string& id, int height = 720, MediaOffers* offers = nullptr, int mode = 0, const string& audio = "original", const string& accountPlayer = {});
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
