// Transporte exclusivo do executável de prova. Nenhum socket sai do loopback.
#include "cast/lounge.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>

namespace {
int connectMock(const yt::Request& request) {
    int fd = ::socket(AF_INET,SOCK_STREAM,0);
    if (fd < 0) return -1;
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(8234);
    ::inet_pton(AF_INET,"127.0.0.1",&address.sin_addr);
    timeval tv{0,100000};
    ::setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
    ::setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    if (::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))) { ::close(fd); return -1; }
    auto slash = request.url.find('/',8);
    std::string path = slash == std::string::npos ? "/" : request.url.substr(slash);
    std::string raw = std::string(request.body.empty() ? "GET " : "POST ") + path + " HTTP/1.0\r\nHost: fixture\r\nConnection: close\r\n";
    raw += "Content-Type: " + std::string(request.form ? "application/x-www-form-urlencoded" : "application/json") + "\r\n";
    for (const auto& h : request.headers) raw += h + "\r\n";
    raw += "Content-Length: " + std::to_string(request.body.size()) + "\r\n\r\n" + request.body;
    size_t offset = 0;
    while (offset < raw.size()) {
        auto n = ::send(fd,raw.data()+offset,raw.size()-offset,MSG_NOSIGNAL);
        if (n <= 0) { ::close(fd); return -1; }
        offset += size_t(n);
    }
    return fd;
}
class Mock final : public yt::Transport {
    const std::atomic_bool* cancel;
public:
    explicit Mock(const std::atomic_bool* c) : cancel(c) {}
    yt::Response request(const yt::string& url, const yt::string& body, bool form) override {
        yt::Request r; r.url=url; r.body=body; r.form=form; return send(r);
    }
    yt::Response send(const yt::Request& request) override {
        yt::Response response;
        int fd = connectMock(request);
        if (fd < 0) { response.error="mock_connect"; return response; }
        std::string raw; char bytes[16384];
        while (!cancel || !*cancel) {
            auto n = ::recv(fd,bytes,sizeof(bytes),0);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            if (n <= 0) break;
            raw.append(bytes,size_t(n));
        }
        ::close(fd);
        auto end = raw.find("\r\n\r\n");
        if (end == std::string::npos) { response.error="mock_headers"; return response; }
        response.status = std::atoi(raw.c_str()+raw.find(' ')+1);
        auto at = raw.find("Content-Range:");
        if (at != std::string::npos) response.contentRange = raw.substr(at+14,raw.find("\r\n",at)-at-14);
        response.body = raw.substr(end+4);
        return response;
    }
};
}
namespace yt {
#ifdef TUBE360_CAST_TEST
extern "C" std::unique_ptr<Transport> __wrap__ZN2yt5httpsEPKSt6atomicIbE(const std::atomic_bool* cancel) { return std::make_unique<Mock>(cancel); }
#else
std::unique_ptr<Transport> https(const std::atomic_bool* cancel) { return std::make_unique<Mock>(cancel); }
#endif
}
namespace cast {
long channel(const string& url, const std::atomic_bool& cancel, const std::function<bool(const string&)>& deliver) {
    yt::Request request; request.url = url;
    int fd = connectMock(request);
    if (fd < 0) return 0;
    string raw; char bytes[4096]; long status = 0; bool headerDone = false;
    Frames frames;
    while (!cancel) {
        auto n = ::recv(fd,bytes,sizeof(bytes),0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) break;
        if (!headerDone) {
            raw.append(bytes,size_t(n)); auto end = raw.find("\r\n\r\n");
            if (end == string::npos) continue;
            status = std::atoi(raw.c_str()+raw.find(' ')+1); headerDone = true;
            auto body = raw.substr(end+4);
            if (status != 200 || !frames.feed(body.data(),body.size(),deliver)) break;
        } else if (!frames.feed(bytes,size_t(n),deliver)) break;
    }
    ::close(fd); return status;
}
}
