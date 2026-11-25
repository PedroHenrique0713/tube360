#include "sponsorblock.hpp"
#include "picojson.h"
#include <mbedtls/sha256.h>
#include <algorithm>
#include <cmath>

namespace yt::sponsorblock {
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
Segments parse(const string& body, const string& id) {
    Segments out;
    if (!validVideo(id) || !bounded(body)) return out;
    J root; string error;
    try { picojson::parse(root,body.begin(),body.end(),&error); } catch (...) { return out; }
    if (!error.empty() || !root.is<picojson::array>()) return out;
    for (const auto& video : root.get<picojson::array>()) {
        if (text(get(video,"videoID"))!=id) continue; // O prefixo devolve também vídeos de outras pessoas.
        const auto& segments=get(video,"segments");
        if (!segments.is<picojson::array>()) return {};
        for (const auto& s : segments.get<picojson::array>()) {
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
            if (out.size()==maxSegments) return {}; // Resposta truncada poderia cortar um grupo sobreposto.
            out.push_back({start,end,duration});
        }
        break;
    }
    std::sort(out.begin(),out.end(),[](const Segment& a,const Segment& b) { return a.start<b.start; });
    return out;
}
Segments Client::fetch(const string& id, uint64_t now) {
    if (!validVideo(id) || net.cancelled()) return {};
    for (const auto& e : cache) if (e.id==id && now<e.expires) return e.segments;
    Request request; request.url=requestUrl(id); request.maxBytes=maxResponseBytes;
    auto response=net.send(request);
    Segments segments;
    if (response.status==200 && response.error.empty()) segments=parse(response.body,id);
    if (net.cancelled()) return {};
    cache.erase(std::remove_if(cache.begin(),cache.end(),[&](const Entry& e) { return e.id==id || now>=e.expires; }),cache.end());
    if (cache.size()>=32) cache.erase(cache.begin());
    cache.push_back({id,now+(segments.empty() ? 60000u : 600000u),segments});
    return segments;
}
}
