#include "rules.hpp"
#include "rules-default.hpp"
#include <algorithm>
#include <climits>
#include <cstring>
#include <set>

namespace updates {
namespace {
bool profile(const J& j,Profile& p) {
    if (!fields(j,{"name","code","version","agent","device","headers"})) return false;
    p.name=text(j,"name"); p.code=text(j,"code"); p.version=text(j,"version"); p.agent=text(j,"agent"); p.device=get(j,"device");
    if (!ascii(p.name,40) || !ascii(p.code,8) || p.code.find_first_not_of("0123456789")!=string::npos ||
        !ascii(p.version,64,true) || !ascii(p.agent,512) || !p.device.is<O>() || p.device.get<O>().size()>12 || !get(j,"headers").is<A>()) return false;
    for (const auto& kv:p.device.get<O>()) {
        if (kv.first!="deviceMake" && kv.first!="deviceModel" && kv.first!="osName" && kv.first!="osVersion" &&
            kv.first!="androidSdkVersion" && kv.first!="platform" && kv.first!="clientScreen") return false;
        if (kv.first=="androidSdkVersion") { if (number(kv.second,1,100)<0) return false; }
        else if (!ascii(text(kv.second),128)) return false;
    }
    for(const auto& h:get(j,"headers").get<A>()) {
        string v=text(h); auto colon=v.find(':'); auto name=v.substr(0,colon);
        if (!ascii(v,512) || colon==string::npos || (name!="Accept" && name!="Accept-Language" && name!="X-Goog-Api-Format-Version" && name!="X-Youtube-Bootstrap-Logged-In")) return false;
        p.headers.push_back(v);
    }
    return p.headers.size()<=8;
}
bool decode(const J& j,Rules& r,const J& base) {
    if (!fields(j,{"schema","kind","version","strings","limits","timestampMarkers","players","tv","samsung","fallbackStatuses"}) ||
        number(get(j,"schema"),1,1)!=1 || text(j,"kind")!="tube360-rules" || (r.version=number(get(j,"version"),1,INT_MAX))<1) return false;
    const auto& strings=get(j,"strings"); const auto& limits=get(j,"limits");
    if (!strings.is<O>() || !limits.is<O>()) return false;
    // Chaves introduzidas por uma versão do app podem faltar em regras assinadas para versões anteriores: o padrão embutido
    // preenche. Qualquer outra chave que falte ou sobre reprova, como antes.
    static const std::set<string> addedStrings={"tvPlayerPath"}, addedLimits={"nsig"};
    for (const auto& kv:strings.get<O>()) if (!get(base,"strings").get<O>().count(kv.first)) return false;
    for (const auto& kv:limits.get<O>()) if (!get(base,"limits").get<O>().count(kv.first)) return false;
    for (const auto& kv:get(base,"strings").get<O>()) {
        if (!strings.get<O>().count(kv.first)) {
            if (!addedStrings.count(kv.first)) return false;
            r.strings[kv.first]=text(get(base,"strings"),kv.first);
            continue;
        }
        auto s=text(strings,kv.first);
        if (!get(strings,kv.first).is<string>() || !ascii(s,1024,kv.first=="apiKey" || kv.first=="tvClientId" || kv.first=="tvClientSecret")) return false;
        if (kv.first.find("Origin")!=string::npos) {
            auto h=host(s); if (s!="https://"+h || (kv.first=="oauthOrigin" ? h!="oauth2.googleapis.com" && h!="www.youtube.com" : h!="www.youtube.com" && h!="music.youtube.com" && h!="youtube.com")) return false;
        }
        if (kv.first.find("Path")!=string::npos || kv.first=="swReferer")
            if (s[0]!='/' || s.compare(0,2,"//")==0 || s.find("..")!=string::npos || s.find_first_of("\\#")!=string::npos || s.find("://")!=string::npos) return false;
        if (kv.first.find("Header")!=string::npos && (s.compare(0,2,"X-")!=0 || s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-")!=string::npos)) return false;
        if (kv.first=="apiKey" && s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=string::npos) return false;
        r.strings[kv.first]=s;
    }
    const std::map<string,std::pair<int,int>> bounds={{"responseBytes",{65536,6*1024*1024}},{"jsonBytes",{4096,3*1024*1024}},
        {"cards",{1,120}},{"retries",{1,3}},{"backoffMs",{100,2000}},{"markerBytes",{32,8192}},{"timestampMax",{10000,100000000}},
        {"listPages",{1,40}},{"listItems",{24,1024}},{"queueItems",{1,256}},{"catalogItems",{24,240}},{"parserDepth",{4,80}},{"parserNodes",{128,160000}},{"checkHours",{1,12}},{"failures",{2,8}},{"successes",{2,8}},{"pageItems",{1,48}},{"nsig",{0,1}}};
    for (const auto& kv:bounds) {
        const bool added=addedLimits.count(kv.first) && !limits.get<O>().count(kv.first);
        int n=number(added ? get(get(base,"limits"),kv.first) : get(limits,kv.first),kv.second.first,kv.second.second);
        if(n<0) return false; r.limits[kv.first]=n;
    }
    for (auto name:{"timestampMarkers","fallbackStatuses"}) {
        const auto& a=get(j,name); if(!a.is<A>() || a.get<A>().empty() || a.get<A>().size()>6) return false;
        auto& out=std::strcmp(name,"timestampMarkers")==0 ? r.timestampMarkers : r.fallbackStatuses;
        for(const auto& v:a.get<A>()) { if(!ascii(text(v),80)) return false; out.push_back(text(v)); }
    }
    const auto& a=get(j,"players"); if (!a.is<A>() || a.get<A>().empty() || a.get<A>().size()>5) return false;
    for(const auto& j:a.get<A>()) { Profile p; if(!profile(j,p) || p.version.empty()) return false; r.players.push_back(std::move(p)); }
    return profile(get(j,"tv"),r.tv) && profile(get(j,"samsung"),r.samsung) && !r.samsung.version.empty();
}
std::shared_ptr<RuleStore> global;
std::mutex globalMu;
}
bool decodeRules(const string& raw,Rules& r) {
    J j,base; r={};
    return raw.size()<=maxDocument && parse(raw,j) && parse(defaultRules,base) && decode(j,r,base);
}
string Rules::api(const string& route,bool music) const { return s(music?"musicOrigin":"webOrigin")+s("apiPath")+route+s("apiQuery")+(s("apiKey").empty()?"":"&key="+s("apiKey")); }
RuleStore::RuleStore(string dir,string pub):path(dir+"update-rules-state.json"),key(std::move(pub)) {
    Rules baked; if (!decodeRules(defaultRules,baked)) std::abort();
    active=std::make_shared<Rules>(baked); goodBytes=defaultRules;
    J state; if (!parse(read(path,maxDocument*5),state)) return;
    int h=number(get(state,"highest"),1,INT_MAX), bad=number(get(state,"rejected"),0,INT_MAX);
    auto load=[&](const char* prefix,string& bytes,string& sig,Rules& parsed) {
        bytes=text(state,string(prefix)+"Bytes"); sig=text(state,string(prefix)+"Sig");
        return (bytes==defaultRules || verify(bytes,sig,key)) && decodeRules(bytes,parsed);
    };
    string gb,gs,ab,as; Rules g,a;
    if (h<1 || bad<0 || !load("good",gb,gs,g) || !load("active",ab,as,a) || h<std::max(g.version,a.version)) return;
    highest=h; rejected=bad; goodBytes=gb; goodSig=gs; activeBytes=ab; activeSig=as; active=std::make_shared<Rules>(a);
    failures=std::max(0,number(get(state,"failures"),0,8)); successes=std::max(0,number(get(state,"successes"),0,8));
}
bool RuleStore::save() {
    return write(path,J(O{{"highest",J(double(highest))},{"rejected",J(double(rejected))},{"failures",J(double(failures))},{"successes",J(double(successes))},
        {"activeBytes",J(activeBytes.empty()?string(defaultRules):activeBytes)},{"activeSig",J(activeSig)},{"goodBytes",J(goodBytes)},{"goodSig",J(goodSig)}}).serialize());
}
std::shared_ptr<const Rules> RuleStore::snapshot() const { std::lock_guard<std::mutex> l(mu); return active; }
int RuleStore::highWater() const { std::lock_guard<std::mutex> l(mu); return highest; }
int RuleStore::failureCount() const { std::lock_guard<std::mutex> l(mu); return failures; }
bool RuleStore::accept(const string& bytes,const string& sig,string& error) {
    if(!verify(bytes,sig,key)) {error="signature";return false;}
    Rules r; if(!decodeRules(bytes,r)) {error="schema";return false;}
    std::lock_guard<std::mutex> l(mu);
    if(r.version<=highest || r.version<=rejected) {error="version";return false;}
    auto old=active; auto ob=activeBytes,os=activeSig,ogb=goodBytes,ogs=goodSig; int oh=highest,of=failures,oss=successes;
    if(successes>=active->n("successes") && !activeBytes.empty()){goodBytes=activeBytes;goodSig=activeSig;}
    activeBytes=bytes; activeSig=sig; highest=r.version; failures=successes=0; active=std::make_shared<Rules>(r);
    if(!save()) {active=old;activeBytes=ob;activeSig=os;goodBytes=ogb;goodSig=ogs;highest=oh;failures=of;successes=oss;error="cache";return false;}
    error.clear(); return true;
}
void RuleStore::observe(int version,bool success,bool incompatible) {
    if(!success && !incompatible) return;
    std::lock_guard<std::mutex> l(mu);
    if(version!=active->version || activeBytes==goodBytes || activeBytes.empty()) return;
    if(success) {failures=0;successes=std::min(successes+1,active->n("successes"));}
    else {successes=0; if(++failures>=active->n("failures")) {
        rejected=std::max(rejected,version); Rules good; decodeRules(goodBytes,good); active=std::make_shared<Rules>(good); activeBytes=goodBytes;activeSig=goodSig; failures=successes=0;
    }}
    save();
}
void setRuleStore(std::shared_ptr<RuleStore> s) {std::lock_guard<std::mutex> l(globalMu);global=std::move(s);}
std::shared_ptr<const Rules> rules() {
    std::lock_guard<std::mutex> l(globalMu);
    if(global) return global->snapshot();
    static auto baked=[] {Rules r;decodeRules(defaultRules,r);return std::make_shared<const Rules>(r);}(); return baked;
}
void observe(int v,bool s,bool f) {std::shared_ptr<RuleStore> g;{std::lock_guard<std::mutex> l(globalMu);g=global;}if(g)g->observe(v,s,f);}
}
