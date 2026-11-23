#pragma once
#include "lounge.hpp"
#include <memory>

namespace cast {
struct Status {
    bool online = false, discovery = false;
    string code, who;
    uint64_t revision = 0;
};
class Receiver {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit Receiver(const string& assets);
    ~Receiver();
    void enable(bool);
    // Somente uma cópia do estado por amostra. Nenhuma rede na thread de desenho.
    void publish(const Playback&, const string& ended = {});
    void drain(std::deque<Command>&, Status&);
    void stop();
};
}
