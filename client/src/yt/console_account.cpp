#include "console_account.hpp"
#include "membership_json.hpp"
#include "picojson.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace yt {
namespace {
using J = picojson::value;
using O = picojson::object;
using A = picojson::array;
const string origin = "https://www.youtube.com";
const string tvAgent = "Mozilla/5.0 (ChromiumStylePlatform) Cobalt/Version";
const string samsungAgent = "Mozilla/5.0 (SMART-TV; Linux; Tizen 2.4.0) AppleWebKit/538.1 (KHTML, like Gecko) Version/2.4.0 TV Safari/538.1";
const J& get(const J& j, const char* k) { static const J nil; return j.is<O>() ? j.get(k) : nil; }
const A& arr(const J& j) { static const A empty; return j.is<A>() ? j.get<A>() : empty; }
string str(const J& j) { return j.is<string>() ? j.get<string>() : ""; }
string str(const J& j, const char* k) { return str(get(j,k)); }
bool flag(const J& j, const char* k) { const auto& v=get(j,k); return v.is<bool>() && v.get<bool>(); }
double num(const J& j) { return j.is<double>() && std::isfinite(j.get<double>()) ? j.get<double>() : 0; }
string text(const J& j) {
    if (j.is<string>()) return str(j);
    auto s=str(j,"simpleText");
    if (s.empty()) s=str(j,"content");
    if (s.empty()) for (auto& run:arr(get(j,"runs"))) s+=str(run,"text");
    return s;
}
string clean(string s) {
    for (char& c:s) if (static_cast<unsigned char>(c)<32 || c==127) c=' ';
    return s;
}
string enc(const string& s) {
    const char hex[]="0123456789ABCDEF"; string out;
    for (unsigned char c:s) {
        if ((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='-' || c=='_' || c=='.' || c=='~') out+=c;
        else { out+='%'; out+=hex[c>>4]; out+=hex[c&15]; }
    }
    return out;
}
string dec(const string& s) {
    string out;
    for (size_t i=0;i<s.size();++i) {
        if (s[i]=='%' && i+2<s.size()) {
            auto digit=[](char c) { return c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : c>='A' && c<='F' ? c-'A'+10 : -1; };
            int a=digit(s[i+1]), b=digit(s[i+2]);
            if (a>=0 && b>=0) { out+=char(a*16+b); i+=2; continue; }
        }
        out+=s[i]=='+' ? ' ' : s[i];
    }
    return out;
}
std::map<string,string> params(const string& s, char separator='&') {
    std::map<string,string> out;
    size_t start=0;
    while (start<s.size()) {
        auto end=s.find(separator,start); if (end==string::npos) end=s.size();
        auto eq=s.find('=',start);
        if (eq<end) out[dec(s.substr(start,eq-start))]=dec(s.substr(eq+1,end-eq-1));
        start=end+1;
    }
    return out;
}
bool id(const string& s, size_t length=0) {
    return !s.empty() && s.size()<=160 && (!length || s.size()==length) &&
        s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")==string::npos;
}
bool channel(const string& s) { return id(s,24) && s.compare(0,2,"UC")==0; }
bool headerValue(const string& s) {
    return s.size()<=8192 && std::all_of(s.begin(),s.end(),[](unsigned char c){return c>32 && c<127;});
}
bool parse(const string& raw, J& j) {
    if (raw.size()>3*1024*1024) return false;
    bool quoted=false, escaped=false;
    int depth=0, nodes=0;
    for (char c:raw) {
        if (quoted) {
            if (escaped) escaped=false;
            else if (c=='\\') escaped=true;
            else if (c=='"') quoted=false;
        } else if (c=='"') quoted=true;
        else if (c=='{' || c=='[') { if (++depth>80 || ++nodes>160000) return false; }
        else if (c=='}' || c==']') { if (--depth<0) return false; }
        else if (c==',' && ++nodes>160000) return false;
    }
    if (depth || quoted) return false;
    try { return picojson::parse(j,raw).empty() && j.is<O>(); }
    catch (const std::exception&) { return false; }
}
string read(const string& path) {
    auto* f=std::fopen(path.c_str(),"rb"); if (!f) return {};
    string s; char buf[4096]; size_t n;
    while ((n=std::fread(buf,1,sizeof(buf),f)) && s.size()+n<=3*1024*1024) s.append(buf,n);
    bool ok=std::feof(f) && !std::ferror(f);
    std::fclose(f); return ok ? s : "";
}
string response(const Response& r, J& j) {
    if (!r.error.empty() || r.status>=500 || r.status==429) return "unavailable";
    if (r.status==401 || r.status==403) return "unauthorized";
    if (r.status!=200 || !parse(r.body,j) || j.contains("error")) return "invalid_response";
    return {};
}
void nodes(const J& j, const string& name, std::vector<const J*>& out) {
    if (j.is<A>()) for (auto& c:arr(j)) nodes(c,name,out);
    else if (j.is<O>()) for (auto& kv:j.get<O>()) {
        if (kv.first==name) out.push_back(&kv.second);
        else if (kv.first!="menu" && kv.first!="onLongPressCommand" && kv.first!="frameworkUpdates") nodes(kv.second,name,out);
    }
}
const J& first(const J& j, const char* name) {
    std::vector<const J*> found; nodes(j,name,found);
    static const J nil; return found.empty() ? nil : *found.front();
}
string image(const J& j) {
    std::vector<const J*> found; nodes(j,"thumbnails",found); nodes(j,"sources",found);
    string out; double best=0;
    for (auto* group:found) for (auto& v:arr(*group)) {
        double w=num(get(v,"width")); auto u=str(v,"url");
        if (u.compare(0,2,"//")==0) u="https:"+u;
        if (u.compare(0,8,"https://")==0 && (out.empty() || (w<=1024 && w>best))) { out=u; best=w; }
    }
    return out;
}
string channelId(const J& j) {
    std::vector<const J*> found; nodes(j,"browseEndpoint",found);
    for (auto* ep:found) if (channel(str(*ep,"browseId"))) return str(*ep,"browseId");
    return {};
}
int seconds(const string& s) {
    int value=0, part=0;
    if (s.size()>12 || s.empty()) return 0;
    for (char c:s) {
        if (c==':') { value=value*60+part; part=0; }
        else if (c>='0' && c<='9') part=part*10+c-'0';
        else return 0;
    }
    return std::min(864000,value*60+part);
}

// A API de TV usa o grant legado e a identidade que /tv publica no JavaScript.
// DeviceLogin conserva o relógio, o backoff e a validação dos tokens.
class TVOAuth : public Transport {
    Transport& net;
    string deviceId;
public:
    explicit TVOAuth(Transport& t):net(t) {
        auto now=std::chrono::steady_clock::now().time_since_epoch().count();
        deviceId="tube360-"+std::to_string(static_cast<unsigned long long>(now));
    }
    Response request(const string& url, const string& body, bool) override {
        auto form=params(body); O payload;
        for (auto& kv:form) payload[kv.first]=J(kv.second);
        if (url.find("device/code")!=string::npos) {
            payload["scope"]=J(string("http://gdata.youtube.com https://www.googleapis.com/auth/youtube-paid-content"));
            payload["device_id"]=J(deviceId); payload["device_model"]=J(string("ytlr::"));
        } else if (form.count("device_code")) {
            payload.erase("device_code"); payload["code"]=J(form["device_code"]);
            payload["grant_type"]=J(string("http://oauth.net/grant_type/device/1.0"));
        }
        auto route=url.substr(url.find_last_of('/')+1);
        if (url.find("device/code")!=string::npos) route="device/code";
        Request r; r.url=origin+"/o/oauth2/"+route; r.body=J(payload).serialize(); r.agent=tvAgent;
        return net.send(r);
    }
};

struct Item {
    string type, id, title, byline, image, channelId, section;
    int duration=0, pos=0;
    Membership members=Membership::None;
};
struct Parsed {
    std::vector<Item> items;
    string token;
    int tokenDepth=999;
    std::set<string> seen;
};
bool ignored(const string& key) {
    static const std::set<string> keys={"menu","onLongPressCommand","onFocusCommand","header","thumbnail","navigationEndpoint",
        "onSelectCommand","frameworkUpdates","playerOverlays","transportControls"};
    return keys.count(key);
}
void render(const J& v, const string& key, const string& section, Parsed& out) {
    if (!v.is<O>()) return;
    const auto& meta=get(get(v,"metadata"),"tileMetadataRenderer");
    const auto& lock=get(get(v,"metadata"),"lockupMetadataViewModel");
    const auto& title=get(meta,"title");
    const auto& ep=v.contains("onSelectCommand") ? get(v,"onSelectCommand") :
        key=="lockupViewModel" ? get(get(get(get(v,"rendererContext"),"commandContext"),"onTap"),"innertubeCommand") : get(v,"navigationEndpoint");
    const auto& browse=get(ep,"browseEndpoint");
    const auto& watch=get(ep,"watchEndpoint");
    auto type=str(v,"contentType");
    Item item; item.section=section; item.members=membership(v);
    item.id=str(v,"videoId");
    if (item.id.empty()) item.id=str(watch,"videoId");
    if (item.id.empty()) item.id=str(browse,"browseId");
    if (item.id.empty()) item.id=str(v,"playlistId");
    if (item.id.empty()) item.id=str(v,"channelId");
    if (item.id.empty()) item.id=str(v,"contentId");
    bool playlist=type.find("PLAYLIST")!=string::npos || key=="playlistRenderer" || key=="gridPlaylistRenderer";
    if (playlist) {
        item.id=str(browse,"browseId");
        if (item.id.empty()) item.id=str(v,"contentId");
        if (item.id.empty()) item.id=str(v,"playlistId");
        if (item.id.empty()) item.id=str(watch,"playlistId");
    }
    item.title=text(title);
    if (item.title.empty()) item.title=text(get(lock,"title"));
    if (item.title.empty()) item.title=text(get(v,"title"));
    if (item.title.empty()) item.title=item.id;
    item.byline=text(get(v,"shortBylineText"));
    if (item.byline.empty()) item.byline=text(get(v,"longBylineText"));
    if (item.byline.empty()) item.byline=text(get(v,"subtitle"));
    item.channelId=channelId(get(v,"shortBylineText"));
    if (item.channelId.empty()) item.channelId=channelId(get(meta,"lines"));
    for (auto& line:arr(get(meta,"lines"))) {
        string words;
        for (auto& part:arr(get(get(line,"lineRenderer"),"items"))) {
            auto s=text(get(get(part,"lineItemRenderer"),"text"));
            if (!s.empty()) words+=(words.empty()?"":" ")+s;
        }
        if (!words.empty() && item.byline.empty()) item.byline=words;
    }
    item.image=image(v);
    item.duration=seconds(text(get(v,"lengthText")));
    const auto& overlays=key=="tileRenderer" ? get(get(get(v,"header"),"tileHeaderRenderer"),"thumbnailOverlays") : get(v,"thumbnailOverlays");
    bool hidden=flag(v,"isUpcoming") || v.contains("upcomingEventData") || get(ep,"reelWatchEndpoint").is<O>();
    const auto& playable=get(v,"isPlayable");
    hidden=hidden || (playable.is<bool>() && !playable.get<bool>() && item.members==Membership::None);
    for (auto& overlay:arr(overlays)) {
        const auto& timing=get(overlay,"thumbnailOverlayTimeStatusRenderer");
        auto style=str(timing,"style");
        hidden=hidden || style.find("UPCOMING")!=string::npos || style.find("SHORTS")!=string::npos;
        if (!item.duration) item.duration=seconds(text(get(timing,"text")));
        double progress=num(get(get(overlay,"thumbnailOverlayResumePlaybackRenderer"),"percentDurationWatched"));
        item.pos=int(item.duration*std::clamp(progress,0.0,100.0)/100.0+0.5);
    }
    std::vector<const J*> badges; nodes(get(v,"badges"),"metadataBadgeRenderer",badges); nodes(get(meta,"lines"),"metadataBadgeRenderer",badges);
    for (auto* badge:badges) {
        auto style=str(*badge,"style");
        hidden=hidden || style.find("SHORTS")!=string::npos || style.find("UPCOMING")!=string::npos;
    }
    if (hidden || !id(item.id)) return;
    if (playlist) item.type="playlist";
    else if (channel(item.id) || type.find("CHANNEL")!=string::npos) item.type="channel";
    else if (type=="LOCKUP_CONTENT_TYPE_PLAYLIST" || item.id.compare(0,2,"VL")==0 || item.id.compare(0,2,"PL")==0 || item.id.compare(0,2,"RD")==0) item.type="playlist";
    else if (id(item.id,11)) item.type="video";
    else return;
    if (item.type!="video") item.members=Membership::None;
    if (out.seen.insert(item.type+item.id).second && out.items.size()<256) out.items.push_back(std::move(item));
}
void walk(const J& j, Parsed& out, const string& section="", int depth=0) {
    if (j.is<A>()) for (auto& c:arr(j)) walk(c,out,section,depth+1);
    else if (j.is<O>()) for (auto& kv:j.get<O>()) {
        const auto& k=kv.first; const auto& v=kv.second;
        static const std::set<string> renderers={"tileRenderer","videoRenderer","gridVideoRenderer","playlistVideoRenderer","playlistPanelVideoRenderer",
            "lockupViewModel","channelRenderer","gridChannelRenderer","playlistRenderer","gridPlaylistRenderer"};
        if (renderers.count(k)) render(v,k,section,out);
        else if (k=="tabRenderer") { if (flag(v,"selected")) walk(get(v,"content"),out,section,depth+1); }
        else if (k=="shelfRenderer") {
            auto heading=text(get(v,"title"));
            if (heading.empty()) heading=text(get(get(get(get(get(v,"headerRenderer"),"shelfHeaderRenderer"),"avatarLockup"),"avatarLockupRenderer"),"title"));
            walk(get(v,"content"),out,heading.empty()?section:heading,depth+1);
            walk(get(v,"contents"),out,heading.empty()?section:heading,depth+1);
        } else if (k=="continuationCommand" || k=="nextContinuationData") {
            auto token=str(v,"token"); if (token.empty()) token=str(v,"continuation");
            if (!token.empty() && depth<out.tokenDepth) { out.token=token; out.tokenDepth=depth; }
        } else if (!ignored(k)) walk(v,out,section,depth+1);
    }
}
bool safeImage(const string& url) {
    if (url.compare(0,8,"https://")!=0) return false;
    if (url.find_first_of("\r\n\\\t ")!=string::npos) return false;
    auto slash=url.find('/',8);
    auto host=url.substr(8,slash-8);
    return url.compare(0,8,"https://")==0 && (host=="i.ytimg.com" || host=="yt3.ggpht.com" ||
        host=="yt3.googleusercontent.com" || host=="lh3.googleusercontent.com");
}
bool safeStats(const string& url) {
    return (url.compare(0,34,"https://www.youtube.com/api/stats/")==0 ||
            url.compare(0,32,"https://s.youtube.com/api/stats/")==0) &&
        url.find_first_of("\r\n\\\t ")==string::npos;
}
string replaceQuery(string url, const std::map<string,string>& values) {
    auto q=url.find('?'); string base=url.substr(0,q); auto p=q==string::npos ? std::map<string,string>{} : params(url.substr(q+1));
    for (auto& kv:values) p[kv.first]=kv.second;
    string result=base+"?"; for (auto& kv:p) { if (result.back()!='?') result+='&'; result+=enc(kv.first)+"="+enc(kv.second); }
    return result;
}
}

struct ConsoleAccount::Impl {
    Transport& net;
    TVOAuth oauth;
    std::mutex mutex;
    std::mutex tokenMutex;
    string publishedToken;
    std::atomic_uint64_t tokenGeneration{0};
    string path, clientId, secret, version="7.20250219.14.00", visitor, name, avatar;
    DeviceCode code;
    Tokens tokens;
    bool loaded=false, identityKnown=false, tokenDirty=false;
    int64_t wall=0, monotonic=0, retryRefresh=0;
    struct List {
        std::vector<Item> items;
        std::set<string> seen, continuations;
        string continuation, title, subtitle, image, banner, historyVideo, laterVideo, channelId, route="browse", fields;
        bool started=false, more=true, subscribed=false, videosOnly=false;
        unsigned pages=0;
        int64_t created=0;
    };
    std::map<string,List> lists;
    std::map<string,string> images;
    unsigned imageSerial=0;
    struct Tracking { string playback, watchtime, cpn; bool added=false; };
    std::map<string,Tracking> tracking;

