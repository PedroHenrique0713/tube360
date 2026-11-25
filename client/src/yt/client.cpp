#include "client.hpp"
#include "membership_json.hpp"
#include "locale.hpp"
#include "update/rules.hpp"
#include "picojson.h"
#include <algorithm>
#include <cctype>
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
    const auto limits=updates::rules();
    const int maxDepth=limits->n("parserDepth"), maxNodes=limits->n("parserNodes");
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
            if (++depth > maxDepth || ++nodes > maxNodes) break;
        } else if (c == ',') {
            if (++nodes > maxNodes) break;
        } else if ((c == '}' || c == ']') && --depth == 0) return i + 1;
    }
    return string::npos;
}
bool parse(const string& s, J& j) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (s.size() > size_t(updates::rules()->n("jsonBytes")) || b == string::npos) return false;
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
    return updates::youtubeUrl(u);
}
void collect(const J& j, Page& out, std::set<string>& seen, size_t limit) {
    if (out.cards.size() >= limit) return;
    if (j.is<Array>()) {
        for (const auto& v : array(j)) collect(v,out,seen,limit);
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
                if (!id.empty() && !title.empty() && seen.insert(id).second && out.cards.size()<limit)
                    out.cards.push_back({id,title,membership(v)});
            } else collect(v,out,seen,limit);
        }
    }
}
}

void Client::setLanguage(const string& value) { language = localeLanguage(value); }
void Client::bindRules() {
    auto next=updates::rules();
    if (!policy || next->version!=policy->version) {
        context.clear(); visitor.clear(); playerJs.clear(); sts=0; policy=std::move(next);
    }
}
Response Client::post(const string& route, const string& fields) {
    if (context.empty()) {
        auto r = net.send(Request{policy->s("webOrigin")+"/",{},policy->s("browserAgent")});
        if (!r.error.empty() || r.status!=200) return r;
        const string key = policy->s("contextMarker");
        auto pos = r.body.find(key);
        if (pos==string::npos) return {0,{}, "web_context_missing"};
        auto colon = r.body.find_first_not_of(" \t\r\n",pos+key.size());
        auto begin = colon==string::npos ? string::npos : r.body.find_first_not_of(" \t\r\n",colon+1);
        auto end = objectEnd(r.body,begin);
        if (colon==string::npos || r.body[colon]!=':' || end==string::npos || end-begin>65536)
            return {0,{}, "web_context_invalid"};
        J j;
        auto value = r.body.substr(begin,end-begin);
        if (!parse(value,j) || str(field(j,"client"),"clientName")!=policy->s("webName"))
            return {0,{}, "web_context_invalid"};
        context = std::move(value);
    }
    J localized; parse(context,localized);
    auto& client = localized.get<Object>()["client"].get<Object>();
    client["hl"] = J(language); client["gl"] = J(string(localeRegion(language)));
    return net.request(policy->api(route),
        "{\"context\":"+localized.serialize()+","+fields+"}");
}
Page Client::page(const string& route, const string& fields, const string& exclude) {
    bindRules();
    J j;
    Page out;
    auto response=post(route,fields);
    out.error = content(response,j);
    out.failure = failureOf(response);
    if (!out.error.empty()) { updates::observe(policy->version,false,out.error=="invalid_json" || out.error=="web_context_missing" || out.error=="web_context_invalid" || out.error=="HTTP 400" || out.error=="HTTP 403"); return out; }
    if (j.contains("error") || !j.contains("contents")) { out.error="invalid_page"; out.failure=Failure::JSON; updates::observe(policy->version,false,true); return out; }
    std::set<string> seen{exclude};
    collect(field(j,"contents"),out,seen,size_t(policy->n("cards")));
    updates::observe(policy->version,true,false);
    return out;
}
Page Client::home() { return page("browse",member("browseId",updates::rules()->s("homeBrowseId"))); }
Page Client::search(const string& query) { return page("search",member("query",query)); }
Page Client::next(const string& id) { return page("next",member("videoId",id),id); }

