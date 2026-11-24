// Receiver de produção; somente syscalls de rede recebem falhas/endereço de loopback.
#include "cast/receiver.hpp"
#include <SDL3/SDL.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

extern "C" int __real_getsockname(int, sockaddr*, socklen_t*);
extern "C" int __real_bind(int, const sockaddr*, socklen_t);
extern "C" int __real_setsockopt(int, int, int, const void*, socklen_t);
extern "C" ssize_t __real_sendto(int, const void*, size_t, int, const sockaddr*, socklen_t);

extern "C" int __wrap_getsockname(int fd, sockaddr* address, socklen_t* size) {
    int rc = __real_getsockname(fd,address,size);
    if (!rc && address->sa_family == AF_INET) {
        if (const char* path = std::getenv("XT_LAN_ADDRESS_FILE")) {
            std::ifstream file(path); std::string ip; file >> ip;
            inet_pton(AF_INET,ip.c_str(),&reinterpret_cast<sockaddr_in*>(address)->sin_addr);
        }
    }
    return rc;
}
extern "C" int __wrap_bind(int fd, const sockaddr* address, socklen_t size) {
    if (address->sa_family == AF_INET) {
        auto local = *reinterpret_cast<const sockaddr_in*>(address);
        if (ntohs(local.sin_port) == 1900) {
            local.sin_port = htons(1934);
            return __real_bind(fd,reinterpret_cast<sockaddr*>(&local),size);
        }
    }
    return __real_bind(fd,address,size);
}
extern "C" int __wrap_setsockopt(int fd, int level, int option, const void* value, socklen_t size) {
    bool multicast = level == IPPROTO_IP &&
        (option == IP_MULTICAST_IF || option == IP_MULTICAST_TTL || option == IP_ADD_MEMBERSHIP);
    bool broadcast = level == SOL_SOCKET && option == SO_BROADCAST;
    static std::atomic_int failures{[] {
        const char* count = std::getenv("XT_LAN_MEMBERSHIP_FAILURES"); return count ? std::atoi(count) : 0;
    }()};
    bool transient = level == IPPROTO_IP && option == IP_ADD_MEMBERSHIP && failures.fetch_sub(1) > 0;
    if (((multicast || broadcast) && std::getenv("XT_LAN_FAIL_OPTIONS")) || transient) {
        errno = ENOPROTOOPT; return -1;
    }
    return __real_setsockopt(fd,level,option,value,size);
}
extern "C" ssize_t __wrap_sendto(int fd, const void* data, size_t size, int flags,
                                  const sockaddr* address, socklen_t length) {
    if (address->sa_family == AF_INET) {
        auto target = *reinterpret_cast<const sockaddr_in*>(address);
        if (ntohl(target.sin_addr.s_addr) == 0xeffffffau) {
            // Mesmo com IP_MULTICAST_IF falhando, nenhum anúncio escapa para a LAN real.
            in_addr loopback{htonl(INADDR_LOOPBACK)};
            __real_setsockopt(fd,IPPROTO_IP,IP_MULTICAST_IF,&loopback,sizeof(loopback));
            target.sin_port = htons(1934);
            return __real_sendto(fd,data,size,flags,reinterpret_cast<sockaddr*>(&target),length);
        }
    }
    return __real_sendto(fd,data,size,flags,address,length);
}
int main(int argc, char** argv) {
    if (argc != 2 || !SDL_Init(0)) return 1;
    {
        cast::Receiver receiver(std::string(argv[1])+"/"); receiver.enable(true);
        std::string command;
        while (std::getline(std::cin,command) && command != "quit") {
            receiver.enable(command != "off");
        }
        receiver.stop();
    }
    SDL_Quit();
}
