// Sondagens TCP que ficam em EINPROGRESS até o timeout, sem tocar a rede real.
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <atomic>
#include <cerrno>
#include <cstdio>

namespace {
std::atomic_bool silent[FD_SETSIZE]{};
std::atomic_int active{0}, maximum{0};
struct Report {
    ~Report() { std::fprintf(stderr,"scan-fixture: maximum=%d remaining=%d\n",maximum.load(),active.load()); }
} report;
}
extern "C" int __real_connect(int, const sockaddr*, socklen_t);
extern "C" int __real_select(int, fd_set*, fd_set*, fd_set*, timeval*);
extern "C" int __real_close(int);

extern "C" int __wrap_connect(int fd, const sockaddr* address, socklen_t size) {
    if (address->sa_family == AF_INET && fd < FD_SETSIZE) {
        const auto& target = *reinterpret_cast<const sockaddr_in*>(address);
        auto host = ntohl(target.sin_addr.s_addr);
        if (ntohs(target.sin_port) == 8009 && (host & 0xffffff00u) == 0x7f4d1900u && (host & 255) != 254) {
            if (!silent[fd].exchange(true)) {
                int count = ++active, before = maximum;
                while (count > before && !maximum.compare_exchange_weak(before,count)) {}
            }
            errno = EINPROGRESS; return -1;
        }
    }
    return __real_connect(fd,address,size);
}
extern "C" int __wrap_select(int count, fd_set* reads, fd_set* writes, fd_set* errors, timeval* timeout) {
    for (int fd=0; fd<count && fd<FD_SETSIZE; ++fd) if (silent[fd]) {
        if (writes) FD_CLR(fd,writes);
        if (errors) FD_CLR(fd,errors);
    }
    return __real_select(count,reads,writes,errors,timeout);
}
extern "C" int __wrap_close(int fd) {
    if (fd >= 0 && fd < FD_SETSIZE && silent[fd].exchange(false)) --active;
    return __real_close(fd);
}
