#pragma once
#include "client.hpp"

namespace yt::sponsorblock {
constexpr size_t maxSegments = 128, maxResponseBytes = 512 * 1024;
struct Segment { double start=0, end=0, duration=0; };
using Segments = std::vector<Segment>;
bool validVideo(const string& id);
string hashPrefix(const string& id);
string requestUrl(const string& id);
Segments parse(const string& body, const string& id);
// Cache limitado à sessão, inclusive respostas vazias e falhas; usado só no worker.
class Client {
    struct Entry { string id; uint64_t expires=0; Segments segments; };
    Transport& net;
    std::vector<Entry> cache;
public:
    explicit Client(Transport& transport) : net(transport) {}
    Segments fetch(const string& id, uint64_t now);
};
}
