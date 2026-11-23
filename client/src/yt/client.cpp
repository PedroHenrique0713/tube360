#include "client.hpp"
#include "picojson.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <set>
#include <cstdio>
#include <ctime>

namespace yt {
namespace {
using J = picojson::value;
using Object = picojson::object;
using Array = picojson::array;
const J& field(const J& j, const char* name) {
    static const J none;
    return j.is<Object>() ? j.get(name) : none;
}
const Array& array(const J& j) {
    static const Array empty;
    return j.is<Array>() ? j.get<Array>() : empty;
}
string str(const J& j) { return j.is<string>() ? j.get<string>() : string{}; }
string str(const J& j, const char* name) { return str(field(j, name)); }
int integer(const J& j, const char* name) {
    const J& v = field(j, name);
    if (v.is<double>()) {
        double n = v.get<double>();
        return n >= 0 && n <= INT_MAX ? int(n) : 0;
    }
    auto s = str(v);
    if (s.empty() || s.size() > 10 || s.find_first_not_of("0123456789") != string::npos) return 0;
    auto n = std::strtoull(s.c_str(), nullptr, 10);
    return n <= INT_MAX ? int(n) : 0;
}
string label(const J& j) {
    auto s = str(j, "simpleText");
    if (s.empty()) for (auto& run : array(field(j, "runs"))) s += str(run, "text");
    return s;
}
// Limita a pilha do parser recursivo e o número de nós que o DOM pode alocar.
size_t objectEnd(const string& s, size_t begin) {
    bool quoted = false, escaped = false;
    int depth = 0, nodes = 0;
    if (begin >= s.size() || s[begin] != '{') return string::npos;
    for (size_t i = begin; i < s.size(); ++i) {
        char c = s[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 80 || ++nodes > 160000) break;
        } else if (c == ',') {
            if (++nodes > 160000) break;
        } else if ((c == '}' || c == ']') && --depth == 0) return i + 1;
    }
    return string::npos;
}
bool parse(const string& s, J& j) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (s.size() > 3 * 1024 * 1024 || b == string::npos) return false;
    auto end = objectEnd(s,b);
    if (end == string::npos || s.find_first_not_of(" \t\r\n",end) != string::npos) return false;
    string err;
    try {
        picojson::parse(j, s.begin()+b, s.begin()+end, &err);
    } catch (const std::exception&) { return false; }
    return err.empty() && j.is<Object>();
}
string content(const Response& r, J& j) {
    if (!r.error.empty()) return r.error;
    if (r.status != 200) return "HTTP " + std::to_string(r.status);
    return parse(r.body,j) ? "" : "invalid_json";
}
string member(const char* name, const string& value) {
    return J(string(name)).serialize()+":"+J(value).serialize();
}
bool httpsUrl(const string& u) {
    return u.size()>8 && u.compare(0,8,"https://")==0 &&
        std::none_of(u.begin(), u.end(), [](unsigned char c){return c <= 32 || c == 127 || c == '\\';});
}
void collect(const J& j, Page& out, std::set<string>& seen) {
    if (out.cards.size() >= 60) return;
    if (j.is<Array>()) {
        for (const auto& v : array(j)) collect(v,out,seen);
    } else if (j.is<Object>()) {
        for (const auto& kv : j.get<Object>()) {
            const J& v = kv.second;
            if (kv.first=="videoRenderer" || kv.first=="compactVideoRenderer" ||
                kv.first=="gridVideoRenderer" || kv.first=="videoWithContextRenderer" ||
                kv.first=="lockupViewModel" || kv.first=="endScreenVideoRenderer") {
                auto id = str(v,"videoId"), title = label(field(v,"title"));
                if (kv.first=="lockupViewModel") {
                    if (str(v,"contentType") != "LOCKUP_CONTENT_TYPE_VIDEO") continue;
                    id = str(v,"contentId");
                    title = str(field(field(field(v,"metadata"),"lockupMetadataViewModel"),"title"),"content");
                }
                if (!id.empty() && !title.empty() && seen.insert(id).second && out.cards.size()<60)
                    out.cards.push_back({id,title});
            } else collect(v,out,seen);
        }
    }
}
}

