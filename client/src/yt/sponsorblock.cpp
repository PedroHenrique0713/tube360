#include "sponsorblock.hpp"
#include "picojson.h"
#include <mbedtls/sha256.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace yt::sponsorblock {
const char* stateName(State state) {
    switch (state) {
    case State::SEGMENTS: return "ok";
    case State::EMPTY: return "empty";
    case State::INVALID_ID: return "invalid_id";
    case State::CANCELLED: return "cancelled";
    case State::TLS: return "tls_error";
    case State::NETWORK: return "network_error";
    case State::HTTP: return "http_error";
    case State::PARSE: return "parse_error";
    }
    return "unknown";
}
string Diagnostic::line() const {
    char codes[280], verification[32];
    if (verify<0) std::snprintf(verification,sizeof(verification),"unavailable");
    else std::snprintf(verification,sizeof(verification),"0x%08lx",(unsigned long)verify);
    std::snprintf(codes,sizeof(codes)," http=%ld curl=%ld mbedtls=-0x%04x verify=%s bytes=%u bucket=%u matched=%d rejected=%u cached=%d seconds=%.3f epoch=%lld",
        http,transport,(unsigned)(tls<0 ? -tls : tls),verification,(unsigned)bytes,(unsigned)bucket,matched,(unsigned)rejected,cached,seconds,(long long)epoch);
    string clean=detail.substr(0,240);
    for (char& c : clean) if (static_cast<unsigned char>(c)<32 || c==127) c=' ';
    string why=reason.substr(0,120);
    for (char& c : why) if (static_cast<unsigned char>(c)<32 || c==127) c=' ';
    return string("status=")+stateName(state)+" prefix="+prefix+" reason="+why+codes+(clean.empty() ? "" : " detail="+clean);
}
bool validVideo(const string& id) {
    return id.size()==11 && id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")==string::npos;
}
string hashPrefix(const string& id) {
    if (!validVideo(id)) return {};
    unsigned char digest[32];
    if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(id.data()),id.size(),digest,0)) return {};
    constexpr char hex[]="0123456789abcdef";
    return {hex[digest[0]>>4],hex[digest[0]&15],hex[digest[1]>>4],hex[digest[1]&15]};
}
string requestUrl(const string& id) {
    auto prefix=hashPrefix(id);
    if (prefix.empty()) return {};
    return "https://sponsor.ajay.app/api/skipSegments/"+prefix+
        "?categories=%5B%22sponsor%22%2C%22selfpromo%22%2C%22interaction%22%5D&actionTypes=%5B%22skip%22%5D";
}
namespace {
using J=picojson::value;
const J& get(const J& j, const char* key) { static const J nil; return j.is<picojson::object>() ? j.get(key) : nil; }
string text(const J& j) { return j.is<string>() ? j.get<string>() : string{}; }
bool bounded(const string& body) {
    if (body.size()>maxResponseBytes) return false;
    int depth=0, nodes=0; bool quoted=false, escaped=false, started=false, done=false;
    for (char c : body) {
        bool space=c==' ' || c=='\r' || c=='\n' || c=='\t';
        if (done) { if (!space) return false; continue; }
        if (!started) { if (space) continue; if (c!='[') return false; started=true; }
        if (quoted) { if (escaped) escaped=false; else if (c=='\\') escaped=true; else if (c=='"') quoted=false; }
        else if (c=='"') quoted=true;
        else if (c=='{' || c=='[') { if (++depth>16 || ++nodes>16384) return false; }
        else if (c=='}' || c==']') { if (--depth<0) return false; if (!depth) done=true; }
        else if (c==',' && ++nodes>16384) return false;
    }
    return done && depth==0 && !quoted;
}
}
Segments parse(const string& body, const string& id, Diagnostic* diagnostic) {
    Diagnostic ignored; auto& info=diagnostic ? *diagnostic : ignored;
    info.state=State::PARSE; info.reason="json_bounds";
    info.bytes=body.size(); info.bucket=info.rejected=0; info.matched=false;
    Segments out;
    if (!validVideo(id)) { info.state=State::INVALID_ID; info.reason="invalid_id"; return out; }
    if (!bounded(body)) return out;
    J root; string error;
    info.reason="json_syntax";
    try { picojson::parse(root,body.begin(),body.end(),&error); } catch (...) { info.reason="json_exception"; return out; }
    if (!error.empty()) { info.detail=error.substr(0,240); return out; }
    info.reason="root_not_array";
    if (!root.is<picojson::array>()) return out;
    info.bucket=root.get<picojson::array>().size();
    info.state=State::EMPTY; info.reason="video_not_in_bucket";
    for (const auto& video : root.get<picojson::array>()) {
        if (text(get(video,"videoID"))!=id) continue; // O prefixo devolve também vídeos de outras pessoas.
        info.matched=true;
        const auto& segments=get(video,"segments");
        if (!segments.is<picojson::array>()) { info.state=State::PARSE; info.reason="segments_not_array"; return {}; }
        info.reason="no_supported_segments";
        for (const auto& s : segments.get<picojson::array>()) {
            ++info.rejected;
            auto category=text(get(s,"category")), action=text(get(s,"actionType"));
            if (category!="sponsor" && category!="selfpromo" && category!="interaction") continue;
            if (!get(s,"actionType").is<picojson::null>() && (!get(s,"actionType").is<string>() || action!="skip")) continue;
            const auto& pair=get(s,"segment");
            if (!pair.is<picojson::array>() || pair.get<picojson::array>().size()!=2) continue;
            const auto& a=pair.get<picojson::array>();
            if (!a[0].is<double>() || !a[1].is<double>()) continue;
            double start=a[0].get<double>(), end=a[1].get<double>(), duration=0;
            const auto& d=get(s,"videoDuration");
            if (!d.is<picojson::null>() && !d.is<double>()) continue;
            if (d.is<double>()) duration=d.get<double>();
            if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(duration) ||
                start<0 || end<=start || end>7*24*3600 || duration<0) continue;
            if (out.size()==maxSegments) { info.state=State::PARSE; info.reason="segment_limit"; return {}; }
            out.push_back({start,end,duration});
            --info.rejected;
        }
        break;
    }
    std::sort(out.begin(),out.end(),[](const Segment& a,const Segment& b) { return a.start<b.start; });
    if (!out.empty()) { info.state=State::SEGMENTS; info.reason="segments"; }
    return out;
}
Segments Client::fetch(const string& id, uint64_t now, bool refresh) {
    last={}; last.prefix=hashPrefix(id); last.epoch=std::time(nullptr);
    if (!validVideo(id)) { last.state=State::INVALID_ID; last.reason="invalid_id"; return {}; }
    if (last.prefix.empty()) { last.state=State::INVALID_ID; last.reason="sha256_failed"; return {}; }
    if (net.cancelled()) { last.state=State::CANCELLED; last.reason="cancelled"; return {}; }
    if (!refresh) for (const auto& e : cache) if (e.id==id && now<e.expires) { last=e.diagnostic; last.cached=true; return e.segments; }
    Request request; request.url=requestUrl(id); request.maxBytes=maxResponseBytes;
    request.connectTimeoutMs=connectTimeoutMs; request.timeoutMs=requestTimeoutMs;
    request.freshConnection=refresh;
    auto response=net.send(request);
    Segments segments;
    last.http=response.status; last.transport=response.transportCode; last.tls=response.tlsCode;
    last.verify=response.tlsVerify; last.detail=response.transportDetail; last.bytes=response.body.size(); last.seconds=response.seconds;
    if (response.status==200 && response.error.empty()) segments=parse(response.body,id,&last);
    else if (response.error.empty() && response.status==404) { last.state=State::EMPTY; last.reason="http_404"; }
    else {
        auto failure=failureOf(response);
        last.state=failure==Failure::TLS ? State::TLS : failure==Failure::CANCELLED ? State::CANCELLED : failure==Failure::HTTP ? State::HTTP : State::NETWORK;
        last.reason=response.error.empty() ? "http_status" : response.error;
    }
    if (net.cancelled()) { last.state=State::CANCELLED; last.reason="cancelled"; return {}; }
    cache.erase(std::remove_if(cache.begin(),cache.end(),[&](const Entry& e) { return e.id==id || now>=e.expires; }),cache.end());
    if (cache.size()>=32) cache.erase(cache.begin());
    cache.push_back({id,now+(segments.empty() ? 60000u : 600000u),segments,last});
    return segments;
}
}
