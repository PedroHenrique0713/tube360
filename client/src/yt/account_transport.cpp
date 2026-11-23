#include "console_account.hpp"
#ifndef __XSDK__
#include "picojson.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>

namespace yt {
namespace {
using J=picojson::value;
string read(const string& path) {
    std::ifstream f(path,std::ios::binary);
    std::ostringstream data; data<<f.rdbuf(); return data.str();
}
string str(const J& j,const char* key) {
    if (!j.is<picojson::object>()) return {};
    const auto& v=j.get(key); return v.is<string>() ? v.get<string>() : "";
}
class Replay final : public Transport {
    string dir;
    J rules;
public:
    explicit Replay(string path):dir(std::move(path)) {
        auto raw=read(dir+"/responses.json");
        if (!picojson::parse(rules,raw).empty()) rules=J();
    }
    Response request(const string& url,const string& body,bool form) override {
        Request r; r.url=url; r.body=body; r.form=form; return send(r);
    }
    Response send(const Request& r) override {
        if (const auto* log=std::getenv("XT_ACCOUNT_AUDIT")) if (auto* f=std::fopen(log,"a")) {
            // Somente transporte de replay, com marcadores falsos.
            std::fprintf(f,"%s\t%s\n",r.url.c_str(),r.body.empty()?"GET":"POST"); std::fclose(f);
        }
        if (!rules.is<picojson::array>()) return {0,{},"invalid_fixture"};
        for (const auto& rule:rules.get<picojson::array>()) {
            if (r.url.find(str(rule,"url"))==string::npos || r.body.find(str(rule,"contains"))==string::npos) continue;
            auto auth=str(rule,"authorization");
            if (!auth.empty() && std::find(r.headers.begin(),r.headers.end(),"Authorization: Bearer "+auth)==r.headers.end())
                return {401,"{}",""};
            const auto& wait=rule.get("delay_ms");
            if (wait.is<double>() && wait.get<double>()>0 && wait.get<double>()<=2000) {
                int ms=int(wait.get<double>());
                if (const auto* log=std::getenv("XT_ACCOUNT_AUDIT")) if (auto* f=std::fopen(log,"a")) {
                    std::fprintf(f,"fixture_delay_ms=%d\n",ms); std::fclose(f);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            }
            auto data=str(rule,"body");
            if (rule.contains("json")) data=rule.get("json").serialize();
            auto file=str(rule,"file");
            if (!file.empty() && file.find("..")==string::npos && file[0]!='/') data=read(dir+"/"+file);
            const auto& status=rule.get("status");
            return {status.is<double>()?long(status.get<double>()):200,data,{}};
        }
        return {0,{},"unexpected_fixture_request"};
    }
};
}
std::unique_ptr<Transport> accountTransport() {
    if (const auto* dir=std::getenv("XT_ACCOUNT_FIXTURES")) return std::make_unique<Replay>(dir);
    return https();
}
}
#else
namespace yt {
std::unique_ptr<Transport> accountTransport() { return https(); }
}
#endif
