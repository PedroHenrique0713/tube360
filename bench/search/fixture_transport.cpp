#include "yt/catalog_replay.hpp"
#include "yt/search.hpp"
#include <SDL3/SDL.h>
class SearchReplay final : public yt::Transport {
    yt::catalog::Replay replay;
    const std::atomic_bool* cancel;
public:
    SearchReplay(const char* directory,const std::atomic_bool* flag):replay(directory),cancel(flag) {}
    bool cancelled() const override { return cancel && cancel->load(); }
    yt::Response request(const yt::string& url,const yt::string& body={},bool form=false) override { return send({url,body,{},form}); }
    yt::Response send(const yt::Request& r) override {
        bool suggest=r.url.find("https://suggestqueries-clients6.youtube.com/")==0;
        if (auto* path=std::getenv("XT_SEARCH_AUDIT")) if (auto* f=std::fopen(path,"a")) {
            std::fprintf(f,"%s\t%s\n",suggest ? "suggest" : "request",r.url.c_str());
            if (!r.body.empty()) std::fprintf(f,"body\t%s\n",r.body.c_str());
            std::fclose(f);
        }
        if (suggest) {
            int delay=std::getenv("XT_SEARCH_DELAY_MS") ? std::atoi(std::getenv("XT_SEARCH_DELAY_MS")) : 0;
            if (delay>0 && !backoff(unsigned(std::min(delay,2500)))) return {0,{},"cancelled"};
            if (std::getenv("XT_SEARCH_ERROR")) return {503,"unavailable",{}};
            auto response=replay.send(r);
            if (response.status==0 && std::getenv("XT_SEARCH_SYNTHETIC_QUERY")) {
                yt::Request known=r; known.url=yt::search::url("astronomia","pt"); response=replay.send(known);
                // Only the echo changes. Completions are from the public recording, intentionally synthetic here.
                auto start=response.body.find("astronomia");
                if (start!=yt::string::npos) response.body.replace(start,10,r.url.substr(r.url.find("&q=")+3));
            }
            return response;
        }
        return replay.send(r);
    }
};
std::unique_ptr<yt::Transport> replayHttps(const std::atomic_bool*) asm("__wrap__ZN2yt5httpsEPKSt6atomicIbE");
std::unique_ptr<yt::Transport> replayHttps(const std::atomic_bool* cancel) {
    const char* dir=std::getenv("XT_SEARCH_REPLAY");
    return std::make_unique<SearchReplay>(dir ? dir : "",cancel);
}
