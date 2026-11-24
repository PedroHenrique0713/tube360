#include "receiver.hpp"
#include "yt/thread.hpp"
#include <SDL3/SDL.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#ifdef __XSDK__
#include <xsdk/net.h>
#include <xsdk/thread.h>
#endif

namespace cast {
namespace {
const string service = "urn:dial-multiscreen-org:service:dial:1";
const string deviceType = "urn:dial-multiscreen-org:device:dial:1";
string xml(const string& text) {
    string out;
    for (char c : text) {
        if (c == '&') out += "&amp;"; else if (c == '<') out += "&lt;"; else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;"; else out += c;
    }
    return out;
}
// O XSDK repassa as opções ao XAM, mas omite as declarações de multicast do Winsock.
struct Membership { in_addr group, interfaceAddress; };
static_assert(sizeof(Membership) == 8);
#ifdef __XSDK__
constexpr int addMembership = 12;
constexpr int multicastInterface = 9, multicastTtl = 10;
#else
constexpr int addMembership = IP_ADD_MEMBERSHIP;
constexpr int multicastInterface = IP_MULTICAST_IF, multicastTtl = IP_MULTICAST_TTL;
#endif
void affinity() {
#ifdef __XSDK__
    xsdk_thread_move(5);
#endif
}
struct Lock {
    SDL_Mutex* mutex;
    explicit Lock(SDL_Mutex* m) : mutex(m) { SDL_LockMutex(m); }
    ~Lock() { SDL_UnlockMutex(mutex); }
};
string load(const string& file) {
    char buf[256]{};
    FILE* f = std::fopen(file.c_str(),"r");
    if (!f) return {};
    std::fgets(buf,sizeof(buf),f); std::fclose(f);
    return clean(buf,128);
}
bool identifier(const string& s) {
    return !s.empty() && s.size() <= 128 && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") == string::npos;
}
void save(const string& file, const string& value) {
    string tmp = file + ".tmp";
    if (FILE* f = std::fopen(tmp.c_str(),"w")) {
#ifndef __XSDK__
        ::chmod(tmp.c_str(),0600);
#endif
        bool ok = std::fprintf(f,"%s\n",value.c_str()) > 0;
        if (std::fclose(f)) ok = false;
        if (ok) ok = std::rename(tmp.c_str(),file.c_str()) == 0;
        if (!ok) std::remove(tmp.c_str());
    }
}
string uuid() {
    unsigned char bytes[16];
    for (auto& b : bytes) b = static_cast<unsigned char>(SDL_rand_bits());
    bytes[6] = (bytes[6] & 15) | 64; bytes[8] = (bytes[8] & 63) | 128;
    string s;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) s += '-';
        char hex[3]; std::snprintf(hex,sizeof(hex),"%02x",bytes[i]); s += hex;
    }
    return s;
}
string lower(string s) { for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }
string trim(string s) {
    auto first = s.find_first_not_of(" \r\n\t");
    if (first == string::npos) return {};
    return s.substr(first,s.find_last_not_of(" \r\n\t")-first+1);
}
Fields headers(const string& raw) {
    Fields out;
    size_t start = raw.find("\r\n");
    while (start != string::npos) {
        start += 2; size_t end = raw.find("\r\n",start);
        if (end == string::npos || end == start) break;
        string line = raw.substr(start,end-start); auto colon = line.find(':');
        if (colon != string::npos) {
            auto name = lower(line.substr(0,colon));
            if (out.count(name)) return {{"invalid","duplicate"}};
            out[name] = trim(line.substr(colon+1));
        }
        start = end;
    }
    return out;
}
bool decode(const string& s, string& out) {
    auto hex = [](char c) { if (c >= '0' && c <= '9') return c-'0'; if (c >= 'a' && c <= 'f') return c-'a'+10; if (c >= 'A' && c <= 'F') return c-'A'+10; return -1; };
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '%') {
            if (i+2 >= s.size()) return false;
            int a = hex(s[i+1]), b = hex(s[i+2]);
            if (a < 0 || b < 0) return false;
            c = char(a*16+b); i += 2;
        } else if (c == '+') c = ' ';
        if (static_cast<unsigned char>(c) < 32 || c == 127) return false;
        out += c;
    }
    return true;
}
Fields fields(const string& raw) {
    Fields out; size_t start = 0;
    while (start < raw.size()) {
        auto end = raw.find('&',start); string part = raw.substr(start,end-start); auto eq = part.find('=');
        if (eq == string::npos) return {};
        string k, v;
        if (!decode(part.substr(0,eq),k) || !decode(part.substr(eq+1),v) || out.count(k)) return {};
        out[k] = v;
        if (end == string::npos) break;
        start = end+1;
    }
    return out;
}
bool sendAll(int fd, const string& s) {
    size_t offset = 0;
    while (offset < s.size()) {
#ifdef MSG_NOSIGNAL
        int flags = MSG_NOSIGNAL;
#else
        int flags = 0;
#endif
        auto n = ::send(fd,s.data()+offset,s.size()-offset,flags);
        if (n <= 0) return false;
        offset += size_t(n);
    }
    return true;
}
void timeout(int fd, int ms) {
    timeval tv{ms/1000,(ms%1000)*1000};
    ::setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
    ::setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
}
string localAddress(const sockaddr_in& remote) {
#ifdef __XSDK__
    // XNet pode recusar connect UDP para multicast; a interface não depende dessa rota.
    in_addr address{}; char ip[INET_ADDRSTRLEN]{};
    return !xsdk_net_address(&address) && ::inet_ntop(AF_INET,&address,ip,sizeof(ip)) ? string(ip) : string{};
#else
    int fd = ::socket(AF_INET,SOCK_DGRAM,0);
    if (fd < 0) return {};
    sockaddr_in local{}; socklen_t size = sizeof(local);
    bool ok = ::connect(fd,reinterpret_cast<const sockaddr*>(&remote),sizeof(remote)) == 0 &&
        ::getsockname(fd,reinterpret_cast<sockaddr*>(&local),&size) == 0;
    ::close(fd);
    char ip[INET_ADDRSTRLEN]{};
    return ok && ::inet_ntop(AF_INET,&local.sin_addr,ip,sizeof(ip)) ? string(ip) : string{};
#endif
}
}
struct Receiver::Impl {
    SDL_Mutex* mutex = SDL_CreateMutex();
    SDL_Thread *controlThread = nullptr, *rpcThread = nullptr, *lanThread = nullptr;
    std::atomic_bool quit{false}, enabled{false}, cancel{false}, rpcCancel{true};
    string directory, identity, rpcUrl, name;
    Status status;
    Playback playback;
    string endedId; uint64_t endedSerial = 0;
    std::deque<string> frames;
    std::deque<Command> commands;
    bool rpcActive = false;
    long channelStatus = 0;
    string pairing, pairingTheme;
    unsigned pairingSeq = 0, pairingDone = 0;
    bool pairingOk = false;
    int dialPort = 8134, ssdpPort = 1900;
    bool loopback = false;
    bool testMulticast = false, testNoMembership = false;

