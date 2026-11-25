#include "discovery.hpp"
#include "yt/cast.hpp"
#include "picojson.h"
#include <curl/curl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#ifdef __XSDK__
#include <xsdk/net.h>
#include <xsdk/kernel.h>
#include <xsdk/thread.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#endif

namespace nest {
namespace {
using J = picojson::value;
using O = picojson::object;
const J& field(const J& j, const char* k) { static const J empty; return j.is<O>() ? j.get(k) : empty; }
std::string text(const J& j, const char* k) { const auto& v=field(j,k); return v.is<std::string>() ? v.get<std::string>() : ""; }
std::string clean(std::string s) {
    for (char& c:s) if (static_cast<unsigned char>(c)<32 || c==127) c=' ';
    if (s.size()>128) { size_t end=128; while (end && (static_cast<unsigned char>(s[end])&0xc0)==0x80) --end; s.resize(end); }
    auto end=s.find_last_not_of(' '); if (end==std::string::npos) return {};
    s.resize(end+1); return s.substr(s.find_first_not_of(' '));
}
std::string normalizedUuid(std::string s) {
    std::string out;
    for (unsigned char c:s) {
        if (c=='-' || c=='{' || c=='}') continue;
        if (!std::isxdigit(c)) return {};
        out+=char(std::tolower(c));
    }
    return out.size()==32 ? out : std::string{};
}
bool validHost(const std::string& s) {
    return !s.empty() && s.size()<=253 && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-")==std::string::npos;
}
bool validMask(uint32_t m) { uint32_t inverse=~m; return m && inverse>=3 && !(inverse&(inverse+1)); }
std::string ip(uint32_t host) {
    in_addr a{htonl(host)}; char out[INET_ADDRSTRLEN]{};
    return inet_ntop(AF_INET,&a,out,sizeof(out)) ? out : "";
}
bool networkMask(Network& net) {
    if (!net.address || net.address==0xffffffffu || (net.address>>24)>=224) return false;
    if (!validMask(net.mask)) {
        net.mask=0xffffff00u;
        SDL_Log("Nest: mascara indisponivel; usando /24 do IP atual %s",ip(net.address).c_str());
    }
    return true;
}
// Limite antes do parser recursivo, inclusive para um serviço HTTP que não seja Cast.
bool parse(const std::string& s,J& j) {
    int depth=0; bool quoted=false,escaped=false;
    if (s.size()>60000) return false;
    for (char c:s) {
        if (quoted) { if (escaped) escaped=false; else if (c=='\\') escaped=true; else if (c=='"') quoted=false; }
        else if (c=='"') quoted=true;
        else if ((c=='{' || c=='[') && ++depth>24) return false;
        else if ((c=='}' || c==']') && --depth<0) return false;
    }
    return !quoted && !depth && picojson::parse(j,s).empty() && j.is<O>();
}
size_t body(char* p,size_t size,size_t n,void* target) {
    auto& out=*static_cast<std::string*>(target); size_t bytes=size*n;
    if (bytes>60000-out.size()) return 0;
    out.append(p,bytes); return bytes;
}
int remaining(uint64_t deadline,int limit) {
    auto now=SDL_GetTicks();
    return deadline ? (now>=deadline ? 0 : int(std::min<uint64_t>(limit,deadline-now))) : limit;
}
bool eureka(Config& device,const std::atomic_bool& cancel,uint64_t deadline) {
    // Firmwares recentes também oferecem esta leitura em HTTPS na porta 8443.
    for (bool tls:{false,true}) {
        int budget=remaining(deadline,tls ? 5000 : 2500);
        if (cancel || !budget) return false;
        auto* easy=curl_easy_init(); if (!easy) return false;
        std::string bytes,url=std::string(tls ? "https://" : "http://")+device.host+(tls ? ":8443" : ":8008")+"/setup/eureka_info?params=device_info,name";
        curl_easy_setopt(easy,CURLOPT_URL,url.c_str());
        curl_easy_setopt(easy,CURLOPT_PROXY,"");
        curl_easy_setopt(easy,CURLOPT_NOSIGNAL,1L);
        curl_easy_setopt(easy,CURLOPT_CONNECTTIMEOUT_MS,long(std::min(budget,tls ? 4000 : 1500)));
        curl_easy_setopt(easy,CURLOPT_TIMEOUT_MS,long(budget));
        // Certificado próprio do mesmo receptor LAN já confirmado pelo protocolo Cast.
        if (tls) { curl_easy_setopt(easy,CURLOPT_SSL_VERIFYPEER,0L); curl_easy_setopt(easy,CURLOPT_SSL_VERIFYHOST,0L); }
        curl_easy_setopt(easy,CURLOPT_WRITEFUNCTION,body);
        curl_easy_setopt(easy,CURLOPT_WRITEDATA,&bytes);
        curl_easy_setopt(easy,CURLOPT_NOPROGRESS,0L);
        curl_easy_setopt(easy,CURLOPT_XFERINFODATA,&cancel);
        curl_easy_setopt(easy,CURLOPT_XFERINFOFUNCTION,+[](void* p,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
            return static_cast<const std::atomic_bool*>(p)->load() ? 1 : 0;
        });
        auto rc=curl_easy_perform(easy); long status=0; curl_easy_getinfo(easy,CURLINFO_RESPONSE_CODE,&status); curl_easy_cleanup(easy);
        J j; if (rc!=CURLE_OK || status!=200 || !parse(bytes,j)) continue;
        const auto& info=field(j,"device_info");
        auto name=clean(text(info,"name")); if (name.empty()) name=clean(text(j,"name"));
        auto model=clean(text(info,"model_name")); if (model.empty()) model=clean(text(j,"model_name"));
        auto id=normalizedUuid(text(info,"ssdp_udn")); if (id.empty()) id=normalizedUuid(text(j,"ssdp_udn"));
        if (!name.empty()) device.name=name;
        if (!model.empty()) device.model=model;
        if (!id.empty()) device.uuid=id;
        if (!device.uuid.empty() && !device.model.empty()) return true;
    }
    return !device.uuid.empty();
}
}
bool Config::load(const std::string& dir) {
    *this=Config{};
    FILE* f=std::fopen((dir+"nest.txt").c_str(),"r"); if (!f) return false;
    char buffer[512]{};
    if (std::fgets(buffer,sizeof(buffer),f)) {
        char host[254]{}; int p=8009;
        if (std::sscanf(buffer,"%253s %d",host,&p)>=1) { this->host=host; port=p; }
    }
    if (std::fgets(buffer,sizeof(buffer),f)) { name=clean(buffer); if (name.empty()) name="Nest"; }
    while (std::fgets(buffer,sizeof(buffer),f)) {
        std::string line=buffer;
        if (line.rfind("uuid=",0)==0) { line.erase(line.find_last_not_of("\r\n")+1); this->uuid=normalizedUuid(line.substr(5)); }
        else if (line.rfind("model=",0)==0) model=clean(line.substr(6));
    }
    std::fclose(f);
    if (!validHost(host) || port<1 || port>65535) host.clear();
    return !host.empty();
}
bool Config::save(const std::string& dir) const {
    if ((!host.empty() && !validHost(host)) || port<1 || port>65535) return false;
    auto tmp=dir+"nest.tmp"; FILE* f=std::fopen(tmp.c_str(),"w"); if (!f) return false;
    bool ok=std::fprintf(f,"%s%s\n%s\nuuid=%s\nmodel=%s\n",host.c_str(),host.empty() ? "" : (" "+std::to_string(port)).c_str(),clean(name).c_str(),normalizedUuid(this->uuid).c_str(),clean(model).c_str())>0;
    if (std::fclose(f)) ok=false;
    if (ok) ok=std::rename(tmp.c_str(),(dir+"nest.txt").c_str())==0;
    if (!ok) std::remove(tmp.c_str());
    return ok;
}
bool localNetwork(Network& net) {
#ifdef __XSDK__
    in_addr address{}; if (xsdk_net_address(&address)) return false;
    // XAM 0x65 lê a configuração; nunca chamamos XnpConfig nem SaveConfigParams.
    // Layout público de XNetConfigParams: IP em 0x54, máscara em 0x58 (492 bytes).
    HANDLE module=0; void* proc=nullptr;
    alignas(8) unsigned char config[492]{};
    if (!XexGetModuleHandle("xam.xex",&module) && XexGetProcedureAddress(module,0x65,&proc)>=0 && proc)
        reinterpret_cast<int(*)(int,void*,uint32_t,uint32_t)>(proc)(1,config,0,0);
    uint32_t saved=0,mask=0; std::memcpy(&saved,config+0x54,4); std::memcpy(&mask,config+0x58,4);
    net={ntohl(address.s_addr),ntohl(mask)};
    if (saved!=address.s_addr || !validMask(net.mask)) {
        SDL_Log("Nest: mascara local indisponivel (IP atual %s, IP salvo %s, mascara %s)",ip(net.address).c_str(),ip(ntohl(saved)).c_str(),ip(net.mask).c_str());
        net.mask=0;
    }
    return networkMask(net);
#else
    // O override de loopback só existe no PC, para usar o mesmo scanner nos testes.
    if (const char* value=std::getenv("XT_NEST_NETWORK")) {
        std::string s=value; auto slash=s.find('/'); in_addr a{},m{};
        if (slash==std::string::npos || inet_pton(AF_INET,s.substr(0,slash).c_str(),&a)!=1 || inet_pton(AF_INET,s.substr(slash+1).c_str(),&m)!=1) return false;
        net={ntohl(a.s_addr),ntohl(m.s_addr)};
        return (!net.mask || validMask(net.mask)) && networkMask(net);
    }
    ifaddrs* list=nullptr; if (getifaddrs(&list)) return false;
    bool found=false;
    for (auto* p=list;p;p=p->ifa_next) {
        if (!p->ifa_addr || !p->ifa_netmask || p->ifa_addr->sa_family!=AF_INET || !(p->ifa_flags&IFF_UP) || (p->ifa_flags&IFF_LOOPBACK)) continue;
        Network n{ntohl(reinterpret_cast<sockaddr_in*>(p->ifa_addr)->sin_addr.s_addr),ntohl(reinterpret_cast<sockaddr_in*>(p->ifa_netmask)->sin_addr.s_addr)};
        if (validMask(n.mask)) { net=n; found=true; break; }
    }
    freeifaddrs(list); return found;
#endif
}
std::vector<std::string> addresses(Network net) {
    std::vector<std::string> out; if (!net.address || !validMask(net.mask)) return out;
    uint32_t mask=net.mask|0xffffff00u, first=net.address&mask, last=first|~mask;
    for (uint32_t at=first+1;at<last;++at) if (at!=net.address) out.push_back(ip(at));
    return out;
}
bool identify(Config& device,const std::atomic_bool& cancel,uint64_t deadline) {
    int budget=remaining(deadline,4000);
    if (cancel || !budget) return false;
    cast::Connection channel;
    // TLS no Xenon disputa CPU com o vídeo; os 600/450 ms antigos só bastavam no PC.
    if (!channel.open(device.host,device.port,budget,&cancel)) return false;
    const std::string sender="sender-tube360-discovery";
    if (!channel.send(sender,"receiver-0",cast::connection,R"({"type":"CONNECT","origin":{}})") ||
        !channel.send(sender,"receiver-0",cast::receiver,R"({"type":"GET_STATUS","requestId":1})")) return false;
    uint64_t until=SDL_GetTicks()+remaining(deadline,2000);
    bool valid=false;
    while (!cancel && SDL_GetTicks()<until) {
        cast::Message message; int rc=channel.read(message,20); if (rc<0) break; if (!rc) continue;
        J j; if (!parse(message.json,j) || (message.destination!=sender && message.destination!="*")) continue;
        if (message.space==cast::heartbeat && text(j,"type")=="PING") channel.send(sender,message.source,cast::heartbeat,R"({"type":"PONG"})");
        if (message.source=="receiver-0" && message.space==cast::receiver && text(j,"type")=="RECEIVER_STATUS" && field(j,"status").is<O>()) {
            auto name=clean(text(field(j,"status"),"userFriendlyName"));
            if (!name.empty()) device.name=name;
            valid=true; break;
        }
    }
    channel.close();
    if (!valid || cancel) return false;
    // Um HTTP 200 isolado não prova que seja um receptor Cast.
    device.uuid.clear(); device.model.clear(); eureka(device,cancel,deadline);
    if (device.name.empty() || device.name=="Nest") device.name=device.host;
    if (device.model.empty()) device.model="Google Cast";
    return !cancel;
}
SearchResult scan(Network net,const std::atomic_bool& cancel,const Config& known) {
    SearchResult result; result.state=SearchState::Done;
    auto hosts=addresses(net); if (hosts.empty()) { result.state=SearchState::NetworkError; return result; }
    if (!known.host.empty()) {
        result.devices.push_back(known);
        hosts.erase(std::remove(hosts.begin(),hosts.end(),known.host),hosts.end());
    }
    uint64_t began=SDL_GetTicks();
    SDL_Log("Nest: busca unicast IP=%s mascara=%s hosts=%u limite=16",ip(net.address).c_str(),ip(net.mask).c_str(),(unsigned)hosts.size());
    struct Probe { int fd; std::string host; uint64_t until; };
    std::vector<Probe> pending; std::vector<std::string> ready;
    size_t next=0; uint64_t probeDeadline=began+10000, deadline=began+30000;
    while (!cancel && SDL_GetTicks()<probeDeadline && (next<hosts.size() || !pending.empty())) {
        while (pending.size()<16 && next<hosts.size()) {
            auto host=hosts[next++]; int fd=socket(AF_INET,SOCK_STREAM,0);
            if (fd<0 || fd>=FD_SETSIZE) { if (fd>=0) close(fd); result.state=SearchState::Incomplete; continue; }
            if (fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK)<0) { close(fd); result.state=SearchState::Incomplete; continue; }
            sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons(8009); inet_pton(AF_INET,host.c_str(),&address.sin_addr);
            int rc=connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address));
            if (!rc) { ready.push_back(host); close(fd); }
            else if (errno==EINPROGRESS || errno==EWOULDBLOCK || errno==EAGAIN) pending.push_back({fd,host,SDL_GetTicks()+500});
            else close(fd);
        }
        if (pending.empty()) continue;
        fd_set writes,errors; FD_ZERO(&writes); FD_ZERO(&errors); int highest=0;
        for (auto& p:pending) { FD_SET(p.fd,&writes); FD_SET(p.fd,&errors); highest=std::max(highest,p.fd); }
        timeval timeout{0,10000}; int rc=select(highest+1,nullptr,&writes,&errors,&timeout);
        if (rc<0 && errno!=EINTR) { result.state=SearchState::NetworkError; break; }
        auto now=SDL_GetTicks();
        for (size_t i=0;i<pending.size();) {
            auto& p=pending[i]; bool done=rc>0 && (FD_ISSET(p.fd,&writes) || FD_ISSET(p.fd,&errors));
            if (done) { int error=0; socklen_t len=sizeof(error); if (!getsockopt(p.fd,SOL_SOCKET,SO_ERROR,&error,&len) && !error) ready.push_back(p.host); }
            if (done || now>=p.until) { close(p.fd); pending.erase(pending.begin()+i); } else ++i;
        }
    }
    if (next<hosts.size() || !pending.empty()) result.state=SearchState::Incomplete;
    for (auto& p:pending) close(p.fd);
    for (auto& host:ready) {
        if (cancel || SDL_GetTicks()>=deadline) { result.state=SearchState::Incomplete; break; }
        Config device; device.host=host;
        if (identify(device,cancel,deadline)) {
            bool duplicate=!device.uuid.empty() && std::any_of(result.devices.begin(),result.devices.end(),[&](const Config& c) { return c.uuid==device.uuid; });
            if (!duplicate) result.devices.push_back(device);
        }
    }
    if (SDL_GetTicks()>=deadline) result.state=SearchState::Incomplete;
    if (cancel) result.state=SearchState::Cancelled;
    std::sort(result.devices.begin(),result.devices.end(),[](const Config& a,const Config& b) { return a.name==b.name ? a.host<b.host : a.name<b.name; });
    SDL_Log("Nest: busca terminou estado=%d encontrados=%u em %llu ms",int(result.state),(unsigned)result.devices.size(),(unsigned long long)(SDL_GetTicks()-began));
    return result;
}
int matching(const Config& saved,const std::vector<Config>& devices) {
    int match=-1;
    for (size_t i=0;i<devices.size();++i) {
        bool same=!saved.uuid.empty() ? saved.uuid==devices[i].uuid : saved.name!="Nest" && !saved.name.empty() && saved.name==devices[i].name;
        if (same) { if (match>=0) return -1; match=int(i); }
    }
    return match;
}
void Discovery::init(const std::string& directory) {
    dir=directory; config.load(dir); mutex=SDL_CreateMutex(); condition=SDL_CreateCondition();
    thread=yt::worker([](void* p) { static_cast<Discovery*>(p)->run(); return 0; },"nest-discovery",this);
    if (!thread) { result.state=SearchState::NetworkError; ++result.revision; }
}
void Discovery::stop() {
    if (!mutex) return;
    SDL_LockMutex(mutex); quit=true; cancelFlag=true; SDL_SignalCondition(condition); SDL_UnlockMutex(mutex);
    if (thread) SDL_WaitThread(thread,nullptr);
    SDL_DestroyCondition(condition); SDL_DestroyMutex(mutex); mutex=nullptr; thread=nullptr;
}
void Discovery::search() {
    SDL_LockMutex(mutex);
    if (thread) {
        if (result.state==SearchState::Searching) {
            if (!manual) { manual=requested=true; cancelFlag=true; }
        } else {
            cancelFlag=false; manual=requested=true; result.state=SearchState::Searching; result.devices.clear(); ++result.revision;
        }
        SDL_SignalCondition(condition);
    }
    SDL_UnlockMutex(mutex);
}
void Discovery::cancel() {
    if (!mutex) return;
    SDL_LockMutex(mutex); cancelFlag=true; requested=false;
    if (result.state==SearchState::Searching) { result.state=SearchState::Cancelled; ++result.revision; }
    SDL_UnlockMutex(mutex);
}
void Discovery::recover() {
    SDL_LockMutex(mutex);
    if (thread && !requested && result.state!=SearchState::Searching && SDL_GetTicks()>=retryAt && !config.host.empty()) {
        cancelFlag=false; manual=false; recovery=requested=true; verified=false; SDL_SignalCondition(condition);
    }
    SDL_UnlockMutex(mutex);
}
void Discovery::refresh() {
    SDL_LockMutex(mutex);
    if (thread && !verified && !requested && result.state!=SearchState::Searching && SDL_GetTicks()>=metadataAt && !config.host.empty()) {
        cancelFlag=false; manual=recovery=false; requested=true; SDL_SignalCondition(condition);
    }
    SDL_UnlockMutex(mutex);
}
bool Discovery::ready() { SDL_LockMutex(mutex); bool value=verified; SDL_UnlockMutex(mutex); return value; }
bool Discovery::configured() { SDL_LockMutex(mutex); bool value=!config.host.empty(); SDL_UnlockMutex(mutex); return value; }
Config Discovery::selected() { SDL_LockMutex(mutex); auto copy=config; SDL_UnlockMutex(mutex); return copy; }
bool Discovery::choose(const Config& value) {
    SDL_LockMutex(mutex);
    bool ok=value.save(dir);
    if (ok) { config=value; verified=!value.host.empty(); ++selection; retryAt=metadataAt=0; cancelFlag=true; requested=false; }
    SDL_UnlockMutex(mutex); return ok;
}
bool Discovery::poll(SearchResult& view) {
    SDL_LockMutex(mutex); bool changed=view.revision!=result.revision; if (changed) view=result; SDL_UnlockMutex(mutex); return changed;
}
void Discovery::run() {
#ifdef __XSDK__
    xsdk_thread_move(5);
#endif
    for (;;) {
        SDL_LockMutex(mutex);
        while (!quit && !requested) SDL_WaitCondition(condition,mutex);
        if (quit) { SDL_UnlockMutex(mutex); return; }
        bool user=manual, failed=recovery; auto saved=config; auto version=selection; requested=false;
        result.state=SearchState::Searching; ++result.revision; SDL_UnlockMutex(mutex);
        SearchResult found; Config live=saved;
        if (!saved.host.empty()) SDL_Log("Nest: %s IP salvo %s",user ? "busca manual; verificando" : failed ? "conexao falhou; verificando" : "metadados em segundo plano; verificando",saved.host.c_str());
        bool same=!saved.host.empty() && identify(live,cancelFlag) && (saved.uuid.empty() || live.uuid==saved.uuid);
        found.state=SearchState::NetworkError;
        if (same) { found.state=SearchState::Done; found.devices.push_back(live); }
        if (user || (failed && !same)) {
            Network net;
            if (localNetwork(net)) found=scan(net,cancelFlag,same ? live : Config{});
            else if (!same) found.state=SearchState::NetworkError;
        }
        SDL_LockMutex(mutex);
        int match=same ? 0 : matching(saved,found.devices);
        if (!user && !cancelFlag && version==selection && match>=0 && (same || found.state==SearchState::Done || !saved.uuid.empty())) {
            verified=true;
            if (!found.devices[match].save(dir)) found.state=SearchState::SaveError;
            config=found.devices[match];
            if (config.host!=saved.host) SDL_Log("Nest: reencontrado em %s por %s",config.host.c_str(),saved.uuid.empty() ? "nome unico" : "UUID");
        }
        if (cancelFlag) found.state=SearchState::Cancelled;
        found.revision=result.revision+1; result=std::move(found);
        metadataAt=SDL_GetTicks()+30000;
        if (user || failed) retryAt=SDL_GetTicks()+30000;
        if (requested) cancelFlag=false;
        SDL_UnlockMutex(mutex);
    }
}
}
