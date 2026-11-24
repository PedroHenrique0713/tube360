#pragma once
#include "yt/thread.hpp"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace nest {
struct Config {
    std::string host, name = "Nest", uuid, model;
    int port = 8009;
    bool load(const std::string& dir);
    bool save(const std::string& dir) const;
};
struct Network { uint32_t address = 0, mask = 0; }; // ordem do host
bool localNetwork(Network&);
std::vector<std::string> addresses(Network);
enum class SearchState { Idle, Searching, Done, Cancelled, NetworkError, Incomplete, SaveError };
struct SearchResult {
    SearchState state = SearchState::Idle;
    std::vector<Config> devices;
    uint64_t revision = 0;
};
SearchResult scan(Network, const std::atomic_bool& cancel, const Config& known = {});
bool identify(Config&, const std::atomic_bool& cancel);
int matching(const Config&, const std::vector<Config>&);

// Um worker e até 16 sockets de sondagem; TLS/JSON nunca passam pela thread de desenho.
class Discovery {
    SDL_Mutex* mutex = nullptr;
    SDL_Condition* condition = nullptr;
    SDL_Thread* thread = nullptr;
    std::atomic_bool cancelFlag{false};
    bool quit = false, requested = false, manual = false, verified = false;
    uint64_t selection = 0, retryAt = 0;
    std::string dir;
    Config config;
    SearchResult result;
    void run();
public:
    ~Discovery() { stop(); }
    void init(const std::string&);
    void stop();
    void search();
    void cancel();
    void recover();
    Config selected();
    bool configured();
    bool ready();
    bool choose(const Config&);
    bool poll(SearchResult&);
};
}
