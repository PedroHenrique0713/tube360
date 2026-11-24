#include "cast.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <sys/select.h>

namespace cast {
namespace {
constexpr size_t limit = 64 * 1024;
using Clock = std::chrono::steady_clock;
void varint(std::string& out, uint64_t value) {
    while (value >= 128) { out += char((value & 127) | 128); value >>= 7; }
    out += char(value);
}
void field(std::string& out, unsigned id, const std::string& value) {
    varint(out, (id << 3) | 2); varint(out, value.size()); out += value;
}
bool number(const std::string& in, size_t& at, uint64_t& value) {
    value = 0;
    for (unsigned shift = 0; shift < 64 && at < in.size(); shift += 7) {
        auto byte = uint8_t(in[at++]);
        if (shift == 63 && byte > 1) return false;
        value |= uint64_t(byte & 127) << shift;
        if (!(byte & 128)) return true;
    }
    return false;
}
bool decode(const std::string& bytes, Message& message) {
    size_t at = 0;
    bool version = false, text = false;
    message = {};
    while (at < bytes.size()) {
        uint64_t key, n;
        if (!number(bytes, at, key) || !(key >> 3)) return false;
        unsigned id = unsigned(key >> 3), wire = key & 7;
        if (wire == 0) {
            if (!number(bytes, at, n)) return false;
            if (id == 1) { if (n != 0) return false; version = true; }
            if (id == 5) { if (n != 0) return false; text = true; }
        } else if (wire == 2) {
            if (!number(bytes, at, n) || n > bytes.size() - at) return false;
            std::string* target = id == 2 ? &message.source : id == 3 ? &message.destination :
                                  id == 4 ? &message.space : id == 6 ? &message.json : nullptr;
            if (target) target->assign(bytes, at, size_t(n));
            at += size_t(n);
        } else if (wire == 1 || wire == 5) {
            size_t size = wire == 1 ? 8 : 4;
            if (size > bytes.size() - at) return false;
            at += size;
        } else return false;
    }
    return version && text && !message.source.empty() && !message.destination.empty() &&
           !message.space.empty() && !message.json.empty();
}
}
struct Connection::Impl {
    CURL* easy = nullptr;
    const std::atomic_bool* cancel = nullptr;
    curl_socket_t socket = CURL_SOCKET_BAD;
    std::string pending;
    bool wait(bool writing, int ms) {
        fd_set set;
        FD_ZERO(&set); FD_SET(socket, &set);
        timeval timeout{ms / 1000, (ms % 1000) * 1000};
        return select(int(socket) + 1, writing ? nullptr : &set, writing ? &set : nullptr, nullptr, &timeout) > 0;
    }
};
Connection::Connection() : impl(std::make_unique<Impl>()) {}
Connection::~Connection() { close(); }
void Connection::close() {
    if (impl->easy) curl_easy_cleanup(impl->easy);
    impl->easy = nullptr; impl->socket = CURL_SOCKET_BAD; impl->pending.clear();
}
bool Connection::open(const std::string& host, int port, int timeoutMs, const std::atomic_bool* cancel) {
    close();
    if (host.empty() || host.size() > 253 || host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != std::string::npos ||
        port < 1 || port > 65535) return false;
    static const auto init = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (init != CURLE_OK || !(impl->easy = curl_easy_init())) return false;
    impl->cancel = cancel;
    auto* easy = impl->easy;
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, cancel);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, +[](void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
        return p && static_cast<const std::atomic_bool*>(p)->load() ? 1 : 0;
    });
    auto url = "https://" + host + ":" + std::to_string(port) + "/";
    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy, CURLOPT_CONNECT_ONLY, 1L);
    // Cast apresenta certificado próprio. Esta exceção fica no canal LAN configurado.
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(easy, CURLOPT_PROXY, "");
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, long(timeoutMs));
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, long(timeoutMs));
    if (curl_easy_perform(easy) != CURLE_OK ||
        curl_easy_getinfo(easy, CURLINFO_ACTIVESOCKET, &impl->socket) != CURLE_OK || impl->socket == CURL_SOCKET_BAD) {
        close(); return false;
    }
    return true;
}
bool Connection::send(const std::string& source, const std::string& destination,
                      const std::string& space, const std::string& json) {
    if (!impl->easy) return false;
    std::string body;
    body += char(8); body += char(0);
    field(body, 2, source); field(body, 3, destination); field(body, 4, space);
    body += char(40); body += char(0); field(body, 6, json);
    if (body.size() > limit) return false;
    std::string frame;
    for (int shift = 24; shift >= 0; shift -= 8) frame += char(body.size() >> shift);
    frame += body;
    size_t at = 0;
    auto deadline = Clock::now() + std::chrono::milliseconds(750);
    while (at < frame.size()) {
        if (impl->cancel && impl->cancel->load()) { close(); return false; }
        size_t sent = 0;
        auto rc = curl_easy_send(impl->easy, frame.data() + at, frame.size() - at, &sent);
        if (rc != CURLE_OK && rc != CURLE_AGAIN) { close(); return false; }
        at += sent;
        if (at < frame.size()) {
            if (Clock::now() >= deadline) { close(); return false; }
            impl->wait(true, 20);
        }
    }
    return true;
}
int Connection::read(Message& message, int timeoutMs) {
    if (!impl->easy) return -1;
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (impl->cancel && impl->cancel->load()) { close(); return -1; }
        auto& pending = impl->pending;
        if (pending.size() >= 4) {
            uint32_t length = 0;
            for (int i = 0; i < 4; ++i) length = (length << 8) | uint8_t(pending[i]);
            if (!length || length > limit) { close(); return -1; }
            if (pending.size() >= length + 4) {
                bool ok = decode(pending.substr(4, length), message);
                pending.erase(0, length + 4);
                if (!ok) { close(); return -1; }
                return 1;
            }
        }
        char buffer[8192]; size_t received = 0;
        auto rc = curl_easy_recv(impl->easy, buffer, sizeof(buffer), &received);
        if ((rc != CURLE_OK && rc != CURLE_AGAIN) || (rc == CURLE_OK && !received)) { close(); return -1; }
        if (received) pending.append(buffer, received);
        else if (Clock::now() < deadline) impl->wait(false, std::min(20, timeoutMs));
    } while (Clock::now() < deadline);
    return 0;
}
}
