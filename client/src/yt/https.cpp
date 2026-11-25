#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "client.hpp"
#include "boot_log.h"
#include <ctime>
#include "roots.hpp"
#include "update-roots.hpp"
#include "update/rules.hpp"
#include <curl/curl.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/debug.h>
#include <strings.h>
#include <sys/socket.h>
#include <fcntl.h>
#ifndef __XSDK__
#include "replay.hpp"
#endif

namespace yt {
namespace {
struct Body { string* data; size_t max; };
double monotonicMs() {
    timespec t{}; clock_gettime(CLOCK_MONOTONIC,&t);
    return double(t.tv_sec)*1000+t.tv_nsec/1e6;
}
struct Trace {
    const Transport* transport;
    const NetworkTrace* callback;
    NetworkTiming& timing;
    bool media;
    curl_socket_t socket=CURL_SOCKET_BAD;
    double start=monotonicMs(), previous=start;
    void tick() {
        if (!*callback) return;
        double now=monotonicMs(); timing.callbackGapMs=std::max(timing.callbackGapMs,now-previous); previous=now;
    }
    void phase(const char* name) {
        tick(); timing.phase=name;
        if (*callback) (*callback)(name,monotonicMs()-start);
    }
};
struct Headers { Response* response; Trace* trace; bool first=true; };
size_t append(char* bytes, size_t count, size_t unit, void* data) {
    auto& target = *static_cast<Body*>(data);
    auto& body = *target.data;
    if (unit && count > (target.max - body.size()) / unit) return 0;
    body.append(bytes, count * unit);
    return count * unit;
}
size_t header(char* bytes, size_t unit, size_t count, void* data) {
    auto& h=*static_cast<Headers*>(data);
    if (h.first) { h.first=false; h.trace->phase("http_response"); }
    size_t n = unit * count;
    if (n >= 14 && !strncasecmp(bytes, "Content-Range:", 14))
        h.response->contentRange.assign(bytes + 14, n - 14);
    if (n >= 10 && !strncasecmp(bytes, "Location:", 9)) {
        auto& loc = h.response->location;
        loc.assign(bytes+9,n-9);
        loc.erase(0,loc.find_first_not_of(" \t"));
        loc.erase(loc.find_last_not_of("\r\n ") + 1);
    }
    return n;
}
int progress(void* ptr, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto& trace=*static_cast<Trace*>(ptr); trace.tick();
    return trace.transport->cancelled();
}
int socketOptions(void* ptr, curl_socket_t fd, curlsocktype) {
    if (ptr) {
        auto& trace=*static_cast<Trace*>(ptr); trace.socket=fd;
        trace.phase("dns_done"); trace.phase("tcp_begin");
        if (!trace.media) return CURL_SOCKOPT_OK;
    }
    int bytes=256*1024;
    // O xsdk pode limitar o valor. O resultado efetivo sai nas métricas, sem rejeitar o socket.
    setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&bytes,sizeof(bytes));
    setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&bytes,sizeof(bytes));
    return CURL_SOCKOPT_OK;
}
// Observa as flags, sem limpar erros nem alterar a validação da cadeia/nome.
int certificate(void* data, mbedtls_x509_crt*, int, uint32_t* flags) {
    *static_cast<uint32_t*>(data) |= *flags;
    return 0;
}
struct TlsDiagnostic { int code=0; Trace* trace=nullptr; uint32_t* certificateFlags=nullptr; };
void tlsDebug(void* context, int, const char*, int, const char* message) {
    // Nível 1 contém erros; não gravar handshake, cabeçalhos, cookies ou material de chave.
    const char* hex=std::strstr(message,"-0x");
    if (!hex) return;
    char* end=nullptr; long code=std::strtol(hex,&end,16);
    if (end>hex+3 && code<0 && code>=-65535) static_cast<TlsDiagnostic*>(context)->code=(int)code;
}
CURLcode tlsContext(CURL*, void* context, void* diagnostic) {
    auto& d=*static_cast<TlsDiagnostic*>(diagnostic);
    if (d.trace) {
        if (d.trace->socket!=CURL_SOCKET_BAD) {
            int flags=fcntl(d.trace->socket,F_GETFL,0);
            if (flags>=0) d.trace->timing.nonblocking=(flags&O_NONBLOCK)!=0;
        }
        d.trace->phase("tls_begin");
    }
    mbedtls_ssl_conf_verify(static_cast<mbedtls_ssl_config*>(context),certificate,d.certificateFlags);
    mbedtls_ssl_conf_dbg(static_cast<mbedtls_ssl_config*>(context),tlsDebug,diagnostic);
    return CURLE_OK;
}
int traceText(CURL*, curl_infotype type, char* bytes, size_t size, void* ptr) {
    // Nenhum texto bruto do curl: nem headers, cookies, URL, certificado ou bytes TLS.
    if (ptr && type==CURLINFO_TEXT && size>=13 && !std::memcmp(bytes,"SSL connected",13))
        static_cast<Trace*>(ptr)->phase("tls_done");
    return 0;
}
class Https final : public Transport {
    uint32_t certificateFlags = 0;
    CURL* easy = nullptr;
    CURL* explicitCookies = nullptr;
    const std::atomic_bool* cancelFlag;
    std::atomic_bool aborted{false};
    TlsDiagnostic tlsDiagnostic;
    char errorBuffer[CURL_ERROR_SIZE]={};
public:
    explicit Https(const std::atomic_bool* flag) : cancelFlag(flag) {
        static const auto init = [] {
            boot_event("begin","tls.global","curl + mbedTLS, embedded roots");
            mbedtls_debug_set_threshold(1);
            auto result=curl_global_init(CURL_GLOBAL_DEFAULT);
            boot_event(result==CURLE_OK?"end":"error","tls.global","result=%d verifypeer=1 verifyhost=2 clock_epoch=%lld",int(result),(long long)std::time(nullptr));
            return result;
        }();
        if (init == CURLE_OK) easy = curl_easy_init();
    }
    ~Https() override {
        if (explicitCookies) curl_easy_cleanup(explicitCookies);
        if (easy) curl_easy_cleanup(easy);
    }
    bool cancelled() const override { return aborted || (cancelFlag && cancelFlag->load()); }
    void cancel() override { aborted=true; }
    Response request(const string& url, const string& body, bool form) override {
        Request r; r.url = url; r.body = body; r.form = form;
        return send(r);
    }
    Response send(const Request& r) override {
        const string& url = r.url;
        const string& body = r.body;
        const bool form = r.form;
        Response res;
        if (r.update && !updates::updateUrl(url)) { res.error="update_url"; return res; }
        Body target{&res.body, r.maxBytes ? std::min(r.maxBytes,size_t(6*1024*1024)) : size_t(updates::rules()->n("responseBytes"))};
        if (!easy) { res.error = "tls_init"; return res; }
        if (cancelled()) { res.error = "cancelled"; return res; }
        bool customCookies = false;
        for (const auto& h : r.headers) if (!strncasecmp(h.c_str(),"Cookie:",7)) customCookies = true;
        // O curl-xenon ainda envia o pote com Cookie: explícito, até vazio. Outro handle não cria esse pote.
        if (customCookies && !explicitCookies) explicitCookies = curl_easy_init();
        CURL* handle = customCookies ? explicitCookies : easy;
        if (!handle) { res.error = "tls_init"; return res; }
        certificateFlags=0;
        curl_easy_reset(handle); // Mantém DNS, sessões TLS e conexões do mesmo worker e identidade.
        Trace trace{this,&r.trace,res.timing,r.media}; Headers incoming{&res,&trace};
        tlsDiagnostic={0,&trace,&certificateFlags};
        std::memset(errorBuffer,0,sizeof(errorBuffer));
        curl_blob ca{const_cast<char*>(r.update ? updateRoots : ytRoots), r.update ? sizeof(updateRoots) : sizeof(ytRoots), CURL_BLOB_NOCOPY};
        curl_slist* headers = nullptr;
        CURLcode setup = CURLE_OK;
        auto set = [&](auto option, auto value) {
            if (setup == CURLE_OK) setup = curl_easy_setopt(handle, option, value);
        };
        set(CURLOPT_URL, url.c_str());
        set(CURLOPT_PROTOCOLS_STR, "https");
        set(CURLOPT_CAINFO_BLOB, &ca);
        set(CURLOPT_SSL_VERIFYHOST, 2L);
        set(CURLOPT_SSL_VERIFYPEER, 1L);
        set(CURLOPT_ERRORBUFFER,errorBuffer);
        set(CURLOPT_SSL_CTX_FUNCTION,tlsContext);
        set(CURLOPT_SSL_CTX_DATA,&tlsDiagnostic);
        set(CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_1_1));
        set(CURLOPT_USERAGENT, r.agent.empty() ? "Tube360/1.0" : r.agent.c_str());
        // Pote de cookies em memória; o reset do handle não apaga os cookies. Sem cookies no libcurl, segue sem eles.
        if (!customCookies) curl_easy_setopt(handle, CURLOPT_COOKIEFILE, "");
        set(CURLOPT_TIMEOUT_MS, 15000L);
        set(CURLOPT_CONNECTTIMEOUT_MS, 8000L);
        set(CURLOPT_NOSIGNAL, 1L);
        set(CURLOPT_FRESH_CONNECT, r.freshConnection ? 1L : 0L);
        if (r.trace) {
            set(CURLOPT_VERBOSE,1L); set(CURLOPT_DEBUGFUNCTION,traceText); set(CURLOPT_DEBUGDATA,&trace);
            set(CURLOPT_SOCKOPTFUNCTION,socketOptions); set(CURLOPT_SOCKOPTDATA,&trace);
        }
        if (r.media) {
            set(CURLOPT_BUFFERSIZE, r.readBuffer);
            set(CURLOPT_TIMEOUT_MS, 12000L);
            set(CURLOPT_LOW_SPEED_LIMIT, 16L*1024);
            set(CURLOPT_LOW_SPEED_TIME, 4L);
            set(CURLOPT_TCP_KEEPALIVE, 1L);
            set(CURLOPT_SOCKOPTFUNCTION, socketOptions);
        }
        if (r.connectTimeoutMs>0) set(CURLOPT_CONNECTTIMEOUT_MS,std::clamp(r.connectTimeoutMs,1L,60000L));
        if (r.timeoutMs>0) set(CURLOPT_TIMEOUT_MS,std::clamp(r.timeoutMs,1L,120000L));
        set(CURLOPT_HEADERFUNCTION, header);
        set(CURLOPT_HEADERDATA, &incoming);
        set(CURLOPT_WRITEFUNCTION, append);
        set(CURLOPT_WRITEDATA, &target);
        set(CURLOPT_NOPROGRESS, 0L);
        set(CURLOPT_XFERINFOFUNCTION, progress);
        set(CURLOPT_XFERINFODATA, &trace);
        for (const auto& h : r.headers) {
            auto next = curl_slist_append(headers, h.c_str());
            if (!next) { curl_slist_free_all(headers); res.error = "memory"; return res; }
            headers = next;
        }
        if (!body.empty()) {
            auto next = curl_slist_append(headers, form ? "Content-Type: application/x-www-form-urlencoded" : "Content-Type: application/json");
            if (!next) { curl_slist_free_all(headers); res.error = "memory"; return res; }
            headers = next;
        }
        if (headers) set(CURLOPT_HTTPHEADER, headers);
        if (!body.empty()) {
            set(CURLOPT_POSTFIELDS, body.data());
            set(CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        }
        static std::atomic_bool firstRequest{true};
        bool traceFirst=firstRequest.exchange(false);
        if(traceFirst)boot_event("begin","tls.first-request","kind=%s; URL, tokens and cookies omitted",r.media?"media":r.update?"update":"API");
        trace.phase("dns_begin");
        auto rc = setup == CURLE_OK ? curl_easy_perform(handle) : setup;
        trace.tick(); tlsDiagnostic.trace=nullptr;
        curl_easy_setopt(handle,CURLOPT_DEBUGDATA,nullptr);
        curl_easy_setopt(handle,CURLOPT_VERBOSE,0L);
        curl_easy_getinfo(handle,CURLINFO_OS_ERRNO,&res.timing.osError);
        double dns=0,tcp=0,tls=0,first=0;
        curl_easy_getinfo(handle,CURLINFO_NAMELOOKUP_TIME,&dns);
        curl_easy_getinfo(handle,CURLINFO_CONNECT_TIME,&tcp);
        curl_easy_getinfo(handle,CURLINFO_APPCONNECT_TIME,&tls);
        curl_easy_getinfo(handle,CURLINFO_STARTTRANSFER_TIME,&first);
        if (dns>0 || tcp>0 || tls>0 || first>0) res.timing.dnsMs=dns*1000;
        if (tcp>0) res.timing.tcpMs=std::max(0.0,tcp-dns)*1000;
        if (tls>0) res.timing.tlsMs=std::max(0.0,tls-tcp)*1000;
        // Este curl também preenche STARTTRANSFER ao encerrar certos erros TLS.
        // Só um header recebido prova primeiro byte HTTP; o timer isolado não prova.
        if (!incoming.first && first>0) res.timing.firstByteMs=first*1000;
        // Mesmo sem conexão concluída, o curl preserva DNS/TCP parciais no erro.
        if (rc==CURLE_OK) res.timing.phase="done";
        else if (!incoming.first) res.timing.phase="http_body";
        else if (tls>0) res.timing.phase="http_wait";
        else if (tcp>0) res.timing.phase="tls";
        else if (dns>0) res.timing.phase="tcp";
        else res.timing.phase="dns";
        res.transportCode=(long)rc; res.tlsCode=rc==CURLE_OK ? 0 : tlsDiagnostic.code;
        res.transportDetail=errorBuffer;
        // O backend pode soltar a conexão após falha; só consultar um contexto ainda disponível.
        curl_tlssessioninfo* session=nullptr;
        if (curl_easy_getinfo(handle,CURLINFO_TLS_SSL_PTR,&session)==CURLE_OK && session &&
            session->backend==CURLSSLBACKEND_MBEDTLS && session->internals)
            res.tlsVerify=(long)mbedtls_ssl_get_verify_result(static_cast<mbedtls_ssl_context*>(session->internals));
        if (!res.tlsCode && rc==CURLE_PEER_FAILED_VERIFICATION) res.tlsCode=MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
        // O erro final do handshake tem precedência sobre um erro interno capturado pelo debug.
        if (rc!=CURLE_OK) {
            const char* hex=std::strstr(errorBuffer,"-0x");
            if (hex) res.tlsCode=(int)std::strtol(hex,nullptr,16);
        }
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &res.status);
        if (std::getenv("XT_HTTP_DEBUG") && res.status != 200) std::fprintf(stderr, "http %ld %s -> %s\n", (long)res.status, url.c_str(), res.location.c_str());
        curl_easy_getinfo(handle, CURLINFO_NUM_CONNECTS, &res.newConnections);
        curl_easy_getinfo(handle, CURLINFO_TOTAL_TIME, &res.seconds);
        curl_socket_t fd=CURL_SOCKET_BAD;
        if (curl_easy_getinfo(handle,CURLINFO_ACTIVESOCKET,&fd)==CURLE_OK && fd!=CURL_SOCKET_BAD) {
            socklen_t n=sizeof(int);
            getsockopt(fd,SOL_SOCKET,SO_RCVBUF,&res.receiveBuffer,&n);
            n=sizeof(int); getsockopt(fd,SOL_SOCKET,SO_SNDBUF,&res.sendBuffer,&n);
        }
        curl_slist_free_all(headers);
        if (rc != CURLE_OK) {
            res.error = rc == CURLE_PEER_FAILED_VERIFICATION ? "tls_peer_verification" : curl_easy_strerror(rc);
            res.failure = rc==CURLE_ABORTED_BY_CALLBACK ? Failure::CANCELLED :
                rc==CURLE_PEER_FAILED_VERIFICATION || rc==CURLE_SSL_CONNECT_ERROR || rc==CURLE_SSL_CERTPROBLEM || rc==CURLE_SSL_CACERT_BADFILE ? Failure::TLS : Failure::NETWORK;
            if (rc==CURLE_COULDNT_RESOLVE_HOST || rc==CURLE_COULDNT_RESOLVE_PROXY) res.failure=Failure::DNS;
            if (res.failure==Failure::TLS && (certificateFlags & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE))) {
                res.failure=Failure::TLS_DATE; res.error="tls_certificate_date";
            }
            res.retryable = rc==CURLE_OPERATION_TIMEDOUT || rc==CURLE_PARTIAL_FILE || rc==CURLE_RECV_ERROR ||
                rc==CURLE_SEND_ERROR || rc==CURLE_COULDNT_CONNECT || rc==CURLE_COULDNT_RESOLVE_HOST || rc==CURLE_SSL_CONNECT_ERROR;
            // Só Range pode aproveitar um prefixo, depois de validar Content-Range. JSON continua atômico.
            if (!r.media || !res.retryable) res.body.clear();
        }
        if(traceFirst)boot_event(rc==CURLE_OK?"end":"error","tls.first-request","curl=%d HTTP=%ld failure=%s cert_flags=%08x",int(rc),res.status,failureName(res.failure),unsigned(certificateFlags));
        return res;
    }
};
}
std::unique_ptr<Transport> https(const std::atomic_bool* cancel) {
#ifndef __XSDK__
    if (const char* directory = std::getenv("XT_YT_REPLAY")) {
        static std::atomic_bool first{true};
        if(first.exchange(false))boot_event("info","tls.transport","desktop recorded responses; no TLS request made");
        return std::make_unique<Replay>(directory);
    }
#endif
    return std::make_unique<Https>(cancel);
}
}
