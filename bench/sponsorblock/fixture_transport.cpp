// Só este executável troca a rede. main/player/seek/UI são os mesmos do produto.
#include "yt/replay.hpp"
#include <SDL3/SDL.h>
#include <fstream>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

extern "C" VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue,const VkPresentInfoKHR* info) {
    static auto present=reinterpret_cast<PFN_vkQueuePresentKHR>(dlsym(RTLD_NEXT,"vkQueuePresentKHR"));
    if (!present) std::abort();
    auto result=present(queue,info); SDL_Delay(4); return result;
}
namespace yt {
class SponsorFixture final : public Transport {
    const std::atomic_bool* stop;
    std::string directory;
    Replay media;
public:
    explicit SponsorFixture(const std::atomic_bool* flag):stop(flag),directory(std::getenv("XT_SPONSOR_FIXTURES")),media(directory) {}
    bool cancelled() const override { return stop && stop->load(); }
    Response request(const string& url,const string& body,bool form) override { return send({url,body,{},form,{}}); }
    Response send(const Request& r) override {
        if (cancelled()) return {0,{},"cancelled"};
        if (r.url.find("https://sponsor.ajay.app/")==0) {
            const string expected="https://sponsor.ajay.app/api/skipSegments/5f6b?categories=%5B%22sponsor%22%2C%22selfpromo%22%2C%22interaction%22%5D&actionTypes=%5B%22skip%22%5D";
            if (r.url!=expected || !r.body.empty() || r.maxBytes!=512*1024) { SDL_Log("BAD SponsorBlock request: %s",r.url.c_str()); return {400,{},{}}; }
            SDL_Log("SponsorBlock fixture GET: %s",r.url.c_str());
            if (const char* delay=std::getenv("XT_SPONSOR_DELAY_MS")) {
                auto until=SDL_GetTicks()+unsigned(std::atoi(delay));
                while (!cancelled() && SDL_GetTicks()<until) SDL_Delay(5);
            }
            if (std::getenv("XT_SPONSOR_FAIL")) return {503,{},"fixture unavailable"};
            std::ifstream f(directory+"/public-prefix.json"); return {200,{std::istreambuf_iterator<char>(f),{}}};
        }
        if (r.url=="https://www.youtube.com/" || r.url.find("https://www.youtube.com/?hl=")==0)
            return {200,R"(<script>ytcfg.set({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"fixture"}}})</script>)"};
        if (r.url.find("/youtubei/v1/player?")!=string::npos || r.url.find("/watch?v=")!=string::npos || r.url.find("/base.js")!=string::npos || r.url.find("https://fixture.googlevideo.com/")==0) return media.send(r);
        if (r.url.find("/youtubei/v1/")!=string::npos) return {200,R"({"contents":[]})"};
        return {404,{},{}};
    }
};
std::unique_ptr<Transport> https(const std::atomic_bool* flag) { return std::make_unique<SponsorFixture>(flag); }
}
