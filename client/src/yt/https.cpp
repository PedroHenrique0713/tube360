#include "client.hpp"
#include "roots.hpp"
#include <curl/curl.h>

namespace yt {
namespace {
constexpr size_t maxBody = 3 * 1024 * 1024;
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
        set(CURLOPT_USERAGENT, "Tube360/1.0");
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
        if (!body.empty()) {
            headers = curl_slist_append(nullptr, form ? "Content-Type: application/x-www-form-urlencoded" : "Content-Type: application/json");
            if (!headers) { res.error = "memory"; return res; }
            set(CURLOPT_HTTPHEADER, headers);
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
