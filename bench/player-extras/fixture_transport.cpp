// Substitui apenas o transporte no executável de prova: o main/player/shader são os do produto.
#include "yt/client.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <dlfcn.h>
#include <vulkan/vulkan.h>
#include <mutex>

// Só o executável de prova injeta consumidor lento; o produto liga diretamente ao Vulkan.
extern "C" VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    static auto present=reinterpret_cast<PFN_vkQueuePresentKHR>(dlsym(RTLD_NEXT,"vkQueuePresentKHR"));
    static int delay=std::getenv("XT_QUALITY_RENDER_MS") ? std::max(0,std::atoi(std::getenv("XT_QUALITY_RENDER_MS"))) : 0;
    if (!present) std::abort();
    VkResult result=present(queue,info);
    if (delay) SDL_Delay(delay);
    return result;
}

namespace yt {
namespace {
class Fixture final : public Transport {
    const std::atomic_bool* cancelFlag;
    std::atomic_bool aborted{false};
public:
    explicit Fixture(const std::atomic_bool* flag) : cancelFlag(flag) {}
    bool cancelled() const override { return aborted || (cancelFlag && cancelFlag->load()); }
    void cancel() override { aborted=true; }
    Response request(const string& url, const string& body, bool form) override {
        Request r; r.url=url; r.body=body; r.form=form; return send(r);
    }
    Response send(const Request& r) override {
        if (cancelled()) return {0,"","cancelled"};
        if (r.url.find("/watch?")!=string::npos)
            return {200,R"(<script>ytcfg.set({"VISITOR_DATA":"fixture","jsUrl":"/s/player/fixture/base.js"})</script>)"};
        if (r.url.find("/base.js")!=string::npos) return {200,"var a={signatureTimestamp:20775};"};
        string dir=std::getenv("XT_QUALITY_FIXTURES") ? std::getenv("XT_QUALITY_FIXTURES") : "";
        if (r.url.find("/youtubei/v1/player?")!=string::npos) {
            if (std::getenv("XT_QUALITY_SERVER")) return {200,R"({"playabilityStatus":{"status":"LOGIN_REQUIRED","reason":"fixture server fallback"}})"};
            std::ifstream f(dir+"/player.json"); std::ostringstream out; out<<f.rdbuf();
            return f ? Response{200,out.str()} : Response{404,"","missing fixture"};
        }
        const string base="https://media.example/";
        if (r.url.find(base)==0 && r.url.find("..",base.size())==string::npos) {
            string name=r.url.substr(base.size());
            std::ifstream f(dir+"/"+name,std::ios::binary);
            if (!f) return {404,"","missing media fixture"};
            f.seekg(0,std::ios::end); auto size=(long long)f.tellg();
            long long first=0,last=size-1; bool range=false;
            for (const auto& h : r.headers) if (h.find("Range: bytes=")==0) { range=true; std::sscanf(h.c_str(),"Range: bytes=%lld-%lld",&first,&last); }
            if (first<0 || first>=size || last<first) return {416,"","invalid range"};
            last=std::min(last,size-1); f.seekg(first);
            string data(size_t(last-first+1),'\0'); f.read(data.data(),data.size());
            if (const char* rate=std::getenv("XT_QUALITY_MBIT")) {
                static std::mutex mutex;
                static uint64_t until=0;
                double mbps=std::atof(rate);
                if (mbps>0) {
                    for (size_t offset=0;offset<data.size() && !cancelled();offset+=16384) {
                        uint64_t deadline;
                        size_t bytes=std::min(size_t(16384),data.size()-offset);
                        { std::lock_guard<std::mutex> lock(mutex); until=std::max(until,SDL_GetTicks())+uint64_t(bytes*.008/mbps); deadline=until; }
                        while (SDL_GetTicks()<deadline && !cancelled()) SDL_Delay(5);
                    }
                    if (cancelled()) return {0,"","cancelled"};
                }
            }
            if (name=="1080.mp4") if (const char* delay=std::getenv("XT_QUALITY_SLOW_MS")) SDL_Delay(std::max(0,std::atoi(delay)));
            if (!range) return {200,std::move(data)};
            return {206,std::move(data),"","bytes "+std::to_string(first)+"-"+std::to_string(last)+"/"+std::to_string(size)};
        }
        if (r.url=="https://www.youtube.com/?hl=pt" || r.url=="https://www.youtube.com/?hl=en" || r.url=="https://www.youtube.com/?hl=es")
            return {200,R"(<script>ytcfg.set({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"fixture"}}})</script>)"};
        if (r.url.find("/youtubei/v1/next?")!=string::npos) {
            SDL_Log("extras fixture next: %s",r.body.c_str());
            if (std::getenv("XT_EXTRAS_COMMENTS_ERROR")) return {503,"","fixture offline"};
            string name=r.body.find("PAGE_TWO")!=string::npos ? "comments-two.json" : r.body.find("PAGE_ONE")!=string::npos ? "comments-one.json" : "comments-one.json";
            std::ifstream f(dir+"/"+name); std::ostringstream out; out<<f.rdbuf();
            return f ? Response{200,out.str()} : Response{404,"","missing comments fixture"};
        }
        if (r.url=="https://returnyoutubedislikeapi.com/votes?videoId=4p_9OPKgcDE") {
            SDL_Log("extras fixture RYD");
            if (std::getenv("XT_EXTRAS_VOTES_ERROR")) return {429,"","fixture throttled"};
            return {200,R"({"id":"4p_9OPKgcDE","likes":3210,"dislikes":87})"};
        }
        if (r.url.find("https://www.youtube.com/")==0) return {200,R"({"contents":[]})"};
        std::fprintf(stderr,"unexpected fixture URL: %s\n",r.url.c_str());
        return {400,"","unexpected fixture URL"};
    }
};
}
std::unique_ptr<Transport> https(const std::atomic_bool* cancel) { return std::make_unique<Fixture>(cancel); }
}
