#pragma once
#include "locale.hpp"
#include "client.hpp"
#include "search.hpp"
#include "membership_json.hpp"
#include "update/rules.hpp"
#ifndef PICOJSON_USE_LOCALE
#define PICOJSON_USE_LOCALE 0
#endif
#include "picojson.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <functional>
#include <set>

namespace yt::catalog {
using J = picojson::value;
using Object = picojson::object;
using Array = picojson::array;
inline const J& get(const J& v, const string& key) {
  static const J nil;
  return v.is<Object>() ? v.get(key) : nil;
}
inline const Array& array(const J& v) {
  static const Array empty;
  return v.is<Array>() ? v.get<Array>() : empty;
}
inline string str(const J& v) { return v.is<string>() ? v.get<string>() : string{}; }
inline string str(const J& v, const string& k) { return str(get(v,k)); }
inline bool flag(const J& v, const string& k) { const auto& f = get(v,k); return f.is<bool>() && f.get<bool>(); }
inline string text(const J& v) {
  if (v.is<string>()) return str(v);
  auto s = str(v,"simpleText");
  if (s.empty()) s = str(v,"content");
  if (s.empty()) for (const auto& r : array(get(v,"runs"))) s += str(r,"text");
  return s;
}
inline bool identifier(const string& s, size_t limit = 256) {
  return !s.empty() && s.size() <= limit && s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == string::npos;
}
inline bool videoId(const string& s) { return s.size() == 11 && identifier(s); }
inline bool channelId(const string& s) { return s.size() == 24 && s.compare(0,2,"UC") == 0 && identifier(s); }
inline string clean(string s) {
  for (char& c : s) if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
  return s;
}
// O DOM remoto tem teto de bytes, profundidade e nós antes de entrar no picojson.
inline size_t jsonEnd(const string& s, size_t start) {
  const auto limits=updates::rules();
  const int maxDepth=limits->n("parserDepth"), maxNodes=limits->n("parserNodes");
  if (start >= s.size() || s[start] != '{') return string::npos;
  int depth = 0, nodes = 0; bool quoted = false, escaped = false;
  for (size_t i = start; i < s.size(); ++i) {
    char c = s[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
    } else if (c == '"') quoted = true;
    else if (c == '{' || c == '[') { if (++depth > maxDepth || ++nodes > maxNodes) return string::npos; }
    else if (c == ',') { if (++nodes > maxNodes) return string::npos; }
    else if ((c == '}' || c == ']') && --depth == 0) return i + 1;
  }
  return string::npos;
}
inline bool parse(const string& s, J& out) {
  if (s.size() > size_t(updates::rules()->n("jsonBytes"))) return false;
  size_t start = s.find_first_not_of(" \r\n\t"), end = jsonEnd(s,start);
  if (end == string::npos || s.find_first_not_of(" \r\n\t",end) != string::npos) return false;
  string error;
  try { picojson::parse(out,s.begin()+start,s.begin()+end,&error); }
  catch (const std::exception&) { return false; }
  return error.empty() && out.is<Object>();
}
inline const J* find(const J& v, const string& key) {
  if (v.is<Object>()) {
    if (v.contains(key)) return &v.get(key);
    for (const auto& kv : v.get<Object>()) if (const J* found = find(kv.second,key)) return found;
  } else for (const auto& x : array(v)) if (const J* found = find(x,key)) return found;
  return nullptr;
}
inline const J* findValue(const J& v, const string& key, const string& value) {
  if (v.is<Object>()) {
    if (str(v,key) == value) return &v;
    for (const auto& kv : v.get<Object>()) if (const J* found = findValue(kv.second,key,value)) return found;
  } else for (const auto& x : array(v)) if (const J* found = findValue(x,key,value)) return found;
  return nullptr;
}
inline string imageUrl(string u) {
  if (u.compare(0,2,"//")==0) u="https:"+u;
  if (u.compare(0,8,"https://") != 0 || u.size() > 4096 || u.find_first_of("@\\ \r\n\t#") != string::npos) return {};
  const auto slash = u.find('/',8);
  const auto host = u.substr(8,slash-8);
  if (host=="music.youtube.com" && slash!=string::npos && u.compare(slash,14,"/image/mixart?")==0) return u;
  for (const auto* domain : {"ytimg.com","ggpht.com","googleusercontent.com"}) {
    string d = domain;
    if (host == d || (host.size() > d.size() && host.compare(host.size()-d.size()-1,d.size()+1,"."+d) == 0)) return u;
  }
  return {};
}
inline void thumbnailCandidates(const J& v, string& best, double& score) {
  if (v.is<Object>()) {
    auto url=imageUrl(str(v,"url"));
    const auto& w=get(v,"width");
    double width=w.is<double>()?w.get<double>():0;
    if(!url.empty() && width<=1280 && width>score){best=url;score=width;}
    for(const auto& kv:v.get<Object>())thumbnailCandidates(kv.second,best,score);
  } else for(const auto& p:array(v))thumbnailCandidates(p,best,score);
}
inline string thumbnail(const J& v) {
  string best; double score=-1;
  thumbnailCandidates(v,best,score);
  return best;
}
// stb_image só suporta JPEG/PNG neste app. Uma capa ytimg tem alternativa JPEG estável.
inline string jpegThumbnail(const string& address) {
  auto u=imageUrl(address); if(u.empty())return {};
  auto start=u.find("/vi_webp/");size_t skip=9;
  if(start==string::npos){start=u.find("/vi/");skip=4;}
  if(start==string::npos)return {};
  auto id=u.substr(start+skip,11);
  if(!videoId(id) || u.size()<=start+skip+11 || u[start+skip+11]!='/')return {};
  auto host=u.substr(8,u.find('/',8)-8);
  if(host!="i.ytimg.com" && host!="i1.ytimg.com" && host!="i2.ytimg.com" && host!="i3.ytimg.com" && host!="i4.ytimg.com")return {};
  return "https://i.ytimg.com/vi/"+id+"/mqdefault.jpg";
}
inline int seconds(const string& s) {
  if (s.empty() || s.size() > 11 || s.find(':') == string::npos || s.find_first_not_of("0123456789:") != string::npos) return 0;
  int64_t out = 0, n = 0;
  for (char c : s) { if (c == ':') { out = (out + n) * 60; n = 0; } else n = n * 10 + c - '0'; }
  return int(std::min<int64_t>(out+n,86400*7));
}
struct Item { string type, id, title, channel, image, channelId, section; int duration = 0; Membership members = Membership::None; bool shortForm = false; };
struct Page {
  string title, subtitle, image, banner, channelId, continuation, error, musicSource;
  std::vector<Item> items;
  bool subscribed = false;
  int continuationDepth = 999;
};
inline string channel(const J& v) {
  if (const J* ep = find(v,"browseEndpoint")) {
    auto id = str(*ep,"browseId");
    if (channelId(id)) return id;
  }
  if (v.is<Object>()) for (const auto& kv : v.get<Object>()) { auto id = channel(kv.second); if (!id.empty()) return id; }
  for (const auto& x : array(v)) { auto id = channel(x); if (!id.empty()) return id; }
  return {};
}
inline Item item(const J& v, const string& key, bool music, const string& section) {
  Item out; out.section = section; out.members = membership(v);
  out.shortForm = key == "reelItemRenderer" || key == "shortsLockupViewModel";
  const auto& cols = array(get(v,"flexColumns"));
  const auto& meta = get(get(v,"metadata"),"tileMetadataRenderer");
  const auto& lock = get(get(v,"metadata"),"lockupMetadataViewModel");
  const J& first = cols.empty() ? get(v,"title") : get(get(cols[0],"musicResponsiveListItemFlexColumnRenderer"),"text");
  out.title = text(first);
  if (out.title.empty() && key == "reelItemRenderer") out.title = text(get(v,"headline"));
  if (out.title.empty()) out.title = text(get(meta,"title"));
  if (out.title.empty()) out.title = text(get(lock,"title"));
  out.channel = text(get(v,"subtitle"));
  if (cols.size() > 1) out.channel = text(get(get(cols[1],"musicResponsiveListItemFlexColumnRenderer"),"text"));
  for (auto k : {"shortBylineText","longBylineText","ownerText"}) if (out.channel.empty()) out.channel = text(get(v,k));
  if (out.channel.empty()) if (const auto* line = find(get(meta,"lines"),"lineItemRenderer")) out.channel = text(get(*line,"text"));
  out.id = str(v,"videoId");
  const J* ep = find(get(v,"navigationEndpoint"),"watchEndpoint");
  if (!ep) ep = find(first,"watchEndpoint");
  if (!ep) ep = find(get(v,"onSelectCommand"),"watchEndpoint");
  if (!ep) ep = find(get(v,"rendererContext"),"watchEndpoint");
  const J* browse = find(get(v,"navigationEndpoint"),"browseEndpoint");
  if (!browse) browse = find(first,"browseEndpoint");
  if (!browse) browse = find(get(v,"onSelectCommand"),"browseEndpoint");
  if (!browse) browse = find(get(v,"rendererContext"),"browseEndpoint");
  string contentType = str(v,"contentType");
  bool list = contentType.find("PLAYLIST") != string::npos || key == "playlistRenderer" || key == "gridPlaylistRenderer";
  bool artist = contentType.find("CHANNEL") != string::npos || key == "channelRenderer" || key == "gridChannelRenderer";
  if (out.id.empty() && key=="reelItemRenderer") out.id=str(v,"videoId");
  if (key=="shortsLockupViewModel") {
    if(out.id.empty()) if(const auto* reel=find(get(v,"onTap"),"reelWatchEndpoint"))out.id=str(*reel,"videoId");
    if(out.title.empty())out.title=str(get(get(v,"overlayMetadata"),"primaryText"),"content");
  }
  if (out.id.empty() && !list && !artist && ep) out.id = str(*ep,"videoId");
  if (out.id.empty()) out.id = str(get(v,"playlistItemData"),"videoId");
  if (out.id.empty() && browse) out.id = str(*browse,"browseId");
  for (auto k : {"playlistId","channelId","contentId"}) if (out.id.empty()) out.id = str(v,k);
  if (out.id.empty() && ep) out.id = str(*ep,"playlistId");
  string pageType;
  if (browse) if (const auto* type = find(*browse,"pageType")) pageType = str(*type);
  if (list) out.type = "playlist";
  else if (artist) out.type = music ? "artist" : "channel";
  else if (videoId(out.id)) out.type = "video";
  else if (pageType.find("ALBUM") != string::npos || out.id.compare(0,4,"MPRE") == 0) out.type = "album";
  else if (channelId(out.id)) out.type = music ? "artist" : "channel";
  else if (pageType.find("PLAYLIST") != string::npos || out.id.compare(0,2,"VL") == 0 || out.id.compare(0,2,"PL") == 0 || out.id.compare(0,2,"RD") == 0 || out.id.compare(0,4,"OLAK") == 0) out.type = "playlist";
  if (out.type != "video") out.members = Membership::None;
  if (!identifier(out.id) || out.type.empty() || flag(v,"isUpcoming") || find(v,"upcomingEventData") || (flag(v,"isUnplayable") && out.members == Membership::None)) out.id.clear();
  out.image = thumbnail(get(v,"thumbnail"));
  for (auto k : {"thumbnailViewModel","thumbnailRenderer","contentImage","header"}) if (out.image.empty()) out.image = thumbnail(get(v,k));
  if (out.image.empty() && out.type == "video") out.image = "https://i.ytimg.com/vi/" + out.id + "/mqdefault.jpg";
  out.channelId = channel(cols.empty() ? v : get(v,"flexColumns"));
  out.duration = seconds(text(get(v,"lengthText")));
  if (!out.duration) if (const auto* badge = find(get(v,"header"),"thumbnailOverlayTimeStatusRenderer")) out.duration = seconds(text(get(*badge,"text")));
  if (!out.duration) if (const auto* badge = find(get(v,"contentImage"),"thumbnailBadgeViewModel")) out.duration = seconds(str(*badge,"text"));
  // Inscrições/Home da conta mandam a duração em thumbnailOverlays (renderer clássico) ou num badge solto, sem lengthText.
  if (out.type == "video" && !out.duration) if (const auto* badge = find(get(v,"thumbnailOverlays"),"thumbnailOverlayTimeStatusRenderer")) out.duration = seconds(text(get(*badge,"text")));
  if (out.type == "video" && !out.duration) if (const auto* badge = find(get(v,"thumbnailOverlays"),"thumbnailBadgeViewModel")) out.duration = seconds(str(*badge,"text"));
  if (out.type == "video" && !out.duration) if (const auto* badge = find(v,"thumbnailBadgeViewModel")) out.duration = seconds(str(*badge,"text"));
  // Transmissão ao vivo: só com a marca explícita (duração 0 é desconhecida). -1 no Item e no fio = ao vivo.
  if (out.type == "video" && !out.duration && (findValue(v,"style","LIVE") || findValue(v,"style","BADGE_STYLE_TYPE_LIVE_NOW"))) out.duration = -1;
  if (!out.duration) for (const auto& c : array(get(v,"fixedColumns"))) out.duration = seconds(text(get(get(c,"musicResponsiveListItemFixedColumnRenderer"),"text")));
  if (!out.duration && cols.size() > 1) {
    for (const auto& c : cols) {
      auto s = text(get(get(c,"musicResponsiveListItemFlexColumnRenderer"),"text"));
      auto dot = s.find_last_of(' ');
      int n = seconds(dot == string::npos ? s : s.substr(dot+1));
      if (n) out.duration = n;
    }
  }
  if (out.title.empty()) out.title = out.id;
  return out;
}
inline void walk(const J& v, Page& p, bool music, std::set<string>& seen, string section = {}, int depth = 0) {
  if (v.is<Array>()) { for (const auto& x : array(v)) walk(x,p,music,seen,section,depth+1); return; }
  if (!v.is<Object>()) return;
  for (const auto& kv : v.get<Object>()) {
    const auto& key = kv.first; const auto& x = kv.second;
    if (key == "tabRenderer") {
      if (flag(x,"selected") || (music && !x.contains("selected"))) walk(get(x,"content"),p,music,seen,section,depth+1);
    } else if (key == "itemSectionRenderer") {
      // WEB: o dia do Histórico vem no cabeçalho da seção (itemSectionHeaderRenderer), que o walk ignora como "header".
      string heading = text(get(get(get(x,"header"),"itemSectionHeaderRenderer"),"title"));
      walk(get(x,"contents"),p,music,seen,heading.empty() ? section : heading,depth+1);
      walk(get(x,"continuations"),p,music,seen,section,depth+1);
    } else if (key == "horizontalListRenderer") {
      walk(get(x,"items"),p,music,seen,section,depth+1);
    } else if (key == "musicCarouselShelfRenderer" || key == "musicShelfRenderer" || key == "shelfRenderer") {
      string heading = text(get(x,"title"));
      if (heading.empty()) if (const auto* t = find(get(x,"header"),"title")) heading = text(*t);
      if (heading.empty()) if (const auto* t = find(get(x,"headerRenderer"),"title")) heading = text(*t);
      walk(get(x,"contents"),p,music,seen,heading.empty() ? section : heading,depth+1);
      walk(get(x,"content"),p,music,seen,heading.empty() ? section : heading,depth+1);
      if (key!="shelfRenderer") walk(get(x,"continuations"),p,music,seen,section,depth+1);
    } else if (key == "continuationCommand" || key == "nextContinuationData" || key == "nextRadioContinuationData") {
      string token = str(x,key == "continuationCommand" ? "token" : "continuation");
      // A TV também pagina cada carrossel. A continuação externa pertence à página.
      if (!token.empty() && token.size() <= 16384 && depth < p.continuationDepth) { p.continuation=token; p.continuationDepth=depth; }
    } else if (key == "shortsLockupViewModel" || key == "reelItemRenderer" || key == "videoRenderer" || key == "gridVideoRenderer" || key == "playlistVideoRenderer" || key == "playlistPanelVideoRenderer" || key == "musicTwoRowItemRenderer" || key == "musicResponsiveListItemRenderer" || key == "lockupViewModel" || key == "tileRenderer" || key == "channelRenderer" || key == "gridChannelRenderer" || key == "playlistRenderer" || key == "gridPlaylistRenderer") {
      Item it = item(x,key,music,section);
      if (!it.id.empty() && p.items.size() < size_t(updates::rules()->n("catalogItems")) && seen.insert(it.type+it.id).second) p.items.push_back(std::move(it));
    } else if (key != "header" && key != "menu" && key != "navigationEndpoint" && key != "onSelectCommand" && key != "onLongPressCommand" && key != "frameworkUpdates" && key != "playerOverlays" && key != "currentVideoEndpoint") walk(x,p,music,seen,section,depth+1);
  }
}
inline Page decode(const J& data, bool music) {
  Page p;
  if (data.contains("error")) { p.error = "youtube_unavailable"; return p; }
  if (!data.contains("contents") && !data.contains("continuationContents") && !data.contains("onResponseReceivedActions") && !data.contains("onResponseReceivedEndpoints")) { p.error = "invalid_catalog"; return p; }
  const J* header = nullptr;
  for (auto k : {"c4TabbedHeaderRenderer","channelHeaderRenderer","musicDetailHeaderRenderer","musicResponsiveHeaderRenderer","musicImmersiveHeaderRenderer","musicVisualHeaderRenderer","playlistHeaderRenderer","pageHeaderViewModel"}) if ((header = find(data,k))) break;
  if (header) {
    p.title = text(get(*header,"title")); p.subtitle = text(get(*header,"subtitle"));
    if (p.title.empty()) if (const auto* t = find(*header,"dynamicTextViewModel")) p.title = text(get(*t,"text"));
    if (p.subtitle.empty()) p.subtitle = text(get(*header,"subscriberCountText"));
    p.image = thumbnail(get(*header,"avatar"));
    if (p.image.empty()) p.image = thumbnail(get(*header,"thumbnail"));
    if (p.image.empty()) if (const auto* img = find(*header,"avatarViewModel")) p.image = thumbnail(*img);
    if (p.image.empty() && music) p.image = thumbnail(*header);
    p.banner = thumbnail(get(*header,"banner"));
    if (p.banner.empty()) if (const auto* banner = find(*header,"imageBannerViewModel")) p.banner = thumbnail(*banner);
    if (const auto* sub = find(*header,"subscribeButtonRenderer")) p.subscribed = flag(*sub,"subscribed");
    if (p.subtitle.empty()) {
      if (const auto* meta = find(*header,"contentMetadataViewModel")) {
        for (const auto& row : array(get(*meta,"metadataRows"))) for (const auto& part : array(get(row,"metadataParts"))) {
          string s = text(get(part,"text"));
          if (!s.empty()) { if (!p.subtitle.empty()) p.subtitle += " · "; p.subtitle += s; }
        }
      }
    }
  }
  std::set<string> seen;
  walk(data,p,music,seen);
  return p;
}
inline string wire(const Page& p) {
  if (!p.error.empty()) return "error\t" + clean(p.error) + "\n";
  string out = "ok\t" + clean(p.title) + "\t" + clean(p.subtitle) + "\t" + p.image + "\t" + (p.subscribed ? "1" : "0") + "\t" + (p.continuation.empty() ? "0" : "1") + "\t" + p.channelId + "\t" + p.banner + "\t\t\t" + p.musicSource + "\n";
  for (const auto& it : p.items) out += it.type + "\t" + it.id + "\t" + clean(it.title) + "\t" + clean(it.channel) + "\t" + std::to_string(it.duration) + "\t" + it.image + "\t" + it.channelId + "\t" + clean(it.section) + "\t" + membershipWire(it.members) + (it.shortForm ? "\tshort" : "") + "\n";
  return out;
}
inline std::map<string,string> query(const string& path) {
  std::map<string,string> out;
  size_t at = path.find('?');
  if (at == string::npos) return out;
  while (++at < path.size()) {
    size_t end = path.find('&',at), eq = path.find('=',at);
    if (eq < end) {
      string value = path.substr(eq+1,end == string::npos ? end : end-eq-1), decoded;
      for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i+2 < value.size()) { decoded += char(std::strtol(value.substr(i+1,2).c_str(),nullptr,16)); i += 2; }
        else decoded += value[i] == '+' ? ' ' : value[i];
      }
      out[path.substr(at,eq-at)] = decoded;
    }
    if (end == string::npos) break;
    at = end;
  }
  return out;
}
inline bool supported(const string& path) {
  if (path.compare(0,17,"/api/nav/channel?") == 0 || path.compare(0,16,"/api/nav/lyrics?") == 0) return true;
  if (path.compare(0,9,"/api/nav?") != 0 && path.compare(0,14,"/api/nav/play?") != 0) return false;
  auto q = query(path); auto kind = q["kind"];
  return kind == "search" || kind == "channel" || kind == "playlist" || kind == "music" || kind == "music_search" || kind == "album" || kind == "artist" || kind == "radio" || kind == "music_playlist" || kind == "music_recent" || kind == "music_library";
}
class Client {
  Transport& net;
  struct Context { J data; string version, visitor; } web, music, accountMusic, tvMusic;
  enum class MusicSource { Auto, Web, TV, Public };
  string token, language = "en";
  bool tokenRejected = false, musicTokenRejected = false;
  std::function<void(const string&)> logger = [](const string& line) { std::fprintf(stderr,"%s\n",line.c_str()); };
  long lastHttp = 0;
public:
  string authError; // último motivo de recusa do token (vazio se aceito)
  string musicSource; // diagnóstico sem token nem identificadores pessoais
private:
  // Com o UA Tube360 o Music devolve a página de navegador incompatível, sem contexto InnerTube.
  std::shared_ptr<const updates::Rules> policy=updates::rules();
  struct List {
    MusicSource source = MusicSource::Auto;
    Page header;
    std::map<int,Page> pages;
    std::set<string> seen, tokens;
    string next;
    int offset = 0, count = 0;
    std::time_t created = 0;
  };
  std::map<string,List> lists;
  string fail(const string& route, long http, const string& code, int provider = 0) {
    // Só rota interna, números e códigos locais: nunca corpo, URL, query, cursor ou mensagem remota.
    if (logger) logger("catalogo falha: rota="+route+" http="+std::to_string(http)+" codigo="+code+" api_code="+std::to_string(provider));
    return code;
  }
  string networkError(const Response& r, const string& route) {
    return fail(route,r.status,"network_error transport="+std::to_string(r.transportCode)+
        " failure="+std::to_string(int(r.failure))+" tls="+std::to_string(r.tlsCode)+" verify="+std::to_string(r.tlsVerify));
  }
  string error(const Response& r, J& data, const string& route) {
    lastHttp=r.status;
    data=J();
    bool valid=parse(r.body,data);
    const auto& remote=get(get(data,"error"),"code");
    int provider=remote.is<double>() && remote.get<double>()>=0 && remote.get<double>()<=999 ? int(remote.get<double>()) : 0;
    if (!r.error.empty()) { networkError(r,route); return "network_error"; }
    if (r.status != 200) return fail(route,r.status,"HTTP " + std::to_string(r.status),provider);
    if (!valid) return fail(route,r.status,"invalid_json");
    if (data.contains("error")) return fail(route,r.status,"youtube_unavailable",provider);
    return {};
  }
  static bool guestResponse(const J& data) {
    const auto& context=get(data,"responseContext");
    if (flag(context,"loggedOut")) return true;
    for (const auto& service:array(get(context,"serviceTrackingParams"))) if (str(service,"service")=="GFEEDBACK")
      for (const auto& param:array(get(service,"params"))) if (str(param,"key")=="logged_in" && str(param,"value")=="0") return true;
    return false;
  }
  void bindRules() {
    auto next=updates::rules();
    if(next->version!=policy->version){policy=next;web={};music={};accountMusic={};tvMusic={};lists.clear();tokenRejected=musicTokenRejected=false;}
  }
  string bootstrap(bool isMusic) {
    auto& c = isMusic ? music : web;
    if (!c.version.empty()) return {};
    Request request; request.url = policy->s(isMusic ? "musicOrigin" : "webOrigin")+"/"; request.agent = policy->s("catalogAgent");
    request.headers={"Cookie:"};
    auto r = net.send(request); lastHttp=r.status;
    const string route=isMusic ? "WEB_REMIX/bootstrap" : "WEB/bootstrap";
    if (!r.error.empty()) { networkError(r,route); return "session_unavailable"; }
    if (r.status != 200) return fail(route,r.status,"session_unavailable");
    auto at = r.body.find(policy->s("contextMarker"));
    if (at == string::npos) return fail(route,r.status,"context_missing");
    at = r.body.find('{',at+policy->s("contextMarker").size());
    auto end = jsonEnd(r.body,at);
    if (end == string::npos || !parse(r.body.substr(at,end-at),c.data)) return fail(route,r.status,"invalid_context");
    const auto& client = get(c.data,"client");
    if (str(client,"clientName") != policy->s(isMusic?"musicName":"webName")) return fail(route,r.status,"invalid_client");
    c.version = str(client,"clientVersion"); c.visitor = str(client,"visitorData");
    return c.version.empty() ? fail(route,r.status,"version_missing") : "";
  }
  string bootstrapAccountMusic() {
    if (!accountMusic.version.empty()) return {};
    return bootstrapIdentity(accountMusic,false);
  }
  string bootstrapIdentity(Context& target, bool tv) {
    if (!target.version.empty()) return {};
    Request r; r.url = policy->s(tv ? "webOrigin" : "musicOrigin") + (tv ? policy->s("tvPath") : "/");
    r.agent = tv ? policy->tv.agent : policy->s("catalogAgent");
    r.headers = {"Accept: */*","Accept-Language: " + language,"Cookie:"};
    auto res = net.send(r); lastHttp=res.status;
    const string route=tv ? "TVHTML5/bootstrap" : "WEB_REMIX/bootstrap";
    if (!res.error.empty()) { networkError(res,route); return "session_unavailable"; }
    if (res.status != 200) return fail(route,res.status,"session_unavailable");
    J data;
    auto at=res.body.find(policy->s("contextMarker"));
    if (at==string::npos) return fail(route,res.status,"invalid_context");
    at=res.body.find('{',at+policy->s("contextMarker").size()); auto end=jsonEnd(res.body,at);
    if (end==string::npos || !parse(res.body.substr(at,end-at),data)) return fail(route,res.status,"invalid_context");
    const auto& remote=get(data,"client");
    auto version=str(remote,"clientVersion"), visitor=str(remote,"visitorData");
    auto safe=[](const string& s) { return s.size()<=8192 && std::all_of(s.begin(),s.end(),[](unsigned char c) { return c>32 && c<127; }); };
    if (str(remote,"clientName")!=(tv ? policy->tv.name : policy->s("musicName")) || version.empty() || !safe(version) || !safe(visitor)) return fail(route,res.status,"invalid_context");
    // Identidade publicada pelo próprio cliente; sem IP, rollout ou contexto desktop da sessão de TV.
    Object client{{"clientName",J(str(remote,"clientName"))},{"clientVersion",J(version)}};
    if (!visitor.empty()) client["visitorData"]=J(visitor);
    target.data=J(Object{{"client",J(client)},{"user",J(Object{})}});
    target.version=version; target.visitor=visitor;
    return {};
  }
  string post(bool isMusic, const string& route, Object args, J& out, MusicSource* selected = nullptr) {
    MusicSource local=MusicSource::Auto;
    auto& source=selected ? *selected : local;
    if (isMusic && !token.empty() && source!=MusicSource::Public) {
      bool tv=source==MusicSource::TV || (source==MusicSource::Auto && musicTokenRejected);
      auto& identity=tv ? tvMusic : accountMusic;
      auto e=tv ? bootstrapIdentity(tvMusic,true) : bootstrapAccountMusic();
      if (!e.empty()) return e;
      auto context = identity.data.get<Object>();
      auto client = get(identity.data,"client").get<Object>();
      client["hl"] = J(language); client["gl"] = J(string(localeRegion(language))); context["client"] = J(client);
      Object payload = args; payload["context"] = J(context);
      if (!tv) payload["isAudioOnly"] = J(true);
      if (tv && route=="browse") {
        auto bid=str(J(args),"browseId");
        if (bid==policy->s("musicHomeId")) payload["browseId"]=J(string("FEtopics_music"));
        else if (bid=="FEmusic_history") payload["browseId"]=J(string("FEmusic_last_played"));
        else if (bid=="FEmusic_liked_playlists") payload["browseId"]=J(policy->s("libraryId"));
      }
      Request r; r.url = policy->api(route,!tv)+"&alt=json";
      r.body = J(payload).serialize(); r.agent = tv ? policy->tv.agent : policy->s("catalogAgent");
      // Cookie: vazio seleciona o handle sem pote do transporte, preservando a sessão pública.
      r.headers = {"Accept: */*","Accept-Language: *","Origin: "+policy->s(tv ? "webOrigin" : "musicOrigin"),"Cookie:",
        policy->s("clientNameHeader")+": "+(tv ? policy->tv.code : policy->s("musicCode")),policy->s("clientVersionHeader")+": " + identity.version,
        "Authorization: Bearer " + token,"X-Goog-Request-Time: "+std::to_string(std::time(nullptr))};
      if (tv) r.headers.push_back("Referer: "+policy->s("webOrigin")+policy->s("tvPath"));
      if (!identity.visitor.empty()) r.headers.push_back(policy->s("visitorHeader")+": " + identity.visitor);
      auto res = net.send(r);
      auto result=error(res,out,string(tv ? "TVHTML5/" : "WEB_REMIX/")+route);
      bool signedOut=false;
      if (res.status != 400 && res.status != 401 && res.status != 403) {
        signedOut=result.empty() && guestResponse(out);
        if (signedOut) fail(string(tv ? "TVHTML5/" : "WEB_REMIX/")+route,res.status,"signed_out");
        if (!signedOut) {
          if (result.empty()) { source=tv ? MusicSource::TV : MusicSource::Web; musicSource=tv ? "tv" : "web"; authError.clear(); }
          return result;
        }
      }
      authError = string(tv ? "TVHTML5 " : "WEB_REMIX ")+route + (signedOut ? " signed_out" : " HTTP " + std::to_string(res.status));
      // Cursor e segunda etapa pertencem ao cliente que os emitiu. Nunca os enviar à reserva.
      if (source!=MusicSource::Auto || args.count("continuation")) return "account_music_unavailable";
      if (!tv) { musicTokenRejected=true; return post(true,route,args,out,&source); }
      if (str(J(args),"browseId")=="FEmusic_history" || str(J(args),"browseId")=="FEmusic_liked_playlists" || str(J(args),"browseId")=="VLLM") return "account_music_unavailable";
      source=MusicSource::Public;
      // A reserva pública precisa reconstruir URL, contexto e cabeçalhos, além de retirar o token.
    }
    auto e = bootstrap(isMusic); if (!e.empty()) return e;
    auto& c = isMusic ? music : web;
    auto context = c.data.get<Object>();
    auto client = get(c.data,"client").get<Object>();
    client["hl"] = J(language); client["gl"] = J(string(localeRegion(language))); context["client"] = J(client);
    args["context"] = J(context);
    string origin = policy->s(isMusic?"musicOrigin":"webOrigin");
    Request r; r.url = policy->api(route,isMusic); r.body = J(args).serialize(); r.agent = policy->s("catalogAgent");
    r.headers = {"Cookie:","Origin: " + origin,policy->s("clientNameHeader")+": "+policy->s(isMusic?"musicCode":"webCode"),policy->s("clientVersionHeader")+": " + c.version};
    if (!c.visitor.empty() && c.visitor.find_first_of("\r\n") == string::npos) r.headers.push_back(policy->s("visitorHeader")+": " + c.visitor);
    // Busca WEB é pública, inclusive sem filtros e nas continuações. O grant TV
    // permanece nas rotas de conta existentes; nunca condiciona uma busca pública.
    bool authorized=!isMusic && route!="search" && !token.empty() && !tokenRejected;
    if (authorized) r.headers.push_back("Authorization: Bearer "+token);
    auto res = net.send(r);
    auto result=error(res,out,string(isMusic ? "WEB_REMIX/" : "WEB/")+route);
    if (authorized && (res.status==401 || res.status==403)) {
      tokenRejected=true; authError=route+" HTTP "+std::to_string(res.status);
      r.headers.pop_back(); res=net.send(r);
      result=error(res,out,"WEB/"+route);
    }
    if (isMusic && result.empty()) { source=MusicSource::Public; musicSource="public"; }
    updates::observe(policy->version,result.empty(),result=="invalid_json" || result=="HTTP 400" || result=="HTTP 403");
    return result;
  }
  string channelTab(J& data, const string& tab, bool isMusic) {
    const J* tabs = find(get(data,"contents"),"tabs");
    if (!tabs) return "channel_tabs_missing";
    string suffix = tab == "shorts" ? "/shorts" : tab == "live" ? "/streams" : tab == "playlists" ? "/playlists" : "/videos";
    for (const auto& t : array(*tabs)) {
      const auto& r = get(t,"tabRenderer");
      string url;
      if (const auto* u = find(get(r,"endpoint"),"webCommandMetadata")) url = str(*u,"url");
      string title = text(get(r,"title"));
      bool match = url.size() >= suffix.size() && url.compare(url.size()-suffix.size(),suffix.size(),suffix) == 0;
      if (url.empty()) match = tab == "shorts" ? title == "Shorts" : tab == "live" ? title == "Live" || title == "Ao vivo" : tab == "playlists" ? title == "Playlists" : title == "Videos" || title == "Vídeos";
      if (!match) continue;
      if (flag(r,"selected")) return {};
      if (const auto* ep = find(get(r,"endpoint"),"browseEndpoint")) {
        Object args; args["browseId"] = J(str(*ep,"browseId"));
        auto params = str(*ep,"params"); if (!params.empty()) args["params"] = J(params);
        J next; auto e = post(isMusic,"browse",args,next);
        if (e.empty()) { auto root = next.get<Object>(); root["header"] = get(data,"header"); data = J(root); }
        return e;
      }
      return "channel_tab_unavailable";
    }
    auto root = data.get<Object>(); root["contents"] = J(Array{}); data = J(root);
    return {};
  }
public:
  explicit Client(Transport& transport) : net(transport) {}
  void setLogger(std::function<void(const string&)> sink) { logger=std::move(sink); }
  // Chamar no worker de navegação ao receber/renovar/remover o token da F11.
  bool setToken(const string& access) {
    if (access.size() > 8192 || !std::all_of(access.begin(),access.end(),[](unsigned char c) { return c > 32 && c < 127; })) return false;
    if (token != access) { token = access; tokenRejected = musicTokenRejected = false; authError.clear(); musicSource.clear(); lists.clear(); web = {}; music = {}; accountMusic = {}; tvMusic = {}; }
    return true;
  }
  Page browse(const std::map<string,string>& q) {
    bindRules();
    auto value = [&](const char* name) { auto it = q.find(name); return it == q.end() ? string{} : it->second; };
    string kind = value("kind"), id = value("id"), search = value("q"), tab = value("tab"), filter = value("filter");
    language = localeLanguage(value("lang"));
    bool isMusic = kind != "channel" && kind != "playlist" && kind != "search";
    Page failed;
    if (token.empty() && (kind=="music_recent" || kind=="music_library")) { failed.error="signed_out"; return failed; }
    if ((kind == "channel" || kind == "artist") ? !channelId(id) : kind == "radio" ? !videoId(id) : (kind == "playlist" || kind == "music_playlist" || kind == "album") ? !identifier(id) : false) { failed.error = "invalid_navigation"; return failed; }
    string key = kind + "|" + id + "|" + search + "|" + tab + "|" + filter + "|" + value("params") + "|" + language;
    int offset = std::max(0,std::atoi(value("offset").c_str()));
    if (lists.size() >= 12 && !lists.count(key)) lists.erase(lists.begin());
    auto& list = lists[key];
    if (!offset && (value("fresh") == "1" || std::time(nullptr)-list.created > 300)) list = {};
    if (auto it = list.pages.find(offset); it != list.pages.end()) { if (isMusic) musicSource=it->second.musicSource; return it->second; }
    if (offset && (offset != list.offset || list.next.empty())) { failed.error = "continuation_expired"; return failed; }
    Object args; string route = "browse";
    if (offset) args["continuation"] = J(list.next);
    if (kind == "search") {
      route = "search";
      if (!offset) {
        args["query"] = J(search);
        auto params=value("params");
        if (params.empty() && (filter=="1" || filter=="2")) { yt::search::Filters legacy; legacy.type=std::atoi(filter.c_str()); params=legacy.params(); }
        if (!params.empty()) args["params"] = J(params);
      }
    } else if (kind == "music_search") {
      route = "search";
      if (!offset) {
        args["query"] = J(search);
        const string params[]={"",policy->s("musicSearchSongs"),policy->s("musicSearchAlbums"),policy->s("musicSearchArtists")};
        int n = std::atoi(filter.c_str());
        if (n > 0 && n < 4) args["params"] = J(string(params[n]));
      }
    } else if (kind == "radio") {
      route = "next";
      if (!offset) { args["videoId"] = J(id); args["playlistId"] = J("RDAMVM"+id); }
    } else if (!offset) {
      string browseId = id;
      if (kind == "music") browseId = search == "charts" ? policy->s("musicExploreId") : policy->s("musicHomeId");
      if (kind == "music_recent") browseId="FEmusic_history";
      if (kind == "music_library") browseId="FEmusic_liked_playlists";
      if ((kind == "playlist" || kind == "music_playlist") && id.compare(0,2,"VL") != 0) browseId = "VL"+id;
      args["browseId"] = J(browseId);
    }
    J data; failed.error = post(isMusic,route,args,data,&list.source);
    if (!failed.error.empty()) return failed;
    if (!offset && kind == "channel") failed.error = channelTab(data,tab,isMusic);
    if (!offset && kind == "music" && search == "charts") {
      const auto* browse = findValue(data,"browseId",policy->s("musicChartsId"));
      if (!browse || str(*browse,"browseId") != policy->s("musicChartsId")) failed.error = "charts_unavailable";
      else {
        Object charts; charts["browseId"] = J(string(policy->s("musicChartsId")));
        if (!str(*browse,"params").empty()) charts["params"] = get(*browse,"params");
        failed.error = post(true,"browse",charts,data,&list.source);
      }
    }
    if (!failed.error.empty()) return failed;
    Page p = decode(data,isMusic);
    if (kind=="search") p.title=search;
    if (isMusic) p.musicSource=list.source==MusicSource::TV ? "tv" : list.source==MusicSource::Web ? "web" : "public";
    if (!p.error.empty()) { fail((isMusic ? "music/" : "WEB/")+route,200,p.error); return p; }
    if (kind == "channel") p.channelId = id;
    if (!offset) { list.header = p; list.header.items.clear(); list.created = std::time(nullptr); }
    else { p.title = list.header.title; p.subtitle = list.header.subtitle; p.image = list.header.image; p.banner = list.header.banner; p.channelId = list.header.channelId; p.subscribed = list.header.subscribed; }
    if (kind == "music" && search == "mixes") p.items.erase(std::remove_if(p.items.begin(),p.items.end(),[](const Item& i) { return i.type != "playlist"; }),p.items.end());
    p.items.erase(std::remove_if(p.items.begin(),p.items.end(),[&](const Item& i) { return !list.seen.insert(i.type+i.id).second; }),p.items.end());
    if (++list.count >= policy->n("listPages") || list.seen.size() >= size_t(policy->n("listItems")) || p.items.empty() || !list.tokens.insert(p.continuation).second) p.continuation.clear();
    list.next = p.continuation; list.offset += int(p.items.size()); list.pages[offset] = p;
    return p;
  }
private:
  string handleImpl(const string& path) {
    bindRules();
    auto q = query(path);
    if (path.compare(0,17,"/api/nav/channel?") == 0) {
      if (!videoId(q["id"])) return "error\tinvalid_video\n";
      J data; Object args; args["videoId"] = J(q["id"]);
      auto e = post(false,"next",args,data);
      string id = channel(get(data,"contents"));
      if (!e.empty() || id.empty()) return "error\tchannel_unavailable\n";
      q["kind"] = "channel"; q["id"] = id; q["tab"] = "videos";
      return wire(browse(q));
    }
    if (path.compare(0,16,"/api/nav/lyrics?") == 0) {
      if (!videoId(q["id"])) return "error\tinvalid_video\n";
      language = localeLanguage(q["lang"]);
      J data; Object args; args["videoId"] = J(q["id"]);
      MusicSource source=MusicSource::Auto;
      auto e = post(true,"next",args,data,&source);
      if (!e.empty()) return "error\t"+e+"\n";
      const auto* tabs = find(data,"tabs"); string id;
      if (tabs) for (const auto& t : array(*tabs)) {
        const auto* ep = find(get(t,"tabRenderer"),"browseEndpoint");
        if (ep && str(*ep,"browseId").compare(0,4,"MPLY") == 0) { id = str(*ep,"browseId"); break; }
      }
      if (id.empty()) return "ok\t\n";
      args.clear(); args["browseId"] = J(id);
      e = post(true,"browse",args,data,&source);
      if (!e.empty()) return "error\t"+e+"\n";
      const auto* lyrics = find(data,"musicDescriptionShelfRenderer");
      return "ok\t" + clean(lyrics ? text(get(*lyrics,"description")) : "") + "\n";
    }
    if (path.compare(0,14,"/api/nav/play?") == 0) {
      q["offset"] = "0"; Page queue = browse(q);
      if (!queue.error.empty()) return wire(queue);
      int offset = int(queue.items.size());
      queue.items.erase(std::remove_if(queue.items.begin(),queue.items.end(),[](const Item& i) { return i.type != "video"; }),queue.items.end());
      for (int n = 0; !queue.continuation.empty() && queue.items.size() < 256 && n < 39; ++n) {
        q["offset"] = std::to_string(offset); Page next = browse(q);
        if (!next.error.empty()) { queue.error = next.error; break; }
        offset += int(next.items.size()); queue.continuation = next.continuation;
        for (auto& i : next.items) if (i.type == "video" && queue.items.size() < 256) queue.items.push_back(std::move(i));
      }
      if (queue.items.size() > size_t(policy->n("queueItems"))) queue.items.resize(size_t(policy->n("queueItems")));
      queue.continuation.clear(); return wire(queue);
    }
    return wire(browse(q));
  }
public:
  string handle(const string& path) {
    lastHttp=0;
    if (query(path)["kind"]=="search") musicSource.clear();
    auto result=handleImpl(path);
    if (result.starts_with("error\t")) {
      auto end=result.find('\n');
      const char* route=path.starts_with("/api/nav/lyrics?") ? "/api/nav/lyrics" : path.starts_with("/api/nav/channel?") ? "/api/nav/channel" : path.starts_with("/api/nav/play?") ? "/api/nav/play" : "/api/nav";
      fail(route,lastHttp,result.substr(6,end==string::npos ? end : end-6));
    }
    return result;
  }

  // ---- F61 Início e coleção: funções novas desta frente só aqui

  // ---- F62 Busca e canal: funções novas desta frente só aqui

  // ---- F63 Player e Shorts: funções novas desta frente só aqui

  // ---- F64 Configurações e controle: funções novas desta frente só aqui

  // ---- F65 Música: funções novas desta frente só aqui

  // ---- F66 controle pelo celular: funções novas desta frente só aqui

  // ---- F67 membros tocando: funções novas desta frente só aqui

};
}
