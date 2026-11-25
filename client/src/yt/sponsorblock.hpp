#pragma once
#include "client.hpp"

namespace yt::sponsorblock {
constexpr size_t maxSegments = 128, maxResponseBytes = 512 * 1024;
constexpr long connectTimeoutMs=2500, requestTimeoutMs=3500;
struct Segment { double start=0, end=0, duration=0; };
using Segments = std::vector<Segment>;
bool validVideo(const string& id);
string hashPrefix(const string& id);
string requestUrl(const string& id);
enum class State { SEGMENTS, EMPTY, INVALID_ID, CANCELLED, TLS, NETWORK, HTTP, PARSE };
const char* stateName(State state);
struct Diagnostic {
    State state=State::EMPTY;
    string reason, prefix, detail;
    long http=0, transport=0, verify=-1;
    int64_t epoch=0;
    int tls=0;
    size_t bytes=0, bucket=0, rejected=0;
    bool matched=false, cached=false;
    double seconds=0;
    NetworkTiming timing;
    string line() const;
};
Segments parse(const string& body, const string& id, Diagnostic* diagnostic=nullptr);
// Cache limitado à sessão, inclusive respostas vazias e falhas; usado só no worker.
class Client {
    struct Entry { string id; uint64_t expires=0; Segments segments; Diagnostic diagnostic; };
    Transport& net;
    std::vector<Entry> cache;
    Diagnostic last;
public:
    NetworkTrace trace;
    explicit Client(Transport& transport) : net(transport) {}
    Segments fetch(const string& id, uint64_t now, bool refresh=false);
    const Diagnostic& diagnostic() const { return last; }
};
}