namespace {
// Valor de "chave":"valor" no HTML do YouTube, com as barras escapadas desfeitas.
string quoted(const string& html, const string& key) {
    auto pos=html.find(key);
    if (pos==string::npos) return {};
    pos+=key.size();
    auto end=html.find('"',pos);
    if (end==string::npos || end-pos>size_t(updates::rules()->n("markerBytes"))) return {};
    string v=html.substr(pos,end-pos);
    for (size_t i; (i=v.find("\\/"))!=string::npos;) v.erase(i,1);
    return v;
}
int digitsAfter(const string& s, const char* key) {
    auto pos=s.find(key);
    if (pos==string::npos) return 0;
    pos+=std::strlen(key);
    int64_t n=0;int max=updates::rules()->n("timestampMax");
    while(pos<s.size() && s[pos]>='0' && s[pos]<='9') {
        n=n*10+(s[pos++]-'0');if(n>max)return 0;
    }
    return int(n);
}
}
bool Client::visitorFallback(string& js) {
    Request sw;
    sw.url=policy->s("webOrigin")+policy->s("swPath"); sw.agent=policy->s("browserAgent");
    sw.headers={"Accept: */*","Accept-Language: "+policy->s("acceptLanguage"),"Referer: "+policy->s("webOrigin")+policy->s("swReferer")};
    auto r=net.send(sw);
    if (!r.error.empty() || r.status!=200 || r.body.compare(0,4,")]}'")!=0) { std::fprintf(stderr,"yt: sw.js_data status %ld erro %s inicio %.12s\n",(long)r.status,r.error.c_str(),r.body.c_str()); return false; }
    J data;
    if (!picojson::parse(data,"{\"d\":"+r.body.substr(4)+"}").empty()) return false;
    auto at=[](const J& v,size_t i)->const J&{ static const J nil; if(!v.is<picojson::array>()) return nil; const auto& a=v.get<picojson::array>(); return i<a.size()?a[i]:nil; };
    const auto& device=at(at(at(at(data.get("d"),0),2),0),0);
    const auto& v=at(device,13);
    if (!v.is<string>() || !updates::ascii(v.get<string>(),size_t(policy->n("markerBytes")))) { std::fprintf(stderr,"yt: sw.js_data sem visitante (%s)\n",device.serialize().substr(0,120).c_str()); return false; }
    visitor=v.get<string>();
    // O player JS: primeiro o índice guardado (qualquer versão das regras), senão a versão anunciada no iframe_api.
    for (const string& idx : {cacheBase+".v"+std::to_string(policy->version)+".idx",cacheBase+".v1.idx",cacheBase+".idx"}) {
        if (cacheBase.empty()) break;
        if (auto* f=std::fopen(idx.c_str(),"rb")) {
            char path[4098]{}; int cached=0;
            if (std::fscanf(f,"%4096s%d",path,&cached)==2 && path[0]=='/' && cached>0) { js=path; std::fclose(f); return true; }
            std::fclose(f);
        }
    }
    Request api; api.url=policy->s("webOrigin")+"/iframe_api"; api.agent=policy->s("browserAgent");
    auto a=net.send(api);
    // O iframe_api traz a versão com barras escapadas: s\/player\/<versão>\/.
    const string mark="s\\/player\\/";
    auto at0=a.status==200 ? a.body.find(mark) : string::npos;
    if (at0==string::npos) { visitor.clear(); return false; }
    auto from=at0+mark.size(), end=from;
    while (end<a.body.size() && end-from<16 && std::isalnum((unsigned char)a.body[end])) ++end;
    if (end==from) { visitor.clear(); return false; }
    js="/s/player/"+a.body.substr(from,end-from)+"/player_ias.vflset/en_US/base.js";
    return true;
}
bool Client::session(const string& id, string& error, bool fresh) {
    sessionFailure=Failure::NONE; sessionRetryable=false;
    if (!visitor.empty() && sts) return true;
    Request page;
    page.url=policy->s("webOrigin")+policy->s("watchPath")+id+policy->s("watchSuffix");
    page.agent=policy->s("browserAgent"); page.freshConnection=fresh;
    page.headers={"Accept-Language: "+policy->s("acceptLanguage")};
    auto r=net.send(page);
    string js;
    if (r.error.empty() && (r.status==429 || (r.status/100==3 && r.location.find("/sorry")!=string::npos))) {
        // A página HTML caiu na checagem de tráfego incomum do Google. A sessão de visitante sai do sw.js_data
        // (rota da API, sem essa checagem) e o player JS do índice guardado ou da versão anunciada no iframe_api.
        if (!visitorFallback(js)) {
            error="HTTP "+std::to_string(r.status)+" (checagem do Google)";
            sessionFailure=failureOf(r); sessionRetryable=retryable(r); return false;
        }
        std::fprintf(stderr,"yt: pagina do video na checagem do Google; sessao pelo sw.js_data\n");
    } else if (!r.error.empty() || r.status!=200) {
        error=!r.error.empty() ? r.error : "HTTP "+std::to_string(r.status);
        sessionFailure=failureOf(r); sessionRetryable=retryable(r); return false;
    } else {
        visitor=quoted(r.body,policy->s("visitorMarker"));
        js=quoted(r.body,policy->s("jsMarker"));
    }
    if (!updates::ascii(visitor,size_t(policy->n("markerBytes"))) || js.size()<2 || js[0]!='/' || js[1]=='/') {
        visitor.clear(); error="visitor_session_missing"; sessionFailure=Failure::SESSION; sessionRetryable=true; return false;
    }
    if (js!=playerJs || !sts) {
        // Uma revisão do player identifica o JS e o timestamp. Publica o índice por último.
        const auto base = cacheBase+".v"+std::to_string(policy->version);
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
        code.url=policy->s("webOrigin")+js;
        code.agent=policy->s("browserAgent");
        auto b=net.send(code);
        int n=0;
        if (b.error.empty() && b.status==200) for (const auto& marker:policy->timestampMarkers) { n=digitsAfter(b.body,marker.c_str()); if(n)break; }
        if (!n) {
            visitor.clear(); error=!b.error.empty() ? b.error : b.status!=200 ? "HTTP "+std::to_string(b.status) : "signature_timestamp_missing";
            sessionFailure=b.error.empty() && b.status==200 ? Failure::SESSION : failureOf(b);
            sessionRetryable=sessionFailure==Failure::SESSION || retryable(b); return false;
        }
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

Video Client::player(const string& id, int height, MediaOffers* offers, int mode, const string& wantedAudio, const string& accountPlayer) {
    bindRules();
    if (offers) *offers = {};
    J j;
    Video out;
    if (id.size()!=11 || id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=string::npos) {
        out.status="INVALID_VIDEO_ID";
        return out;
    }
    struct Report {
        Video& v; std::shared_ptr<const updates::Rules> policy;
        ~Report() {
            bool ok=v.status=="OK" && (!v.hls.empty() || (!v.video.url.empty() && (v.muxed || !v.audio.url.empty())));
            bool bad=v.failure==Failure::JSON || v.failure==Failure::SESSION || v.reason=="HTTP 400" || v.reason=="HTTP 403" ||
                v.reason=="no_direct_avc" || v.reason=="no_direct_aac" ||
                (v.failure==Failure::REFUSAL && std::find(policy->fallbackStatuses.begin(),policy->fallbackStatuses.end(),v.status)!=policy->fallbackStatuses.end());
            updates::observe(policy->version,ok,bad);
        }
    } report{out,policy};
    // O cliente VISIONOS recebe links prontos (sem decifrar JavaScript) quando o pedido leva a sessão de visitante
    // da página do vídeo: cookies, X-Goog-Visitor-Id e o signatureTimestamp do player JS. É o mesmo pedido que o
    // yt-dlp do servidor faz (medido em 2026-11-23: itag 136 direto deste IP, sem login nem PO token).
    if (!accountPlayer.empty()) {
        out.playerClient="TVHTML5"; out.playerAttempts=1;
        if (!parse(accountPlayer,j)) { out.status="ERROR"; out.failure=Failure::JSON; out.reason="invalid_json"; return out; }
        out.status=str(field(j,"playabilityStatus"),"status");
        out.reason=str(field(j,"playabilityStatus"),"reason");
        if (out.status!="OK") {
            if (membersReason(field(j,"playabilityStatus").serialize())) out.reason="members";
            out.failure=Failure::REFUSAL; return out;
        }
    }
    for (size_t client=0; accountPlayer.empty() && client<policy->players.size(); ++client) {
        const auto& identity=policy->players[client];
        out.playerClient=identity.name;
        for (int attempt=0; attempt<policy->n("retries"); ++attempt) {
            if (!session(id,out.reason,attempt>0)) {
                out.failure=sessionFailure;
                out.status="ERROR";
                if (attempt+1>=policy->n("retries") || !sessionRetryable || !net.backoff(unsigned(policy->n("backoffMs")))) return out;
                continue;
            }
            Request q;
            q.url=policy->api("player");
            q.agent=identity.agent; q.freshConnection=attempt>0;
            q.headers={policy->s("clientNameHeader")+": "+identity.code,policy->s("clientVersionHeader")+": "+identity.version,
                       "Origin: "+policy->s("webOrigin"),policy->s("visitorHeader")+": "+visitor};
            q.headers.insert(q.headers.end(),identity.headers.begin(),identity.headers.end());
            auto device=identity.device.serialize(); device=device.substr(1,device.size()-2);
            q.body="{\"context\":{\"client\":{"+member("clientName",identity.name)+","+member("clientVersion",identity.version)+","+
                (device.empty()?"":device+",")+member("userAgent",identity.agent)+","+member("hl",language)+","+member("gl",localeRegion(language))+",\"timeZone\":\"UTC\",\"utcOffsetMinutes\":0}},"+
                member("videoId",id)+",\"playbackContext\":{\"contentPlaybackContext\":{\"html5Preference\":\"HTML5_PREF_WANTS\",\"signatureTimestamp\":"+
                std::to_string(sts)+"}},\"contentCheckOk\":true,\"racyCheckOk\":true}";
            j=J(); ++out.playerAttempts;
            auto reply=net.send(q);
            out.reason=content(reply,j);
            auto kind=out.reason=="invalid_json" ? Failure::JSON : failureOf(reply);
            out.playerTrace+=identity.name+":"+failureName(kind)+"/"+std::to_string(reply.status)+" ";
            if (!out.reason.empty()) {
                out.status="ERROR"; out.failure=out.reason=="invalid_json" ? Failure::JSON : failureOf(reply);
                if (attempt+1<policy->n("retries") && (out.failure==Failure::JSON || retryable(reply)) && net.backoff(unsigned(policy->n("backoffMs")))) continue;
                return out;
            }
            out.status=str(field(j,"playabilityStatus"),"status");
            out.reason=str(field(j,"playabilityStatus"),"reason");
            if (out.status != "OK" && membersReason(field(j,"playabilityStatus").serialize())) {
                out.reason="members"; out.failure=Failure::REFUSAL; return out;
            }
            out.playerTrace+=out.status+" ";
            if (out.status.empty()) { out.status="INVALID_RESPONSE"; out.failure=Failure::JSON; return out; }
            if (out.status=="OK") { out.failure=Failure::NONE; break; }
            out.failure=Failure::REFUSAL;
            if (client==0 && !attempt && out.status=="LOGIN_REQUIRED") {
                visitor.clear();
                if (!net.backoff(unsigned(policy->n("backoffMs")))) return out;
                continue;
            }
            break;
        }
        if (out.status=="OK") break;
        // Alterna só após recusa do cliente. Falha de rede não justifica multiplicar pedidos InnerTube.
        if (out.failure!=Failure::REFUSAL || std::find(policy->fallbackStatuses.begin(),policy->fallbackStatuses.end(),out.status)==policy->fallbackStatuses.end()) return out;
    }
    if (out.status!="OK") return out;
    const auto& details = field(j,"videoDetails");
    out.duration = integer(details, "lengthSeconds");
    auto& live = field(details, "isLive");
    out.live = live.is<bool>() && live.get<bool>();
    out.id = str(details,"videoId"); out.title = str(details,"title");
    out.channel = str(details,"author"); out.channelId = str(details,"channelId");
    const auto& micro = field(field(j,"microformat"),"playerMicroformatRenderer");
    out.date = str(micro,"publishDate");
    if (out.date.empty()) out.date = str(micro,"uploadDate");
    if (out.id != id) { out.status="INVALID_VIDEO_ID"; return out; }
    const auto& sd=field(j,"streamingData");
    out.ttl=integer(sd,"expiresInSeconds");
    auto hls=str(sd,"hlsManifestUrl");
    if (httpsUrl(hls)) out.hls=std::move(hls);
    int audioRate=0, audioRank=-1;
    Format bestAdaptive, bestMuxed, fallback;
    std::vector<Format> adaptive;
    auto better = [mode](const Format& candidate, const Format& best) {
        if (candidate.height != best.height) return candidate.height > best.height;
        if (mode != 1 && (candidate.fps <= 30.1) != (best.fps <= 30.1)) return candidate.fps <= 30.1;
        return candidate.fps > best.fps;
    };
    for (auto key : {"formats","adaptiveFormats"}) {
        const bool muxed = std::strcmp(key,"formats")==0;
        for (const auto& f : array(field(sd,key))) {
            auto url=str(f,"url"), mime=str(f,"mimeType");
            if (!f.is<Object>() || !array(field(f,"drmFamilies")).empty() || f.contains("licenseInfos")) continue;
            bool direct = httpsUrl(url);
            const auto& fps = field(f, "fps");
            Format fmt{url,integer(f,"itag"),integer(f,"height"),fps.is<double>() ? fps.get<double>() : 0.0,integer(f,"bitrate")};
            if (fmt.height == 720 && fmt.fps > 30 && mime.find("avc1") != string::npos) out.highFrameRate720 = true;
            if (mime.find("audio/mp4")==0 && mime.find("mp4a")!=string::npos) {
                if (offers) {
                    ++offers->aac.listed;
                    if (direct) ++offers->aac.direct;
                }
                if (!direct) continue;
                int rate=integer(f,"bitrate");
                // Vídeo com dublagem automática traz uma faixa 140 por idioma, todas com o mesmo bitrate: sem isto saía
                // uma dublagem qualquer. Vale a original; sem marca, a padrão; depois a 140 e o bitrate.
                const auto& track=field(f,"audioTrack");
                const bool original=str(track,"displayName").find("original")!=string::npos;
                const auto& isDefault=field(track,"audioIsDefault");
                int rank=(original ? 4 : 0) + (isDefault.is<bool>() && isDefault.get<bool>() ? 2 : 0) + (fmt.itag==140 ? 1 : 0);
                string name=str(track,"displayName"), language=str(track,"id");
                language=language.substr(0,language.find('.'));
                string lower=name; std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){ return char(std::tolower(c)); });
                bool automatic=lower.find("auto")!=string::npos;
                bool isOriginal=original || lower.find("original")!=string::npos;
                rank=(isOriginal ? 4 : 0) + (isDefault.is<bool>() && isDefault.get<bool>() ? 2 : 0) + (fmt.itag==140 ? 1 : 0);
                string code=audioCode(language,isOriginal,automatic);
                auto existing=std::find_if(out.audioTracks.begin(),out.audioTracks.end(),[&](const AudioTrack& t){ return t.code==code; });
                if (existing==out.audioTracks.end()) out.audioTracks.push_back({code,language,name,isOriginal,automatic,isDefault.is<bool>() && isDefault.get<bool>(),fmt});
                else if (fmt.itag==140 && existing->format.itag!=140) existing->format=fmt;
                if (out.audio.url.empty() || rank>audioRank || (rank==audioRank && rate>audioRate)) { out.audio=fmt; audioRate=rate; audioRank=rank; }
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
                if (!direct || fmt.height>(height>=720 ? (mode==1 ? 720 : 1080) : height) ||
                    (muxed && (fmt.height>720 || mime.find("mp4a")==string::npos))) continue;
                if (fmt.height<=720 && better(fmt,fallback)) fallback=fmt;
                if (!muxed) adaptive.push_back(fmt);
                auto& best=muxed ? bestMuxed : bestAdaptive;
                if (better(fmt,best)) best=fmt;
            }
        }
    }
    auto chosen=std::find_if(out.audioTracks.begin(),out.audioTracks.end(),[&](const AudioTrack& t){ return t.code==wantedAudio; });
    if (chosen==out.audioTracks.end() && wantedAudio!="original") {
        bool language=wantedAudio.rfind("lang:",0)==0;
        size_t split=wantedAudio.rfind('-');
        string wanted=language ? wantedAudio.substr(5) : wantedAudio.substr(0,split);
        chosen=std::find_if(out.audioTracks.begin(),out.audioTracks.end(),[&](const AudioTrack& t){
            return t.language.substr(0,t.language.find('-'))==wanted.substr(0,wanted.find('-')) &&
                   (language || (split!=string::npos && t.code.substr(t.code.rfind('-'))==wantedAudio.substr(split)));
        });
    }
    if (chosen!=out.audioTracks.end()) out.audio=chosen->format;
    for (const auto& track : out.audioTracks) if (track.format.url==out.audio.url) out.selectedAudio=track.code;
    out.muxed = !bestMuxed.url.empty() && (out.audio.url.empty() || !better(bestAdaptive,bestMuxed));
    out.video = out.muxed ? bestMuxed : bestAdaptive;
    if (out.audioTracks.size()>1 && !out.audio.url.empty()) {
        out.muxed=false;
        out.video=bestAdaptive.url.empty() ? bestMuxed : bestAdaptive;
    }
    if (!out.muxed) {
        // Uma fonte distinta reduz os bytes de rede. Filtrar quadros do mesmo 298 não reduz sua vazão.
        for (const auto& fmt : adaptive) if (fmt.url!=out.video.url &&
            (fmt.height<out.video.height || (fmt.height==out.video.height && fmt.fps<=30.1 && out.video.fps>30.1))) out.lowerVideo.push_back(fmt);
        std::sort(out.lowerVideo.begin(),out.lowerVideo.end(),[](const Format& a,const Format& b) {
            return a.height!=b.height ? a.height>b.height : a.fps!=b.fps ? a.fps<b.fps : a.bitrate<b.bitrate;
        });
        out.lowerVideo.erase(std::unique(out.lowerVideo.begin(),out.lowerVideo.end(),[](const Format& a,const Format& b) {
            return a.height==b.height;
        }),out.lowerVideo.end());
        if (!out.lowerVideo.empty()) out.fallbackVideo=out.lowerVideo.front();
        else if (out.video.height>720) { out.fallbackVideo=fallback; if (!fallback.url.empty()) out.lowerVideo.push_back(fallback); }
    }
    if(!out.muxed) {
        out.videoLadder.push_back(out.video);
        out.videoLadder.insert(out.videoLadder.end(),out.lowerVideo.begin(),out.lowerVideo.end());
    }
    out.speakerAudio = out.audio;
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
    const auto p=updates::rules();
    const string base=p->s("webOrigin")+p->s("captionsPath");
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
    if (!error.empty()) {updates::observe(p->version,false,error=="invalid_json" || error=="HTTP 400" || error=="HTTP 403");return false;}
    if (!field(j,"events").is<Array>()) {error="invalid_captions";updates::observe(p->version,false,true);return false;}
    for (const auto& event : array(field(j,"events"))) {
        string line;
        for (const auto& seg : array(field(event,"segs"))) line+=str(seg,"utf8");
        if (!line.empty()) cues.push_back({integer(event,"tStartMs"),integer(event,"dDurationMs"),line});
    }
    updates::observe(p->version,true,false);
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
    auto r=net.request(updates::rules()->s("oauthOrigin")+"/"+route,body,true);
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
    auto e=oauth(net,updates::rules()->s("deviceRoute").c_str(),"client_id="+formValue(clientId)+
        "&scope="+formValue(updates::rules()->s("publicOAuthScope")),j);
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
    auto e=oauth(net,updates::rules()->s("tokenRoute").c_str(),"client_id="+formValue(clientId)+"&client_secret="+formValue(secret)+
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
    auto e=oauth(net,updates::rules()->s("tokenRoute").c_str(),"client_id="+formValue(clientId)+"&client_secret="+formValue(secret)+
        "&refresh_token="+formValue(tokens.refresh)+"&grant_type=refresh_token",j);
    if (e=="invalid_grant") tokens={};
    if (!e.empty()) return e;
    return newTokens(j,now,tokens,false);
}
}
