#include "lounge.hpp"
#include "roots.hpp"
#include <curl/curl.h>

namespace cast {
namespace {
struct Reader {
    Frames frames;
    const std::function<bool(const string&)>& deliver;
    long status = 0;
};
size_t receive(char* bytes, size_t count, size_t unit, void* ptr) {
    auto& r = *static_cast<Reader*>(ptr);
    size_t n = count * unit;
    if (r.status != 200 || !r.frames.feed(bytes,n,r.deliver)) return 0;
    return n;
}
size_t headers(char* bytes, size_t count, size_t unit, void* ptr) {
    auto& r = *static_cast<Reader*>(ptr);
    string line(bytes,count * unit);
    if (line.compare(0,5,"HTTP/") == 0) {
        auto at = line.find(' ');
        r.status = at == string::npos ? 0 : std::strtol(line.c_str() + at + 1,nullptr,10);
    }
    return count * unit;
}
int progress(void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<const std::atomic_bool*>(p)->load();
}
}
long channel(const string& url, const std::atomic_bool& cancel, const std::function<bool(const string&)>& deliver) {
    // Leitor independente do worker de POSTs. O servidor pode manter o GET aberto por minutos.
    auto init = yt::https(&cancel);
    CURL* easy = curl_easy_init();
    if (!easy) return 0;
    Reader reader{{},deliver};
    curl_blob ca{const_cast<char*>(ytRoots), sizeof(ytRoots), CURL_BLOB_NOCOPY};
    CURLcode setup = CURLE_OK;
    auto set = [&](auto option, auto value) { if (setup == CURLE_OK) setup = curl_easy_setopt(easy,option,value); };
    set(CURLOPT_URL,url.c_str()); set(CURLOPT_PROTOCOLS_STR,"https");
    set(CURLOPT_CAINFO_BLOB,&ca); set(CURLOPT_SSL_VERIFYHOST,2L); set(CURLOPT_SSL_VERIFYPEER,1L);
    set(CURLOPT_USERAGENT,"Tube360/1.0"); set(CURLOPT_HTTP_VERSION,long(CURL_HTTP_VERSION_1_1));
    set(CURLOPT_CONNECTTIMEOUT_MS,8000L); set(CURLOPT_LOW_SPEED_LIMIT,1L); set(CURLOPT_LOW_SPEED_TIME,60L);
    set(CURLOPT_NOSIGNAL,1L); set(CURLOPT_NOPROGRESS,0L);
    set(CURLOPT_XFERINFOFUNCTION,progress); set(CURLOPT_XFERINFODATA,&cancel);
    set(CURLOPT_HEADERFUNCTION,headers); set(CURLOPT_HEADERDATA,&reader);
    set(CURLOPT_WRITEFUNCTION,receive); set(CURLOPT_WRITEDATA,&reader);
    auto rc = setup == CURLE_OK ? curl_easy_perform(easy) : setup;
    long status = 0;
    curl_easy_getinfo(easy,CURLINFO_RESPONSE_CODE,&status);
    curl_easy_cleanup(easy);
    if (rc != CURLE_OK && status == 200 && !cancel) return 0;
    return status;
}
}
