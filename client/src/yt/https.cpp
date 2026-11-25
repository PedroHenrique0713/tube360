#include <cstdio>
#include <cstdlib>
#include "client.hpp"
#include "roots.hpp"
#include "update-roots.hpp"
#include "update/rules.hpp"
#include <curl/curl.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <strings.h>
#include <sys/socket.h>
#ifndef __XSDK__
#include "replay.hpp"
#endif

namespace yt {
namespace {
struct Body { string* data; size_t max; };
size_t append(char* bytes, size_t count, size_t unit, void* data) {
    auto& target = *static_cast<Body*>(data);
    auto& body = *target.data;
    if (unit && count > (target.max - body.size()) / unit) return 0;
    body.append(bytes, count * unit);
    return count * unit;
}
size_t header(char* bytes, size_t unit, size_t count, void* data) {
    size_t n = unit * count;
    if (n >= 14 && !strncasecmp(bytes, "Content-Range:", 14))
        static_cast<Response*>(data)->contentRange.assign(bytes + 14, n - 14);
    if (n >= 10 && !strncasecmp(bytes, "Location:", 9)) {
        auto& loc = static_cast<Response*>(data)->location;
        loc.assign(bytes+9,n-9);
        loc.erase(0,loc.find_first_not_of(" \t"));
        loc.erase(loc.find_last_not_of("\r\n ") + 1);
    }
    return n;
}
int progress(void* ptr, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<const Transport*>(ptr)->cancelled();
}
int socketOptions(void*, curl_socket_t fd, curlsocktype) {
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
CURLcode tlsContext(CURL*, void* context, void* data) {
    mbedtls_ssl_conf_verify(static_cast<mbedtls_ssl_config*>(context),certificate,data);
    return CURLE_OK;
}
class Https final : public Transport {
    uint32_t certificateFlags = 0;
    CURL* easy = nullptr;
    CURL* explicitCookies = nullptr;
    const std::atomic_bool* cancelFlag;
    std::atomic_bool aborted{false};
public:
    explicit Https(const std::atomic_bool* flag) : cancelFlag(flag) {
        static const auto init = curl_global_init(CURL_GLOBAL_DEFAULT);
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
        set(CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_1_1));
        set(CURLOPT_USERAGENT, r.agent.empty() ? "Tube360/1.0" : r.agent.c_str());
        // Pote de cookies em memória; o reset do handle não apaga os cookies. Sem cookies no libcurl, segue sem eles.
        if (!customCookies) curl_easy_setopt(handle, CURLOPT_COOKIEFILE, "");
        set(CURLOPT_TIMEOUT_MS, 15000L);
        set(CURLOPT_CONNECTTIMEOUT_MS, 8000L);
        set(CURLOPT_NOSIGNAL, 1L);
        set(CURLOPT_FRESH_CONNECT, r.freshConnection ? 1L : 0L);
        if (r.media) {
            set(CURLOPT_BUFFERSIZE, r.readBuffer);
            set(CURLOPT_TIMEOUT_MS, 12000L);
            set(CURLOPT_LOW_SPEED_LIMIT, 16L*1024);
            set(CURLOPT_LOW_SPEED_TIME, 4L);
            set(CURLOPT_TCP_KEEPALIVE, 1L);
            set(CURLOPT_SOCKOPTFUNCTION, socketOptions);
        }
        set(CURLOPT_SSL_CTX_FUNCTION, tlsContext);
        set(CURLOPT_SSL_CTX_DATA, &certificateFlags);
        set(CURLOPT_HEADERFUNCTION, header);
        set(CURLOPT_HEADERDATA, &res);
        set(CURLOPT_WRITEFUNCTION, append);
        set(CURLOPT_WRITEDATA, &target);
        set(CURLOPT_NOPROGRESS, 0L);
        set(CURLOPT_XFERINFOFUNCTION, progress);
        set(CURLOPT_XFERINFODATA, this);
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
        auto rc = setup == CURLE_OK ? curl_easy_perform(handle) : setup;
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
        return res;
    }
};
}
std::unique_ptr<Transport> https(const std::atomic_bool* cancel) {
#ifndef __XSDK__
    if (const char* directory = std::getenv("XT_YT_REPLAY")) return std::make_unique<Replay>(directory);
#endif
    return std::make_unique<Https>(cancel);
}
}
