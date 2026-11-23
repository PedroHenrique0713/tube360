#include "client.hpp"
#include "roots.hpp"
#include <curl/curl.h>

namespace yt {
namespace {
constexpr size_t maxBody = 6 * 1024 * 1024; // o base.js do player passa de 3 MB
size_t append(char* bytes, size_t count, size_t unit, void* data) {
    auto& body = *static_cast<string*>(data);
    if (unit && count > (maxBody - body.size()) / unit) return 0;
    body.append(bytes, count * unit);
    return count * unit;
}
int progress(void* ptr, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return ptr && static_cast<const std::atomic_bool*>(ptr)->load();
}
class Https final : public Transport {
    CURL* easy = nullptr;
    const std::atomic_bool* cancel;
public:
    explicit Https(const std::atomic_bool* flag) : cancel(flag) {
        static const auto init = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (init == CURLE_OK) easy = curl_easy_init();
    }
    ~Https() override { if (easy) curl_easy_cleanup(easy); }
    Response request(const string& url, const string& body, bool form) override {
        Request r; r.url = url; r.body = body; r.form = form;
        return send(r);
    }
    Response send(const Request& r) override {
        const string& url = r.url;
        const string& body = r.body;
        const bool form = r.form;
        Response res;
        if (!easy) { res.error = "tls_init"; return res; }
        if (cancel && cancel->load()) { res.error = "cancelled"; return res; }
        curl_easy_reset(easy); // Mantém DNS, sessões TLS e conexões do mesmo worker.
        curl_blob ca{const_cast<char*>(ytRoots), sizeof(ytRoots), CURL_BLOB_NOCOPY};
        curl_slist* headers = nullptr;
        CURLcode setup = CURLE_OK;
        auto set = [&](auto option, auto value) {
            if (setup == CURLE_OK) setup = curl_easy_setopt(easy, option, value);
        };
        set(CURLOPT_URL, url.c_str());
        set(CURLOPT_PROTOCOLS_STR, "https");
        set(CURLOPT_CAINFO_BLOB, &ca);
        set(CURLOPT_SSL_VERIFYHOST, 2L);
        set(CURLOPT_SSL_VERIFYPEER, 1L);
        set(CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_1_1));
        set(CURLOPT_USERAGENT, r.agent.empty() ? "Tube360/1.0" : r.agent.c_str());
        // Pote de cookies em memória; o reset do handle não apaga os cookies. Sem cookies no libcurl, segue sem eles.
        curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");
        set(CURLOPT_TIMEOUT_MS, 15000L);
        set(CURLOPT_CONNECTTIMEOUT_MS, 8000L);
        set(CURLOPT_NOSIGNAL, 1L);
        set(CURLOPT_WRITEFUNCTION, append);
        set(CURLOPT_WRITEDATA, &res.body);
        if (cancel) {
            set(CURLOPT_NOPROGRESS, 0L);
            set(CURLOPT_XFERINFOFUNCTION, progress);
            set(CURLOPT_XFERINFODATA, cancel);
        }
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
        auto rc = setup == CURLE_OK ? curl_easy_perform(easy) : setup;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &res.status);
        curl_slist_free_all(headers);
        if (rc != CURLE_OK) {
            res.body.clear();
            res.error = rc == CURLE_PEER_FAILED_VERIFICATION ? "tls_peer_verification" : curl_easy_strerror(rc);
        }
        return res;
    }
};
}
std::unique_ptr<Transport> https(const std::atomic_bool* cancel) {
    return std::make_unique<Https>(cancel);
}
}
