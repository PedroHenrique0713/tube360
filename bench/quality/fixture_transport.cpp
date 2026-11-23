// Substitui apenas o transporte no executável de prova: o main/player/shader são os do produto.
#include "yt/client.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace yt {
namespace {
class Fixture final : public Transport {
    const std::atomic_bool* cancel;
public:
    explicit Fixture(const std::atomic_bool* flag) : cancel(flag) {}
    Response request(const string& url, const string& body, bool form) override {
        Request r; r.url=url; r.body=body; r.form=form; return send(r);
    }
    Response send(const Request& r) override {
        if (cancel && cancel->load()) return {0,"","cancelled"};
        if (r.url.find("/watch?")!=string::npos)
            return {200,R"(<script>ytcfg.set({"VISITOR_DATA":"fixture","jsUrl":"/s/player/fixture/base.js"})</script>)"};
        if (r.url.find("/base.js")!=string::npos) return {200,"var a={signatureTimestamp:20775};"};
        string dir=std::getenv("XT_QUALITY_FIXTURES") ? std::getenv("XT_QUALITY_FIXTURES") : "";
        if (r.url.find("/youtubei/v1/player?")!=string::npos) {
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
            if (name=="1080.mp4") if (const char* delay=std::getenv("XT_QUALITY_SLOW_MS")) SDL_Delay(std::max(0,std::atoi(delay)));
            if (!range) return {200,std::move(data)};
            return {206,std::move(data),"","bytes "+std::to_string(first)+"-"+std::to_string(last)+"/"+std::to_string(size)};
        }
        if (r.url.find("https://www.youtube.com/")==0) return {200,R"({"contents":[]})"};
        std::fprintf(stderr,"unexpected fixture URL: %s\n",r.url.c_str());
        return {400,"","unexpected fixture URL"};
    }
};
}
std::unique_ptr<Transport> https(const std::atomic_bool* cancel) { return std::make_unique<Fixture>(cancel); }
}
