#pragma once
#ifdef TUBE360_UPDATE_TEST_CONFIG
#include "service.hpp"
#include <curl/curl.h>
#include <cstring>
#include <strings.h>

namespace updates {
// HTTP existe apenas na build de bancada, para a origem literal compilada com a chave descartável.
class TestTransport final:public yt::Transport {
    const std::atomic_bool* cancelFlag;
    CURL* handle=nullptr;
    struct Body {string bytes;size_t limit;};
    static size_t body(char* p,size_t a,size_t b,void* data) {
        auto& out=*static_cast<Body*>(data);
        if(b && a>(out.limit-out.bytes.size())/b)return 0;
        out.bytes.append(p,a*b);return a*b;
    }
    static size_t header(char* p,size_t a,size_t b,void* data) {
        auto& r=*static_cast<yt::Response*>(data);size_t n=a*b;
        if(n>=14 && !strncasecmp(p,"Content-Range:",14))r.contentRange.assign(p+14,n-14);
        if(n>=9 && !strncasecmp(p,"Location:",9)) {
            r.location.assign(p+9,n-9);
            r.location.erase(0,r.location.find_first_not_of(" \t"));
            r.location.erase(r.location.find_last_not_of(" \t\r\n")+1);
        }
        return n;
    }
    static int progress(void* p,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {return static_cast<TestTransport*>(p)->cancelled();}
public:
    explicit TestTransport(const std::atomic_bool* flag):cancelFlag(flag) {
        static const auto init=curl_global_init(CURL_GLOBAL_DEFAULT);
        if(init==CURLE_OK)handle=curl_easy_init();
    }
    ~TestTransport() override {if(handle)curl_easy_cleanup(handle);}
    bool cancelled() const override {return cancelFlag && *cancelFlag;}
    yt::Response request(const string& url,const string& bytes={},bool form=false) override {
        yt::Request r;r.url=url;r.body=bytes;r.form=form;return send(r);
    }
    yt::Response send(const yt::Request& r) override {
        const string base=TUBE360_UPDATE_TEST_BASE;
        if(!handle || !r.update || r.url.compare(0,base.size(),base)!=0 || !updateUrl(r.url))return {0,{},"test_origin"};
        yt::Response response;Body out{{},r.maxBytes};curl_slist* headers=nullptr;
        for(const auto& h:r.headers) {
            auto* next=curl_slist_append(headers,h.c_str());
            if(!next){curl_slist_free_all(headers);return {0,{},"memory"};}headers=next;
        }
        curl_easy_reset(handle);
        curl_easy_setopt(handle,CURLOPT_URL,r.url.c_str());
        curl_easy_setopt(handle,CURLOPT_PROTOCOLS_STR,"http");
        curl_easy_setopt(handle,CURLOPT_PROXY,"");
        curl_easy_setopt(handle,CURLOPT_TIMEOUT_MS,15000L);
        curl_easy_setopt(handle,CURLOPT_CONNECTTIMEOUT_MS,3000L);
        curl_easy_setopt(handle,CURLOPT_NOSIGNAL,1L);
        curl_easy_setopt(handle,CURLOPT_HTTPHEADER,headers);
        curl_easy_setopt(handle,CURLOPT_WRITEFUNCTION,body);
        curl_easy_setopt(handle,CURLOPT_WRITEDATA,&out);
        curl_easy_setopt(handle,CURLOPT_HEADERFUNCTION,header);
        curl_easy_setopt(handle,CURLOPT_HEADERDATA,&response);
        curl_easy_setopt(handle,CURLOPT_NOPROGRESS,0L);
        curl_easy_setopt(handle,CURLOPT_XFERINFOFUNCTION,progress);
        curl_easy_setopt(handle,CURLOPT_XFERINFODATA,this);
        auto rc=curl_easy_perform(handle);
        curl_easy_getinfo(handle,CURLINFO_RESPONSE_CODE,&response.status);
        if(rc!=CURLE_OK)response.error=curl_easy_strerror(rc);else response.body=std::move(out.bytes);
        curl_slist_free_all(headers);return response;
    }
};
}
#endif
