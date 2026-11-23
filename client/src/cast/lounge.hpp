#pragma once
#include "yt/client.hpp"
#include <picojson.h>
#include <deque>
#include <functional>
#include <map>

namespace cast {
using std::string;
using Object = picojson::object;
using Command = std::vector<string>;
using Fields = std::map<string, string>;
string form(const Fields&);
string text(const picojson::value&, const string& key);
string clean(const string&, size_t limit = 64);
bool videoId(const string&);

// BrowserChannel entrega quadros com tamanho decimal, inclusive entre cortes de recv/TLS.
class Frames {
    string pending;
public:
    bool feed(const char*, size_t, const std::function<bool(const string&)>&);
};
struct Playback {
    string id;
    double position = 0, duration = 0;
    int state = -1; // lounge: -1 idle, 1 playing, 2 paused, 3 loading, 4 stopped
    uint64_t serial = 0;
};
struct Message { string name; Object payload; };
class Lounge {
    yt::Transport& transport;
    string device, screen, token, sid, gsession, theme = "cl", list, current, cpn, params, ctt;
    int aid = -1, rid = 41000, offset = 0, index = -1, listIndex = -1;
    uint64_t tokenUntil = 0, serial = 0;
    bool seen = false, autoplay = false, supportsAutoplay = false, muted = false;
    double volume = 100;
    Playback actual;
    std::vector<string> queue;
    std::deque<Message> outgoing;
    std::deque<Command> commands;
    std::map<string, string> senders;
    bool request(const string&, const Fields&, string&);
    string query(const string& kind);
    void emit(const string&, Object = {});
    void report();
    void select(int, double = 0);
    void incoming(int, const picojson::array&);
public:
    string code, who;
    explicit Lounge(yt::Transport& t, string identity, string savedScreen = {});
    bool begin(uint64_t now, const string& selectedTheme = "cl");
    bool receive(const string& frame);
    bool flush();
    bool pair(const string& code);
    bool pairingCode();
    void observe(const Playback&);
    void notify() { report(); }
    void ended(const string&, uint64_t serial);
    void reset(bool clearPlayer = false);
    string streamUrl();
    string screenId() const { return screen; }
    bool expired(uint64_t now) const { return now >= tokenUntil; }
    bool needsNext() const;
    string nextInPlaylist();
    bool allowsAutoplay() const { return autoplay; }
    void offerNext(const string& id);
    string currentId() const { return current; }
    bool pop(Command&);
};

// Retorna o status HTTP. A callback recebe cada quadro completo enquanto a conexão está aberta.
long channel(const string& url, const std::atomic_bool& cancel,
             const std::function<bool(const string&)>& frame);
}