    Impl(Transport& t,string p):net(t),oauth(t),path(std::move(p)) {}
    void load() {
        if (loaded) return;
        loaded=true;
        J j; if (!parse(read(path),j)) return;
        auto& c=get(j,"client");
        auto a=str(j,"access_token"), r=str(j,"refresh_token");
        auto cid=str(c,"client_id"), cs=str(c,"client_secret");
        if (a.empty() || r.empty() || cid.empty() || cs.empty() ||
            !headerValue(a) || !headerValue(r) || !headerValue(cid) || !headerValue(cs)) return;
        auto expiry=num(get(j,"expires_at"));
        if (expiry<0 || expiry>9000000000000000.0) return;
        tokens={a,r,int64_t(expiry)};
        clientId=cid; secret=cs; name=str(j,"name");
        if (!str(j,"client_version").empty() && headerValue(str(j,"client_version"))) version=str(j,"client_version");
    }
    bool save() {
        tokenDirty=true;
        O j{{"access_token",J(tokens.access)},{"refresh_token",J(tokens.refresh)},{"expires_at",J(double(tokens.expires))},
            {"name",J(name)},{"client_version",J(version)},
            {"client",J(O{{"client_id",J(clientId)},{"client_secret",J(secret)}})}};
        auto raw=J(j).serialize(); auto temp=path+".tmp";
        // 0600 no PC. FATX não oferece ACL, então o arquivo só deve viver na pasta do app.
        int flags=O_WRONLY|O_CREAT|O_TRUNC;
#ifdef O_NOFOLLOW
        flags|=O_NOFOLLOW;
#endif
        int fd=::open(temp.c_str(),flags,0600);
        if (fd<0) return false;
#ifndef __XSDK__
        bool ok=::fchmod(fd,0600)==0;
#else
        bool ok=true;
#endif
        size_t off=0;
        while (ok && off<raw.size()) {
            auto n=::write(fd,raw.data()+off,raw.size()-off);
            if (n<=0) ok=false; else off+=size_t(n);
        }
#ifndef __XSDK__
        if (ok && ::fsync(fd)!=0) ok=false;
#endif
        if (::close(fd)!=0) ok=false;
        if (ok) ok=std::rename(temp.c_str(),path.c_str())==0;
        if (!ok) std::remove(temp.c_str());
        tokenDirty=!ok;
        return ok;
    }
    void clear() {
        code={}; tokens={}; name.clear(); avatar.clear(); visitor.clear(); identityKnown=false; tokenDirty=false;
        retryRefresh=0; lists.clear(); tracking.clear(); images.clear();
    }
    string discover() {
        Request r; r.url=origin+"/tv"; r.agent=tvAgent;
        r.headers={"Referer: https://www.youtube.com/tv","Accept-Language: en-US"};
        auto html=net.send(r);
        if (html.status!=200 || !html.error.empty()) return "unavailable";
        std::smatch match;
        if (!std::regex_search(html.body,match,std::regex(R"rx(<script\s+id="base-js"\s+src="([^"]+)"[^>]*>)rx"))) return "tv_identity_missing";
        auto url=match[1].str();
        if (url.compare(0,2,"//")==0) url="https:"+url;
        else if (!url.empty() && url[0]=='/') url=origin+url;
        if (url.compare(0,origin.size()+1,origin+"/")!=0 || url.find_first_of("\r\n\\ ")!=string::npos) return "tv_identity_missing";
        r.url=url; auto script=net.send(r);
        if (script.status!=200 || !script.error.empty()) return "unavailable";
        if (!std::regex_search(script.body,match,std::regex(R"rx(clientId:"([^"]+)",[^"]*?:"([^"]+)")rx"))) return "tv_identity_missing";
        clientId=match[1]; secret=match[2];
        if (!headerValue(clientId) || !headerValue(secret)) return "tv_identity_missing";
        // A versão do bundle acompanha a identidade que esse mesmo app publica.
        if (std::regex_search(html.body,match,std::regex(R"rx("INNERTUBE_CLIENT_VERSION"\s*:\s*"([^"]+)")rx"))) { if (headerValue(match[1])) version=match[1]; }
        return {};
    }
    string auth() {
        if (tokens.refresh.empty()) return "signed_out";
        if (wall<tokens.expires-60) return tokenDirty && !save() ? "token_save_failed" : "";
        if (wall<retryRefresh) return "unavailable";
        DeviceLogin login(oauth,clientId,secret);
        auto e=login.refresh(wall,tokens);
        if (e=="invalid_grant" || e=="invalid_token") { clear(); std::remove(path.c_str()); return "signed_out"; }
        if (!e.empty()) { retryRefresh=wall+30; return e; }
        retryRefresh=0;
        if (!headerValue(tokens.access) || !headerValue(tokens.refresh)) { clear(); return "invalid_token_response"; }
        return save() ? "" : "token_save_failed";
    }
    string call(const string& route,const string& fields,J& j,bool samsung=false) {
        if (!tokens.refresh.empty()) { auto e=auth(); if (!e.empty()) return e; }
        O client{{"clientName",J(string("TVHTML5"))},{"clientVersion",J(samsung ? string("5.20260707") : version)},
            {"hl",J(language)},{"gl",J(language=="pt" ? string("BR") : string("US"))},
            {"timeZone",J(string("UTC"))},{"utcOffsetMinutes",J(0.0)}};
        if (!visitor.empty()) client["visitorData"]=J(visitor);
        if (samsung) {
            client["deviceMake"]=J(string("Samsung")); client["deviceModel"]=J(string("UKS9800"));
            client["osName"]=J(string("Tizen")); client["osVersion"]=J(string("2.4.0"));
            client["userAgent"]=J(samsungAgent);
        }
        O context{{"client",J(client)},{"user",J(O{{"enableSafetyMode",J(false)}})}};
        Request r; r.url=origin+"/youtubei/v1/"+route+"?prettyPrint=false"; r.agent=samsung ? samsungAgent : tvAgent;
        r.headers={"Origin: "+origin,"X-Youtube-Client-Name: 7","X-Youtube-Client-Version: "+(samsung ? string("5.20260707") : version)};
        if (!tokens.access.empty()) r.headers.push_back("Authorization: Bearer "+tokens.access);
        if (!visitor.empty()) r.headers.push_back("X-Goog-Visitor-Id: "+visitor);
        r.body="{\"context\":"+J(context).serialize()+(fields.empty() ? "" : ","+fields)+"}";
        auto res=net.send(r);
        auto e=response(res,j);
        bool readable=route=="browse" || route=="next" || route=="player" || route=="account/accounts_list";
        if (e=="unauthorized" && !readable) tokens.expires=wall;
        if (e=="unauthorized" && readable && !tokens.refresh.empty()) {
            tokens.expires=wall;
            e=auth(); if (!e.empty()) return e;
            for (auto& header:r.headers) if (header.compare(0,22,"Authorization: Bearer ")==0) header="Authorization: Bearer "+tokens.access;
            j=J(); e=response(net.send(r),j);
        }
        const auto& contextResponse=get(j,"responseContext");
        if (headerValue(str(contextResponse,"visitorData"))) visitor=str(contextResponse,"visitorData");
        return e;
    }
    string language="en";
    void identity() {
        if (identityKnown || tokens.access.empty()) return;
        J j;
        if (!call("account/accounts_list","",j).empty()) return;
        std::vector<const J*> accounts; nodes(j,"accountItem",accounts); nodes(j,"accountItemRenderer",accounts);
        for (auto* account:accounts) if (flag(*account,"isSelected") || name.empty()) {
            name=clean(text(get(*account,"accountName")));
            avatar=registerImage(image(get(*account,"accountPhoto")));
            if (avatar.empty()) avatar=registerImage(image(get(*account,"avatar")));
            if (flag(*account,"isSelected")) break;
        }
        identityKnown=true;
        save();
    }
    AccountReply status(bool start) {
        if (!code.device.empty()) {
            DeviceLogin login(oauth,clientId,secret); Tokens next;
            auto e=login.poll(monotonic,code,next);
            if (e.empty()) {
                if (!headerValue(next.access) || !headerValue(next.refresh)) { clear(); return {{}, "invalid_token_response"}; }
                tokens=next; tokens.expires=wall+next.expires-monotonic;
                if (!save()) { clear(); return {{}, "token_save_failed"}; }
            } else if (e!="authorization_pending" && e!="slow_down" && e!="temporarily_unavailable" && e!="expired_token" && e!="access_denied")
                return {{},e};
        }
        if (!tokens.refresh.empty()) {
            auto e=auth(); if (!e.empty() && e!="signed_out") return {{},e};
            if (!tokens.refresh.empty()) {
                identity();
                if (!tokens.refresh.empty()) return {"ok\tsigned_in\t"+clean(name)+"\t\t\t"+avatar+"\n",{}};
            }
        }
        if (start && code.device.empty()) {
            if (clientId.empty()) { auto e=discover(); if (!e.empty()) return {{},e}; }
            DeviceLogin login(oauth,clientId,secret);
            auto e=login.start(monotonic,code); if (!e.empty()) return {{},e};
        }
        if (!code.device.empty()) return {"ok\tpending\t\t"+clean(code.user)+"\t"+clean(code.verificationUrl)+"\n",{}};
        return {"ok\tsigned_out\t\t\t\n",{}};
    }
    string registerImage(const string& url) {
        if (!safeImage(url)) return {};
        for (auto& kv:images) if (kv.second==url) return kv.first;
        if (images.size()>=512) images.erase(images.begin());
        auto key="~yt"+std::to_string(++imageSerial);
        images[key]=url; return key;
    }
    string itemImage(const Item& item) {
        if (item.type=="video") return item.id;
        return registerImage(item.image);
    }
    string fetch(List& list, const string& kind, const string& itemId, const string& tab) {
        J j;
        auto fields=list.started ? "\"continuation\":"+J(list.continuation).serialize() : list.fields;
        auto e=call(list.route,fields,j);
        if (!e.empty()) return e;
        if (j.get<O>().empty()) return "invalid_page";
        if (!list.started) {
            const auto& header=first(j,kind=="channel" ? "channelHeaderRenderer" : "playlistHeaderRenderer");
            const auto& fallback=kind=="channel" ? first(j,"c4TabbedHeaderRenderer") : first(j,"overlayPanelHeaderRenderer");
            const auto& h=header.is<O>() ? header : fallback;
            list.title=text(get(h,"title")); list.subtitle=text(get(h,"subtitle"));
            list.image=registerImage(image(get(h,"avatar")));
            if (kind=="channel") {
                list.banner=registerImage(image(get(h,"banner")));
                if (list.banner.empty()) list.banner=registerImage(image(get(h,"tvBanner")));
                if (list.banner.empty()) list.banner=registerImage(image(first(j,"tvSurfaceContentBackgroundRenderer")));
                list.channelId=itemId;
                list.subscribed=flag(first(h,"subscribeButtonRenderer"),"subscribed");
                std::vector<const J*> shelves; nodes(j,"shelfRenderer",shelves);
                const auto pattern=tab=="live" ? "live|vivo|transmiss" : tab=="playlists" ? "playlists" : "^videos$|^vídeos$|^uploads$";
                bool matched=false;
                for (auto* shelf:shelves) {
                    auto title=text(get(*shelf,"title"));
                    if (title.empty()) title=text(get(first(*shelf,"avatarLockupRenderer"),"title"));
                    if (std::regex_search(title,std::regex(pattern,std::regex::icase))) { j=J(O{{"shelfRenderer",*shelf}}); matched=true; break; }
                }
                if (!shelves.empty() && !matched) j=J(O{{"contents",J(A{})}});
            }
        }
        Parsed parsed; walk(j,parsed);
        if (kind=="library" && !list.started) {
            for (const auto& preview: {std::pair{"FEhistory", &list.historyVideo}, std::pair{"VLWL", &list.laterVideo}}) {
                J data;
                auto error=call("browse", "\"browseId\":"+J(string(preview.first)).serialize(), data);
                // A revogação também apaga lists; nenhuma referência da página continua válida.
                if (tokens.refresh.empty()) return "signed_out";
                if (error.empty()) {
                    Parsed cover; walk(data, cover);
                    auto video=std::find_if(cover.items.begin(),cover.items.end(),[](const Item& item) { return item.type=="video"; });
                    if (video!=cover.items.end()) *preview.second=video->id;
                }
            }
            parsed.items.erase(std::remove_if(parsed.items.begin(),parsed.items.end(),[](const Item& item) { return item.id=="WL" || item.id=="VLWL"; }),parsed.items.end());
            int previews=0;
            for (auto& item:parsed.items) if (item.type=="playlist" && item.image.empty() && previews++<8) {
                J data;
                bool mix=item.id.compare(0,2,"RD")==0 || item.id.compare(0,4,"VLRD")==0;
                string fields=mix ? "\"playlistId\":"+J(item.id.substr(item.id.compare(0,2,"VL")==0 ? 2 : 0)).serialize() :
                    "\"browseId\":"+J(item.id.compare(0,2,"VL")==0 ? item.id : "VL"+item.id).serialize();
                auto error=call(mix ? "next" : "browse",fields,data);
                if (tokens.refresh.empty()) return "signed_out";
                if (error.empty()) {
                    Parsed preview; walk(data,preview);
                    auto video=std::find_if(preview.items.begin(),preview.items.end(),[](const Item& i) { return i.type=="video"; });
                    if (video!=preview.items.end()) item.image="https://i.ytimg.com/vi/"+video->id+"/mqdefault.jpg";
                }
            }
        }
        if (kind=="subscriptions" && !list.videosOnly && !list.started &&
            std::none_of(parsed.items.begin(),parsed.items.end(),[](const Item& i){return i.type=="channel";})) {
            J channels;
            e=call("browse","\"browseId\":\"FEchannels\"",channels);
            if (!e.empty()) return e;
            Parsed extra; walk(channels,extra);
            for (auto it=extra.items.rbegin();it!=extra.items.rend();++it) if (it->type=="channel") parsed.items.insert(parsed.items.begin(),*it);
        }
        for (auto& item:parsed.items) if ((!list.videosOnly || item.type=="video") && list.items.size()<1024 && list.seen.insert(item.type+item.id).second) list.items.push_back(std::move(item));
        list.started=true; list.pages++;
        list.continuation=parsed.token;
        list.more=!parsed.token.empty() && list.pages<40 && list.items.size()<1024 && list.continuations.insert(parsed.token).second;
        return {};
    }
    AccountReply listing(const std::map<string,string>& p, bool feed, bool play=false) {
        auto value=[&](const char* k) { auto it=p.find(k); return it==p.end() ? string{} : it->second; };
        auto kind=value("kind"), itemId=value("id"), tab=value("tab");
        if (kind=="subs") kind="subscriptions";
        bool privateList=kind=="subscriptions" || kind=="library" || kind=="history" || kind=="later" || itemId=="WL" || itemId=="VLWL";
        if (privateList && tokens.refresh.empty()) return {{}, "signed_out"};
        if (!itemId.empty() && !id(itemId)) return {{}, "invalid_id"};
        int offset=std::atoi(value("offset").c_str());
        if (offset<0 || offset>1024) return {{}, "invalid_offset"};
        int count=play ? 256 : 24;
        auto key=string(feed ? "feed|" : "nav|")+kind+"|"+itemId+"|"+tab+"|"+language;
        auto existing=lists.find(key);
        if (existing==lists.end() || (!offset && (value("fresh")=="1" || wall-existing->second.created>=300))) {
            if (lists.size()>=8) lists.erase(lists.begin());
            List list; list.created=wall; list.videosOnly=feed;
            string browseId;
            if (kind=="home") browseId="FEwhat_to_watch";
            else if (kind=="subscriptions") browseId="FEsubscriptions";
            else if (kind=="library") browseId="FEplaylist_aggregation";
            else if (kind=="history") browseId="FEhistory";
            else if (kind=="later") browseId="VLWL";
            else if (kind=="channel") { if (!channel(itemId)) return {{}, "invalid_channel"}; browseId=itemId; }
            else if (kind=="playlist") {
                if (itemId.compare(0,2,"RD")==0) {
                    list.route="next"; list.fields="\"playlistId\":"+J(itemId).serialize();
                } else browseId=itemId.compare(0,2,"VL")==0 ? itemId : "VL"+itemId;
            } else return {{}, "unsupported_kind"};
            if (!browseId.empty()) list.fields="\"browseId\":"+J(browseId).serialize();
            lists[key]=std::move(list);
        }
        auto& list=lists[key];
        while (list.more && list.items.size()<size_t(offset+count)) {
            auto e=fetch(list,kind,itemId,tab); if (!e.empty()) return {{},e};
        }
        auto end=std::min(list.items.size(),size_t(offset+count));
        bool more=end<list.items.size() || list.more;
        string body=feed ? "ok\t"+kind+"\t"+clean(list.title)+"\t"+(more?"1":"0")+"\n" :
            "ok\t"+clean(list.title)+"\t"+clean(list.subtitle)+"\t"+list.image+"\t"+(list.subscribed?"1":"0")+"\t"+(more?"1":"0")+"\t"+list.channelId+"\t"+list.banner+"\t"+list.historyVideo+"\t"+list.laterVideo+"\n";
        for (size_t i=size_t(offset); i<end; ++i) {
            const auto& v=list.items[i];
            if (feed) {
                if (v.type=="video") body+=v.id+"\t"+std::to_string(v.duration)+"\t"+clean(v.byline)+"\t\t"+clean(v.title)+"\t"+std::to_string(v.pos)+"\t"+membershipWire(v.members)+"\n";
            } else if (!play || v.type=="video") body+=v.type+"\t"+v.id+"\t"+clean(v.title)+"\t"+clean(v.byline)+"\t"+std::to_string(v.duration)+"\t"+itemImage(v)+"\t"+v.channelId+"\t"+clean(v.section)+"\t"+membershipWire(v.members)+"\n";
        }
        return {body,{}};
    }
    AccountReply next(const string& videoId) {
        J j; auto e=call("next","\"videoId\":"+J(videoId).serialize(),j); if (!e.empty()) return {{},e};
        string like=str(first(get(j,"transportControls"),"likeButtonRenderer"),"likeStatus");
        const auto& next=first(get(j,"playerOverlays"),"playerOverlayAutoplayRenderer");
        auto nextId=str(next,"videoId");
        if (!id(nextId,11) || nextId==videoId) nextId.clear();
        return {"ok\t"+like+"\t"+nextId+"\t"+clean(text(get(next,"byline")))+"\t"+clean(text(get(next,"videoTitle")))+"\n",{}};
    }
    AccountReply watched(const string& videoId, double pos, bool progress) {
        if (tokens.refresh.empty()) return {{}, "signed_out"};
        auto e=auth(); if (!e.empty()) return {{},e};
        if (!progress) tracking.erase(videoId);
        auto found=tracking.find(videoId);
        if (found==tracking.end()) {
            J j;
            e=call("player","\"videoId\":"+J(videoId).serialize()+",\"contentCheckOk\":true,\"racyCheckOk\":true",j,true);
            if (!e.empty()) return {{},e};
            const auto& t=get(j,"playbackTracking");
            Tracking entry{str(get(t,"videostatsPlaybackUrl"),"baseUrl"),str(get(t,"videostatsWatchtimeUrl"),"baseUrl"),{},false};
            if (!safeStats(entry.playback) || !safeStats(entry.watchtime)) return {{}, "tracking_unavailable"};
            entry.cpn=std::to_string(static_cast<unsigned long long>(monotonic))+"Tube360Watch";
            entry.cpn.resize(16,'0');
            if (tracking.size()>=16) tracking.erase(tracking.begin());
            tracking[videoId]=entry; found=tracking.find(videoId);
        }
        auto& t=found->second;
        auto send=[&](string url,std::map<string,string> values) {
            if (url.compare(0,18,"https://s.youtube.")==0) url.replace(0,10,"https://www.");
            values["ver"]="2"; values["c"]="tvhtml5"; values["cver"]="5.20260707";
            values["cbrver"]="5.20260707"; values["cpn"]=t.cpn;
            Request r; r.url=replaceQuery(url,values); r.agent=samsungAgent;
            r.headers={"Authorization: Bearer "+tokens.access};
            auto res=net.send(r);
            return res.error.empty() && res.status>=200 && res.status<300;
        };
        if (!t.added) {
            if (!send(t.playback,{{"fmt","251"},{"rtn","0"},{"rt","0"}})) return {{}, "history_failed"};
            t.added=true;
        }
        if (pos>0) {
            char time[32]; std::snprintf(time,sizeof(time),"%.3f",pos);
            if (!send(t.watchtime,{{"st",time},{"et",time},{"cmt",time},{"final","1"}})) return {{}, "progress_failed"};
        }
        for (auto it=lists.begin();it!=lists.end();) {
            if (it->first.compare(0,13,"feed|history|")==0 || it->first.compare(0,12,"nav|history|")==0) it=lists.erase(it);
            else ++it;
        }
        return {"ok\n",{}};
    }
};
ConsoleAccount::ConsoleAccount(Transport& t,string path):impl(new Impl(t,std::move(path))) {}
ConsoleAccount::~ConsoleAccount()=default;
bool ConsoleAccount::supports(const string& path) {
    auto q=path.find('?'); auto route=path.substr(0,q);
    if (route=="/api/account" || route=="/api/account/login" || route=="/api/account/logout" || route=="/api/account/watched" ||
        route=="/api/nav/action" || route=="/api/nav/channel" || route=="/api/nav/transport") return true;
    if (route.compare(0,11,"/api/video/")==0) return route.size()>=27 && (route.find("/next")!=string::npos || route.find("/rate")!=string::npos);
    if (route!="/api/nav" && route!="/api/feed" && route!="/api/nav/play") return false;
    auto p=params(q==string::npos ? "" : path.substr(q+1));
    auto k=p["kind"];
    return k=="home" || k=="subs" || k=="subscriptions" || k=="library" || k=="history" || k=="later" || k=="playlist" || k=="channel";
}
AccountReply ConsoleAccount::request(const string& path,int64_t wall,int64_t monotonic) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    auto& s=*impl;
    struct Publish {
        Impl& s;
        ~Publish() {
            std::lock_guard<std::mutex> lock(s.tokenMutex);
            auto token=s.wall<s.tokens.expires ? s.tokens.access : string{};
            if (token!=s.publishedToken) { s.publishedToken=std::move(token); s.tokenGeneration.fetch_add(1); }
        }
    } publish{s};
    s.wall=wall ? wall : std::time(nullptr);
    s.monotonic=monotonic>=0 ? monotonic : std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    s.load();
    auto q=path.find('?'); auto route=path.substr(0,q); auto p=params(q==string::npos ? "" : path.substr(q+1));
    auto language=p["lang"]=="pt" ? "pt" : "en";
    s.language=language;
    if (route=="/api/account" || route=="/api/account/login") return s.status(route=="/api/account/login");
    if (route=="/api/account/logout") {
        auto token=s.tokens.refresh;
        if (std::remove(s.path.c_str())!=0 && errno!=ENOENT) return {{}, "token_remove_failed"};
        std::remove((s.path+".tmp").c_str()); s.clear();
        if (!token.empty()) {
            Request r; r.url=origin+"/o/oauth2/revoke"; r.body="token="+enc(token); r.form=true; r.agent=tvAgent;
            s.net.send(r);
        }
        return {"ok\tsigned_out\t\t\t\n",{}};
    }
    if (route=="/api/nav/transport") return {"ok\n",{}};
    if (route=="/api/feed" || route=="/api/nav" || route=="/api/nav/play") return s.listing(p,route=="/api/feed",route=="/api/nav/play");
    if (route=="/api/nav/channel") {
        if (!id(p["id"],11)) return {{}, "invalid_video"};
        J j; auto e=s.call("player","\"videoId\":"+J(p["id"]).serialize(),j,true);
        if (!e.empty()) return {{},e};
        auto c=str(get(j,"videoDetails"),"channelId");
        if (!channel(c)) return {{}, "channel_unavailable"};
        p["kind"]="channel"; p["id"]=c;
        return s.listing(p,false);
    }
    if (route=="/api/nav/action") {
        if (s.tokens.refresh.empty()) return {{}, "signed_out"};
        J j; string e;
        if (p["action"]=="subscribe" && channel(p["id"])) {
            bool enabled=p["value"]!="0";
            e=s.call(enabled ? "subscription/subscribe" : "subscription/unsubscribe",
                "\"channelIds\":["+J(p["id"]).serialize()+"],\"params\":\""+(enabled ? "EgIIAhgA" : "CgIIAhgA")+"\"",j);
        } else if (p["action"]=="later" && id(p["id"],11))
            e=s.call("browse/edit_playlist","\"playlistId\":\"WL\",\"actions\":[{\"action\":\"ACTION_ADD_VIDEO\",\"addedVideoId\":"+J(p["id"]).serialize()+"}]",j);
        else return {{}, "invalid_action"};
        if (!e.empty()) return {{},e};
        s.lists.clear(); return {"ok\n",{}};
    }
    if (route=="/api/account/watched") {
        if (!id(p["id"],11)) return {{}, "invalid_video"};
        auto pos=std::strtod(p["pos"].c_str(),nullptr);
        if (!std::isfinite(pos) || pos<0 || pos>864000) return {{}, "invalid_position"};
        return s.watched(p["id"],pos,p["progress"]=="1");
    }
    if (route.compare(0,11,"/api/video/")==0) {
        auto videoId=route.substr(11,11);
        if (!id(videoId,11)) return {{}, "invalid_video"};
        if (route.substr(22)=="/next") return s.next(videoId);
        if (route.substr(22)=="/rate") {
            if (s.tokens.refresh.empty()) return {{}, "signed_out"};
            const std::map<string,string> routes={{"like","like/like"},{"dislike","like/dislike"},{"none","like/removelike"}};
            auto r=routes.find(p["r"]); if (r==routes.end()) return {{}, "invalid_rating"};
            J j; auto e=s.call(r->second,"\"target\":{\"videoId\":"+J(videoId).serialize()+"}",j);
            if (!e.empty()) return {{},e};
            return {"ok\t"+string(p["r"]=="like" ? "LIKE" : p["r"]=="dislike" ? "DISLIKE" : "INDIFFERENT")+"\n",{}};
        }
    }
    return {{}, "unsupported_route"};
}
string ConsoleAccount::pictureUrl(const string& key) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    auto found=impl->images.find(key);
    return found==impl->images.end() ? string{} : found->second;
}
string ConsoleAccount::accessToken() {
    std::lock_guard<std::mutex> lock(impl->tokenMutex);
    return impl->publishedToken;
}
uint64_t ConsoleAccount::tokenRevision() const { return impl->tokenGeneration.load(); }
}