Response Client::post(const string& route, const string& fields) {
    if (context.empty()) {
        auto r = net.request("https://www.youtube.com/");
        if (!r.error.empty() || r.status!=200) return r;
        const string key = "\"INNERTUBE_CONTEXT\"";
        auto pos = r.body.find(key);
        if (pos==string::npos) return {0,{}, "web_context_missing"};
        auto colon = r.body.find_first_not_of(" \t\r\n",pos+key.size());
        auto begin = colon==string::npos ? string::npos : r.body.find_first_not_of(" \t\r\n",colon+1);
        auto end = objectEnd(r.body,begin);
        if (colon==string::npos || r.body[colon]!=':' || end==string::npos || end-begin>65536)
            return {0,{}, "web_context_invalid"};
        J j;
        auto value = r.body.substr(begin,end-begin);
        if (!parse(value,j) || str(field(j,"client"),"clientName")!="WEB")
            return {0,{}, "web_context_invalid"};
        context = std::move(value);
    }
    return net.request("https://www.youtube.com/youtubei/v1/"+route+"?prettyPrint=false",
        "{\"context\":"+context+","+fields+"}");
}
Page Client::page(const string& route, const string& fields, const string& exclude) {
    J j;
    Page out;
    out.error = content(post(route,fields),j);
    if (!out.error.empty()) return out;
    if (j.contains("error") || !j.contains("contents")) { out.error="invalid_page"; return out; }
    std::set<string> seen{exclude};
    collect(field(j,"contents"),out,seen);
    return out;
}
Page Client::home() { return page("browse",member("browseId","FEwhat_to_watch")); }
Page Client::search(const string& query) { return page("search",member("query",query)); }
Page Client::next(const string& id) { return page("next",member("videoId",id),id); }

namespace {
const char* const browserAgent="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36";
const char* const visionAgent="Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15";
// Valor de "chave":"valor" no HTML do YouTube, com as barras escapadas desfeitas.
string quoted(const string& html, const string& key) {
    auto pos=html.find(key);
    if (pos==string::npos) return {};
    pos+=key.size();
    auto end=html.find('"',pos);
    if (end==string::npos || end-pos>4096) return {};
    string v=html.substr(pos,end-pos);
    for (size_t i; (i=v.find("\\/"))!=string::npos;) v.erase(i,1);
    return v;
}
int digitsAfter(const string& s, const char* key) {
    auto pos=s.find(key);
    if (pos==string::npos) return 0;
    pos+=std::strlen(key);
    int n=0;
    while (pos<s.size() && s[pos]>='0' && s[pos]<='9' && n<100000000) n=n*10+(s[pos++]-'0');
    return n;
}
}
bool Client::session(const string& id, string& error) {
    if (!visitor.empty() && sts) return true;
    Request page;
    page.url="https://www.youtube.com/watch?v="+id+"&bpctr=9999999999&has_verified=1";
    page.agent=browserAgent;
    page.headers={"Accept-Language: en-us,en;q=0.5"};
    auto r=net.send(page);
    if (!r.error.empty() || r.status!=200) { error=!r.error.empty() ? r.error : "HTTP "+std::to_string(r.status); return false; }
    visitor=quoted(r.body,"\"VISITOR_DATA\":\"");
    auto js=quoted(r.body,"\"jsUrl\":\"");
    if (visitor.empty() || js.size()<2 || js[0]!='/' || js[1]=='/') { visitor.clear(); error="visitor_session_missing"; return false; }
    if (js!=playerJs || !sts) {
        // Uma revisão do player identifica o JS e o timestamp. Publica o índice por último.
        const auto& base = cacheBase;
        if (!cacheBase.empty()) {
            if (auto* f = std::fopen((base + ".idx").c_str(), "rb")) {
                char path[4098]{};
                int cached = 0;
                if (std::fscanf(f, "%4096s%d", path, &cached) == 2 && path == js && cached > 0) {
                    playerJs = js; sts = cached;
                }
                std::fclose(f);
                if (sts && playerJs == js) return true;
            }
        }
        Request code;
        code.url="https://www.youtube.com"+js;
        code.agent=browserAgent;
        auto b=net.send(code);
        int n=b.error.empty() && b.status==200 ? digitsAfter(b.body,"signatureTimestamp:") : 0;
        if (!n && b.error.empty()) n=digitsAfter(b.body,"sts:");
        if (!n) { visitor.clear(); error="signature_timestamp_missing"; return false; }
        sts=n; playerJs=js;
        if (!cacheBase.empty()) {
            if (auto* f = std::fopen((base + ".js.tmp").c_str(), "wb")) {
                bool ok = std::fwrite(b.body.data(), 1, b.body.size(), f) == b.body.size();
                if (std::fclose(f) == 0 && ok && std::rename((base + ".js.tmp").c_str(), (base + ".js").c_str()) == 0) {
                    if (auto* index = std::fopen((base + ".idx.tmp").c_str(), "w")) {
                        ok = std::fprintf(index, "%s\n%d\n", js.c_str(), n) > 0;
                        if (std::fclose(index) == 0 && ok) std::rename((base + ".idx.tmp").c_str(), (base + ".idx").c_str());
                    }
                }
            }
        }
    }
    return true;
}