    explicit Impl(string assets, string screenName) : directory(std::move(assets)), name(std::move(screenName)) {
        identity = load(directory + "cast-device.txt");
        if (!identifier(identity)) { identity = uuid(); save(directory + "cast-device.txt",identity); }
#ifdef TUBE360_CAST_TEST
        ssdpPort = 1934; loopback = true;
        testMulticast = std::getenv("XT_CAST_TEST_MULTICAST") != nullptr;
        testNoMembership = std::getenv("XT_CAST_TEST_NO_MEMBERSHIP") != nullptr;
#endif
        controlThread = yt::worker([](void* p) { static_cast<Impl*>(p)->control(); return 0; },"cast-post",this);
        rpcThread = yt::worker([](void* p) { static_cast<Impl*>(p)->rpc(); return 0; },"cast-rpc",this);
        lanThread = yt::worker([](void* p) { static_cast<Impl*>(p)->lan(); return 0; },"cast-dial",this);
    }
    ~Impl() { stop(); SDL_DestroyMutex(mutex); }
    void stop() {
        { Lock lock(mutex); quit = true; cancel = true; rpcCancel = true; }
        if (lanThread) SDL_WaitThread(lanThread,nullptr);
        if (rpcThread) SDL_WaitThread(rpcThread,nullptr);
        if (controlThread) SDL_WaitThread(controlThread,nullptr);
        lanThread = rpcThread = controlThread = nullptr;
    }
    void update(const Lounge& lounge, bool online) {
        Lock lock(mutex);
        if (status.online != online || status.code != lounge.code || status.who != lounge.who) {
            status.online = online; status.code = online ? lounge.code : ""; status.who = online ? lounge.who : "";
            ++status.revision;
        }
    }
    void closeRpc() {
        { Lock lock(mutex); rpcUrl.clear(); rpcCancel = true; }
        for (;;) {
            bool active;
            { Lock lock(mutex); active = rpcActive; }
            if (!active) break;
            SDL_Delay(10);
        }
        Lock lock(mutex); frames.clear(); channelStatus = 0;
    }
    void control() {
        affinity();
        auto transport = yt::https(&cancel);
        string savedScreen = load(directory + "cast-screen.txt");
        if (!identifier(savedScreen)) savedScreen.clear();
        Lounge lounge(*transport,identity,savedScreen,name);
        yt::Client youtube(*transport);
        uint64_t retryAt = 0, codeAt = 0, stateAt = 0, nextAt = 0, notifyAt = 0;
        unsigned failures = 0;
        string selectedTheme = "cl", offeredFor;
        bool online = false;
        while (!quit) {
            if (!enabled) {
                if (online) closeRpc();
                online = false; lounge.reset(true); update(lounge,false);
                { Lock lock(mutex); commands.clear(); endedId.clear(); }
                retryAt = 0; SDL_Delay(50); continue;
            }
            { Lock lock(mutex); if (quit || !enabled) continue; cancel = false; }
            uint64_t now = SDL_GetTicks();
            std::deque<string> incoming;
            Playback p; string ended, pairCode, theme;
            unsigned pairSeq; long streamStatus; uint64_t endSerial;
            {
                Lock lock(mutex);
                incoming.swap(frames); p = playback; ended.swap(endedId);
                endSerial = endedSerial; pairCode = pairing; theme = pairingTheme; pairSeq = pairingSeq; streamStatus = channelStatus;
            }
            if (!theme.empty() && theme != selectedTheme) { selectedTheme = theme; online = false; retryAt = 0; }
            bool sessionFailed = streamStatus == 400 || streamStatus == 401 || streamStatus == 403 || streamStatus == 410;
            for (const auto& frame : incoming) if (!lounge.receive(frame)) sessionFailed = true;
            if (online && (lounge.expired(now) || sessionFailed)) { online = false; retryAt = now + 1000; }
            if (!online && now >= retryAt) {
                closeRpc();
                if (lounge.begin(now,selectedTheme)) {
                    online = true; failures = 0; codeAt = 0;
                    save(directory + "cast-screen.txt",lounge.screenId());
                    { Lock lock(mutex); rpcUrl = lounge.streamUrl(); rpcCancel = false; }
                    SDL_Log("cast: lounge conectado, DIAL TCP %d SSDP UDP %d",dialPort,ssdpPort);
                } else {
                    ++failures; retryAt = now + std::min(300000ULL,5000ULL << std::min(failures,6u));
                    SDL_Log("cast: lounge indisponivel, nova tentativa em %llu ms",static_cast<unsigned long long>(retryAt-now));
                }
            }
            if (online) {
                if (now >= codeAt) { bool ok = lounge.pairingCode(); codeAt = now + (ok ? 300000 : 30000); }
                if (pairSeq != pairingDone && !pairCode.empty()) {
                    bool ok = lounge.pair(pairCode);
                    Lock lock(mutex); pairingDone = pairSeq; pairingOk = ok;
                }
                if (!ended.empty()) lounge.ended(ended,endSerial);
                if (now >= stateAt) { lounge.observe(p); stateAt = now + 250; }
                if (now >= notifyAt && !lounge.who.empty()) { lounge.notify(); notifyAt = now + 15000; }
                if (lounge.needsNext() && offeredFor != lounge.currentId() && now >= nextAt) {
                    offeredFor = lounge.currentId(); nextAt = now + 10000;
                    string next = lounge.nextInPlaylist();
                    if (next.empty() && lounge.allowsAutoplay()) {
                        auto page = youtube.next(offeredFor);
                        if (!page.cards.empty()) next = page.cards.front().id;
                    }
                    lounge.offerNext(next);
                }
                if (!lounge.flush()) { online = false; retryAt = now + 1000; }
                else {
                    Lock lock(mutex);
                    rpcUrl = lounge.streamUrl();
                    Command c;
                    while (lounge.pop(c)) {
                        if (commands.size() >= 256) commands.pop_front();
                        commands.push_back(std::move(c));
                    }
                }
            }
            update(lounge,online);
            if (!online) { closeRpc(); lounge.reset(); }
            SDL_Delay(30);
        }
        closeRpc();
    }
    void rpc() {
        affinity();
        unsigned failures = 0;
        while (!quit) {
            string url;
            { Lock lock(mutex); if (!rpcCancel) { url = rpcUrl; rpcActive = !url.empty(); } }
            if (url.empty()) { SDL_Delay(30); continue; }
            uint64_t start = SDL_GetTicks();
            long result = channel(url,rpcCancel,[&](const string& frame) {
                Lock lock(mutex);
                if (rpcCancel || frames.size() >= 16) return false;
                frames.push_back(frame); return true;
            });
            { Lock lock(mutex); rpcActive = false; channelStatus = result; }
            if (SDL_GetTicks() - start > 10000) failures = 0;
            else failures = std::min(failures+1,6u);
            uint64_t until = SDL_GetTicks() + std::min(30000u,250u << failures);
            while (!quit && !rpcCancel && SDL_GetTicks() < until) SDL_Delay(30);
        }
    }
    string endpoint(const string& ip) const { return "http://" + ip + ":" + std::to_string(dialPort); }
    bool registerPair(const Fields& launch) {
        auto code = launch.find("pairingCode");
        if (code == launch.end() || !identifier(code->second)) return false;
        string theme = "cl";
        auto t = launch.find("theme");
        if (t != launch.end()) { if (t->second != "cl" && t->second != "m") return false; theme = t->second; }
        unsigned seq;
        { Lock lock(mutex); pairing = code->second; pairingTheme = theme; seq = ++pairingSeq; }
        uint64_t until = SDL_GetTicks() + 12000;
        while (!quit && enabled && SDL_GetTicks() < until) {
            { Lock lock(mutex); if (pairingDone == seq) return pairingOk; }
            SDL_Delay(20);
        }
        return false;
    }
    void http(int fd, const string& ip) {
        timeout(fd,1000);
        string raw; char bytes[2048]; size_t end;
        uint64_t deadline = SDL_GetTicks() + 2000;
        while ((end = raw.find("\r\n\r\n")) == string::npos) {
            auto n = ::recv(fd,bytes,sizeof(bytes),0);
            if (n <= 0 || raw.size() + size_t(n) > 8192 || SDL_GetTicks() >= deadline) return;
            raw.append(bytes,size_t(n));
        }
        auto h = headers(raw.substr(0,end+4));
        auto space = raw.find(' '), last = raw.find(' ',space+1);
        if (space == string::npos || last == string::npos) return;
        string method = raw.substr(0,space), path = raw.substr(space+1,last-space-1);
        auto reply = [&](int code, const string& reason, const string& body = "", const string& extra = "") {
            sendAll(fd,"HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Type: application/xml; charset=utf-8\r\n" +
                extra + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
        };
        if (h.count("invalid") || h.count("transfer-encoding")) { reply(400,"Bad Request"); return; }
        bool description = path == "/ssdp/device-desc.xml";
        bool app = path == "/apps/YouTube" || path == "/apps/YouTube/";
        if (method == "GET" && description) {
            string body = "<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion><device><deviceType>" + deviceType +
                "</deviceType><friendlyName>" + xml(name) + "</friendlyName><manufacturer>Microsoft</manufacturer><modelName>Xbox 360</modelName><UDN>uuid:" + identity +
                "</UDN><serviceList><service><serviceType>" + service + "</serviceType><serviceId>urn:dial-multiscreen-org:serviceId:dial</serviceId><controlURL>/ssdp/notfound</controlURL><eventSubURL>/ssdp/notfound</eventSubURL><SCPDURL>/ssdp/notfound</SCPDURL></service></serviceList></device></root>";
            reply(200,"OK",body,"Application-URL: " + endpoint(ip) + "/apps/\r\n");
        } else if (method == "GET" && (app || path == "/apps/YouTube/run")) {
            bool online; { Lock lock(mutex); online = status.online; }
            reply(200,"OK",string("<?xml version=\"1.0\"?><service xmlns=\"urn:dial-multiscreen-org:schemas:dial\" dialVer=\"1.7\"><name>YouTube</name><options allowStop=\"false\"/><state>") +
                (online ? "running" : "stopped") + "</state><link rel=\"run\" href=\"run\"/></service>");
        } else if (method == "POST" && app) {
            char* tail = nullptr; string len = h["content-length"];
            unsigned long length = std::strtoul(len.c_str(),&tail,10);
            if (len.empty() || !tail || *tail || length > 4096) { reply(400,"Bad Request"); return; }
            string body = raw.substr(end+4);
            deadline = SDL_GetTicks() + 2000;
            while (body.size() < length) {
                auto n = ::recv(fd,bytes,std::min(sizeof(bytes),size_t(length-body.size())),0);
                if (n <= 0 || SDL_GetTicks() >= deadline) return;
                body.append(bytes,size_t(n));
            }
            if (body.size() != length || !fields(body).count("pairingCode")) { reply(400,"Bad Request"); return; }
            bool ok = registerPair(fields(body));
            if (ok) reply(201,"Created","","Location: " + endpoint(ip) + "/apps/YouTube/run\r\n");
            else reply(503,"Service Unavailable");
        } else reply(method == "GET" || method == "POST" ? 404 : 405,"Unavailable");
    }
    string ssdp(const string& ip, const string& type, bool notify, bool alive = true) const {
        string common = "USN: uuid:" + identity + (type == "uuid:" + identity ? "" : "::" + type) + "\r\n";
        if (notify) return "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nNT: " + type + "\r\nNTS: ssdp:" + (alive ? "alive" : "byebye") + "\r\n" + common +
            (alive ? "CACHE-CONTROL: max-age=300\r\nLOCATION: " + endpoint(ip) + "/ssdp/device-desc.xml\r\nSERVER: Xbox/360 UPnP/1.0 Tube360/1.0\r\n" : "") + "\r\n";
        return "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=300\r\nEXT:\r\nLOCATION: " + endpoint(ip) + "/ssdp/device-desc.xml\r\nSERVER: Xbox/360 UPnP/1.0 Tube360/1.0\r\nST: " + type + "\r\n" + common + "\r\n";
    }
    void lan() {
        affinity();
        while (!quit) {
            if (!enabled) { SDL_Delay(50); continue; }
            int tcp = ::socket(AF_INET,SOCK_STREAM,0), udp = ::socket(AF_INET,SOCK_DGRAM,0);
            sockaddr_in address{}; address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(loopback && !testMulticast ? INADDR_LOOPBACK : INADDR_ANY);
            int yes = 1;
            if (tcp >= 0) ::setsockopt(tcp,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
            if (udp >= 0) ::setsockopt(udp,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
            address.sin_port = htons(uint16_t(dialPort));
            bool ok = tcp >= 0 && udp >= 0 && ::bind(tcp,reinterpret_cast<sockaddr*>(&address),sizeof(address)) == 0 && ::listen(tcp,4) == 0;
            address.sin_port = htons(uint16_t(ssdpPort));
            ok = ok && ::bind(udp,reinterpret_cast<sockaddr*>(&address),sizeof(address)) == 0;
            sockaddr_in group{}; group.sin_family = AF_INET; group.sin_port = htons(uint16_t(ssdpPort));
            ::inet_pton(AF_INET,"239.255.255.250",&group.sin_addr);
            string advertiseIp = loopback ? "127.0.0.1" : localAddress(group);
            bool joined = false;
            if (ok && (!loopback || testMulticast)) {
                in_addr interfaceAddress{};
                ::inet_pton(AF_INET,advertiseIp.c_str(),&interfaceAddress);
                int ttl = 2;
                int outgoing = ::setsockopt(udp,IPPROTO_IP,multicastInterface,&interfaceAddress,sizeof(interfaceAddress));
                int outgoingError = outgoing ? errno : 0;
                ::setsockopt(udp,IPPROTO_IP,multicastTtl,&ttl,sizeof(ttl));
                if (::setsockopt(udp,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes)))
                    SDL_Log("cast: SO_BROADCAST errno=%d",errno);
                Membership membership{group.sin_addr,interfaceAddress};
                int error = ENOPROTOOPT;
                if (!testNoMembership) {
                    joined = ::setsockopt(udp,IPPROTO_IP,addMembership,&membership,sizeof(membership)) == 0;
                    error = joined ? 0 : errno;
                    if (!joined && interfaceAddress.s_addr) {
                        membership.interfaceAddress.s_addr = htonl(INADDR_ANY);
                        joined = ::setsockopt(udp,IPPROTO_IP,addMembership,&membership,sizeof(membership)) == 0;
                        error = joined ? 0 : errno;
                    }
                }
                SDL_Log("cast: SSDP grupo=%s IP=%s membership=%d errno=%d%s saida_errno=%d UDP=%d",
                    "239.255.255.250",advertiseIp.c_str(),joined,error,error == ENOPROTOOPT ? " (ENOPROTOOPT)" : "",outgoingError,ssdpPort);
                if (!joined) SDL_Log("cast: SSDP sem membership; mantendo recepcao UDP e NOTIFY; DIAL e codigo de TV ativos");
            }
            if (!ok) {
                if (tcp >= 0) ::close(tcp);
                if (udp >= 0) ::close(udp);
                SDL_Log("cast: falha ao abrir DIAL/SSDP");
                for (int i = 0; i < 100 && !quit && enabled; ++i) SDL_Delay(50);
                continue;
            }
            { Lock lock(mutex); status.discovery = true; ++status.revision; }
            uint64_t announceAt = 0;
            std::vector<string> types{service,deviceType,"upnp:rootdevice","uuid:"+identity};
            auto announce = [&](bool alive) {
                if ((loopback && !testMulticast) || advertiseIp.empty()) return;
                unsigned sent = 0; int error = 0;
                for (const auto& type : types) {
                    string packet = ssdp(advertiseIp,type,true,alive);
                    if (::sendto(udp,packet.data(),packet.size(),0,reinterpret_cast<sockaddr*>(&group),sizeof(group)) == ssize_t(packet.size())) ++sent;
                    else error = errno;
                }
                SDL_Log("cast: SSDP NOTIFY %s enviados=%u/%u errno=%d",alive ? "alive" : "byebye",sent,unsigned(types.size()),error);
            };
            unsigned announcements = 0;
            while (!quit && enabled) {
                if (SDL_GetTicks() >= announceAt) {
                    if (advertiseIp.empty()) {
                        advertiseIp = localAddress(group);
                        in_addr address{};
                        if (::inet_pton(AF_INET,advertiseIp.c_str(),&address) == 1) {
                            ::setsockopt(udp,IPPROTO_IP,multicastInterface,&address,sizeof(address));
                            Membership membership{group.sin_addr,address};
                            if (!joined && !testNoMembership)
                                joined = ::setsockopt(udp,IPPROTO_IP,addMembership,&membership,sizeof(membership)) == 0;
                            SDL_Log("cast: SSDP IP pronto %s membership=%d",advertiseIp.c_str(),joined);
                        }
                    }
                    announce(true); ++announcements;
                    announceAt = SDL_GetTicks() + (announcements < 3 ? 1000 : joined ? 120000 : 30000);
                }
                fd_set read; FD_ZERO(&read); FD_SET(tcp,&read); FD_SET(udp,&read);
                timeval wait{0,100000};
                if (::select(std::max(tcp,udp)+1,&read,nullptr,nullptr,&wait) <= 0) continue;
                if (FD_ISSET(udp,&read)) {
                    char bytes[4096]; sockaddr_in sender{}; socklen_t size = sizeof(sender);
                    auto n = ::recvfrom(udp,bytes,sizeof(bytes),0,reinterpret_cast<sockaddr*>(&sender),&size);
                    if (n > 0) {
                        string raw(bytes,size_t(n)); auto h = headers(raw); auto type = h["st"];
                        if (raw.rfind("M-SEARCH * HTTP/1.1\r\n",0) == 0 && h["man"] == "\"ssdp:discover\"") {
                            string ip = loopback ? "127.0.0.1" : localAddress(sender);
                            unsigned replies = 0; int error = 0;
                            if (!ip.empty()) for (const auto& supported : types) if (type == supported || type == "ssdp:all") {
                                string packet = ssdp(ip,supported,false);
                                if (::sendto(udp,packet.data(),packet.size(),0,reinterpret_cast<sockaddr*>(&sender),size) == ssize_t(packet.size())) ++replies;
                                else error = errno;
                            }
                            SDL_Log("cast: SSDP M-SEARCH ST=%s respostas=%u errno=%d",clean(type,128).c_str(),replies,error);
                        }
                    }
                }
                if (FD_ISSET(tcp,&read)) {
                    sockaddr_in sender{}; socklen_t size = sizeof(sender);
                    int fd = ::accept(tcp,reinterpret_cast<sockaddr*>(&sender),&size);
                    if (fd >= 0) { http(fd,loopback ? "127.0.0.1" : localAddress(sender)); ::close(fd); }
                }
            }
            announce(false); ::close(tcp); ::close(udp);
            { Lock lock(mutex); status.discovery = false; ++status.revision; }
        }
    }
};
Receiver::Receiver(const string& assets, const string& name) : impl(std::make_unique<Impl>(assets, name)) {}
Receiver::~Receiver() = default;
void Receiver::enable(bool on) {
#ifndef __XSDK__
    if (std::getenv("XT_CAST_DISABLE")) on = false;
#endif
    Lock lock(impl->mutex);
    impl->enabled = on;
    if (!on) { impl->cancel = true; impl->rpcCancel = true; }
}
void Receiver::publish(const Playback& p, const string& ended) {
    Lock lock(impl->mutex); impl->playback = p;
    if (!ended.empty()) { impl->endedId = ended; impl->endedSerial = p.serial; }
}
void Receiver::drain(std::deque<Command>& commands, Status& status) {
    Lock lock(impl->mutex);
    commands.insert(commands.end(),std::make_move_iterator(impl->commands.begin()),std::make_move_iterator(impl->commands.end()));
    impl->commands.clear();
    if (status.revision != impl->status.revision) status = impl->status;
}
void Receiver::stop() { impl->stop(); }
}
