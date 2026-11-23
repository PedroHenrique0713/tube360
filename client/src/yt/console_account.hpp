#pragma once
#include "client.hpp"
#include <atomic>
#include <memory>

namespace yt {
struct AccountReply {
    string body, error;
    bool ok() const { return error.empty(); }
};

// Os workers compartilham uma sessão. Disco, renovação e rede ficam fora do desenho.
class ConsoleAccount {
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic_bool selected{false};
    std::atomic_uint64_t generation{0};
public:
    ConsoleAccount(Transport&, string tokenPath);
    ~ConsoleAccount();
    static bool supports(const string& path);
    AccountReply request(const string& path, int64_t wall = 0, int64_t monotonic = -1);
    string pictureUrl(const string& key);
    void select(bool console) { if (selected.exchange(console) != console) generation.fetch_add(1); }
    uint64_t revision() const { return generation.load(); }
    bool active() const { return selected.load(); }
};

// No PC, XT_ACCOUNT_FIXTURES seleciona respostas sanitizadas e bloqueia a rede desta sessão.
std::unique_ptr<Transport> accountTransport();
}