Video Client::player(const string& id, int height, MediaOffers* offers) {
    if (offers) *offers = {};
    J j;
    Video out;
    if (id.size()!=11 || id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=string::npos) {
        out.status="INVALID_VIDEO_ID";
        return out;
    }
    // O cliente VISIONOS recebe links prontos (sem decifrar JavaScript) quando o pedido leva a sessão de visitante
    // da página do vídeo: cookies, X-Goog-Visitor-Id e o signatureTimestamp do player JS. É o mesmo pedido que o
    // yt-dlp do servidor faz (medido em 2026-11-23: itag 136 direto deste IP, sem login nem PO token).
    for (int attempt=0; attempt<2; ++attempt) {
        if (!session(id,out.reason)) break;
        Request q;
        q.url="https://www.youtube.com/youtubei/v1/player?prettyPrint=false";
        q.agent=visionAgent;
        q.headers={"X-Youtube-Client-Name: 101","X-Youtube-Client-Version: 1.02","Origin: https://www.youtube.com",
                   "X-Goog-Visitor-Id: "+visitor};
        q.body="{\"context\":{\"client\":{\"clientName\":\"VISIONOS\",\"clientVersion\":\"1.02\","
               "\"deviceMake\":\"Apple\",\"deviceModel\":\"RealityDevice17,1\",\"userAgent\":"+J(string(visionAgent)).serialize()+","
               "\"osName\":\"visionOS\",\"osVersion\":\"26.5.23O471\",\"hl\":\"en\",\"timeZone\":\"UTC\",\"utcOffsetMinutes\":0}},"
               +member("videoId",id)+",\"playbackContext\":{\"contentPlaybackContext\":{\"html5Preference\":\"HTML5_PREF_WANTS\","
               "\"signatureTimestamp\":"+std::to_string(sts)+"}},\"contentCheckOk\":true,\"racyCheckOk\":true}";
        j=J();
        out.reason=content(net.send(q),j);
        if (!out.reason.empty() || attempt || str(field(j,"playabilityStatus"),"status")!="LOGIN_REQUIRED") break;
        visitor.clear(); // sessão recusada: abre uma nova uma única vez
    }
    if (!out.reason.empty()) { out.status="ERROR"; return out; }
    const auto& p = field(j,"playabilityStatus");
    out.status = str(p,"status");
    out.reason = str(p,"reason");
    if (out.status.empty()) out.status="INVALID_RESPONSE";
    if (out.status!="OK") return out;
    const auto& details = field(j,"videoDetails");
    out.duration = integer(details, "lengthSeconds");
    auto& live = field(details, "isLive");
    out.live = live.is<bool>() && live.get<bool>();
    out.id = str(details,"videoId"); out.title = str(details,"title");
    if (out.id != id) { out.status="INVALID_VIDEO_ID"; return out; }
    const auto& sd=field(j,"streamingData");
    out.ttl=integer(sd,"expiresInSeconds");
    auto hls=str(sd,"hlsManifestUrl");
    if (httpsUrl(hls)) out.hls=std::move(hls);
    int audioRate=0;
    Format bestAdaptive, bestMuxed, fallback;
    auto better = [](const Format& candidate, const Format& best) {
        if (candidate.height != best.height) return candidate.height > best.height;
        if ((candidate.fps <= 30.1) != (best.fps <= 30.1)) return candidate.fps <= 30.1;
        return candidate.fps > best.fps;
    };
    for (auto key : {"formats","adaptiveFormats"}) {
        const bool muxed = std::strcmp(key,"formats")==0;
        for (const auto& f : array(field(sd,key))) {
            auto url=str(f,"url"), mime=str(f,"mimeType");
            if (!f.is<Object>() || !array(field(f,"drmFamilies")).empty() || f.contains("licenseInfos")) continue;
            bool direct = httpsUrl(url);
            const auto& fps = field(f, "fps");
            Format fmt{url,integer(f,"itag"),integer(f,"height"),fps.is<double>() ? fps.get<double>() : 0.0};
            if (fmt.height == 720 && fmt.fps > 30 && mime.find("avc1") != string::npos) out.highFrameRate720 = true;
            if (mime.find("audio/mp4")==0 && mime.find("mp4a")!=string::npos) {
                if (offers) {
                    ++offers->aac.listed;
                    if (direct) ++offers->aac.direct;
                }
                if (!direct) continue;
                int rate=integer(f,"bitrate");
                if (out.audio.url.empty() || (fmt.itag==140 && out.audio.itag!=140) || (out.audio.itag!=140 && rate>audioRate)) { out.audio=fmt; audioRate=rate; }
            } else if (mime.find("video/mp4")==0 && mime.find("avc1")!=string::npos &&
                fmt.height>0 && fmt.height<=1080 && fmt.fps>0 && fmt.fps<=60.1 &&
                (fmt.height<=720 || fmt.fps<=30.1) &&
                integer(f,"width")>0 && integer(f,"width")<=(fmt.height>720 ? 1920 : 1280)) {
                if (offers && (fmt.height==720 || fmt.height==480) && fmt.fps>=29.96) {
                    auto& count = fmt.height==720 ? offers->avc720 : offers->avc480;
                    ++count.listed;
                    if (direct) {
                        ++count.direct;
                        if (muxed && mime.find("mp4a")!=string::npos) ++count.muxedAac;
                    }
                }
                if (!direct || fmt.height>(height>=720 ? 1080 : height) ||
                    (muxed && mime.find("mp4a")==string::npos)) continue;
                if (fmt.height<=720 && better(fmt,fallback)) fallback=fmt;
                auto& best=muxed ? bestMuxed : bestAdaptive;
                if (better(fmt,best)) best=fmt;
            }
        }
    }
    out.muxed = !bestMuxed.url.empty() && (out.audio.url.empty() || bestMuxed.height>=bestAdaptive.height);
    out.video = out.muxed ? bestMuxed : bestAdaptive;
    if (out.video.height>720) out.fallbackVideo=fallback;
    if (out.muxed) out.audio={};
    if (out.hls.empty()) {
        if (out.video.url.empty()) out.reason="no_direct_avc";
        else if (!out.muxed && out.audio.url.empty()) out.reason="no_direct_aac";
    }
    const auto now = std::time(nullptr);
    for (const auto* url : {&out.video.url, &out.audio.url, &out.fallbackVideo.url, &out.hls}) {
        auto at = url->find("expire=");
        if (at == string::npos) at = url->find("/expire/");
        if (at == string::npos || now <= 0) continue;
        int64_t expires = std::strtoll(url->c_str()+at+(url->at(at)=='/' ? 8 : 7),nullptr,10);
        if (expires > 0) {
            out.expiresAt = out.expiresAt ? std::min(out.expiresAt,expires) : expires;
            int left = int(std::clamp<int64_t>(expires-now,0,INT_MAX));
            out.ttl = out.ttl ? std::min(out.ttl,left) : left;
        }
    }
    const auto& tracks=field(field(j,"captions"),"playerCaptionsTracklistRenderer");
    for (const auto& t : array(field(tracks,"captionTracks"))) {
        auto url=str(t,"baseUrl");
        if (httpsUrl(url)) out.captions.push_back({url,str(t,"languageCode"),label(field(t,"name"))});
    }
    return out;
}
bool Client::captions(const Track& t,std::vector<Cue>& cues,string& error) {
    cues.clear();
    const string base="https://www.youtube.com/api/timedtext?";
    if (!httpsUrl(t.url) || t.url.compare(0,base.size(),base)!=0) { error="invalid_track_url"; return false; }
    auto url=t.url;
    auto fmt=url.find("&fmt=");
    if (fmt==string::npos && url.compare(base.size(),4,"fmt=")==0) fmt=base.size()-1;
    if (fmt==string::npos) url+="&fmt=json3";
    else {
        auto b=fmt+5, e=url.find('&',b);
        url.replace(b,e==string::npos ? string::npos : e-b,"json3");
    }
    J j;
    error=content(net.request(url),j);
    if (!error.empty()) return false;
    if (!field(j,"events").is<Array>()) { error="invalid_captions"; return false; }
    for (const auto& event : array(field(j,"events"))) {
        string line;
        for (const auto& seg : array(field(event,"segs"))) line+=str(seg,"utf8");
        if (!line.empty()) cues.push_back({integer(event,"tStartMs"),integer(event,"dDurationMs"),line});
    }
    return true;
}

namespace {
string formValue(const string& s) {
    static const char hex[]="0123456789ABCDEF";
    string out;
    for (unsigned char c : s) {
        if ((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') ||
            c=='-' || c=='.' || c=='_' || c=='~') out+=char(c);
        else { out+='%'; out+=hex[c>>4]; out+=hex[c&15]; }
    }
    return out;
}
string oauth(Transport& net,const char* route,const string& body,J& out) {
    auto r=net.request(string("https://oauth2.googleapis.com/")+route,body,true);
    if (!r.error.empty()) return "temporarily_unavailable";
    if (r.status==429 || r.status>=500) return "temporarily_unavailable";
    if (!parse(r.body,out)) return "invalid_oauth_response";
    auto e=str(out,"error");
    if (e.empty()) e=str(out,"error_code");
    if (!e.empty()) return e;
    return r.status==200 ? "" : "HTTP "+std::to_string(r.status);
}
string newTokens(const J& j,int64_t now,Tokens& out,bool first) {
    auto access=str(j,"access_token"), refresh=str(j,"refresh_token");
    int ttl=integer(j,"expires_in");
    if (str(j,"token_type")!="Bearer" || access.empty() || !ttl || (first && refresh.empty()))
        return "invalid_token_response";
    out.access=std::move(access);
    out.expires=now+ttl;
    if (!refresh.empty()) out.refresh=std::move(refresh);
    return {};
}
}
string DeviceLogin::start(int64_t now, DeviceCode& code) {
    code={};
    if (clientId.empty() || secret.empty()) return "missing_client_credentials";
    J j;
    auto e=oauth(net,"device/code","client_id="+formValue(clientId)+
        "&scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fyoutube.readonly",j);
    if (!e.empty()) return e;
    DeviceCode c;
    c.device=str(j,"device_code"); c.user=str(j,"user_code"); c.verificationUrl=str(j,"verification_url");
    int ttl=integer(j,"expires_in");
    if (c.device.empty() || c.user.empty() || !httpsUrl(c.verificationUrl) || !ttl) return "invalid_device_code";
    c.interval=std::max(5,integer(j,"interval")); c.expires=now+ttl; c.nextPoll=now+c.interval;
    code=std::move(c);
    return {};
}
string DeviceLogin::poll(int64_t now, DeviceCode& code, Tokens& tokens) {
    if (code.device.empty() || now>=code.expires) { code={}; return "expired_token"; }
    if (now<code.nextPoll) return "authorization_pending";
    J j;
    code.nextPoll=now+code.interval;
    auto e=oauth(net,"token","client_id="+formValue(clientId)+"&client_secret="+formValue(secret)+
        "&device_code="+formValue(code.device)+"&grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code",j);
    if (e=="slow_down" || e=="temporarily_unavailable") {
        code.interval=std::min(INT_MAX-5,code.interval)+5;
        code.nextPoll=now+code.interval;
    } else if (e.empty()) {
        code={};
        return newTokens(j,now,tokens,true);
    } else if (e!="authorization_pending") code={};
    return e;
}
string DeviceLogin::refresh(int64_t now, Tokens& tokens) {
    if (tokens.refresh.empty()) return "missing_refresh_token";
    J j;
    auto e=oauth(net,"token","client_id="+formValue(clientId)+"&client_secret="+formValue(secret)+
        "&refresh_token="+formValue(tokens.refresh)+"&grant_type=refresh_token",j);
    if (e=="invalid_grant") tokens={};
    if (!e.empty()) return e;
    return newTokens(j,now,tokens,false);
}
}
