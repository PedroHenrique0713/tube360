#pragma once
#include "locale.hpp"
#include "client.hpp"
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
  if (start >= s.size() || s[start] != '{') return string::npos;
  int depth = 0, nodes = 0; bool quoted = false, escaped = false;
  for (size_t i = start; i < s.size(); ++i) {
    char c = s[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
    } else if (c == '"') quoted = true;
    else if (c == '{' || c == '[') { if (++depth > 80 || ++nodes > 160000) return string::npos; }
    else if (c == ',') { if (++nodes > 160000) return string::npos; }
    else if ((c == '}' || c == ']') && --depth == 0) return i + 1;
  }
  return string::npos;
}
inline bool parse(const string& s, J& out) {
  if (s.size() > 3u * 1024 * 1024) return false;
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
inline string imageUrl(const string& u) {
  if (u.compare(0,8,"https://") != 0 || u.size() > 4096 || u.find_first_of("@\\ \r\n\t#") != string::npos) return {};
  const auto slash = u.find('/',8);
  const auto host = u.substr(8,slash-8);
  for (const auto* domain : {"ytimg.com","ggpht.com","googleusercontent.com"}) {
    string d = domain;
    if (host == d || (host.size() > d.size() && host.compare(host.size()-d.size()-1,d.size()+1,"."+d) == 0)) return u;
  }
  return {};
}
inline string thumbnail(const J& v) {
  const J* pics = find(v,"thumbnails");
  if (!pics) pics = find(v,"sources");
  string best; double score = -1;
  if (pics) for (const auto& p : array(*pics)) {
    auto url = imageUrl(str(p,"url"));
    const auto& w = get(p,"width");
    double width = w.is<double>() ? w.get<double>() : 0;
    if (!url.empty() && width <= 1280 && width > score) { best = url; score = width; }
  }
  return best;
}
inline int seconds(const string& s) {
  if (s.empty() || s.size() > 11 || s.find(':') == string::npos || s.find_first_not_of("0123456789:") != string::npos) return 0;
  int64_t out = 0, n = 0;
  for (char c : s) { if (c == ':') { out = (out + n) * 60; n = 0; } else n = n * 10 + c - '0'; }
  return int(std::min<int64_t>(out+n,86400*7));
}
struct Item { string type, id, title, channel, image, channelId, section; int duration = 0; };
struct Page {
  string title, subtitle, image, banner, channelId, continuation, error;
  std::vector<Item> items;
  bool subscribed = false;
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
  Item out; out.section = section;
  const auto& cols = array(get(v,"flexColumns"));
  const auto& meta = get(get(v,"metadata"),"tileMetadataRenderer");
  const auto& lock = get(get(v,"metadata"),"lockupMetadataViewModel");
  const J& first = cols.empty() ? get(v,"title") : get(get(cols[0],"musicResponsiveListItemFlexColumnRenderer"),"text");
  out.title = text(first);
  if (out.title.empty()) out.title = text(get(meta,"title"));
  if (out.title.empty()) out.title = text(get(lock,"title"));
  out.channel = text(get(v,"subtitle"));
  if (cols.size() > 1) out.channel = text(get(get(cols[1],"musicResponsiveListItemFlexColumnRenderer"),"text"));
  for (auto k : {"shortBylineText","longBylineText","ownerText"}) if (out.channel.empty()) out.channel = text(get(v,k));
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
  if (!identifier(out.id) || out.type.empty() || find(get(v,"navigationEndpoint"),"reelWatchEndpoint") || flag(v,"isUpcoming") || find(v,"upcomingEventData") || flag(v,"isUnplayable")) out.id.clear();
  out.image = thumbnail(get(v,"thumbnail"));
  for (auto k : {"thumbnailRenderer","header","contentImage"}) if (out.image.empty()) out.image = thumbnail(get(v,k));
  if (out.image.empty() && out.type == "video") out.image = "https://i.ytimg.com/vi/" + out.id + "/mqdefault.jpg";
  out.channelId = channel(cols.empty() ? v : get(v,"flexColumns"));
  out.duration = seconds(text(get(v,"lengthText")));
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
inline void walk(const J& v, Page& p, bool music, std::set<string>& seen, string section = {}) {
  if (v.is<Array>()) { for (const auto& x : array(v)) walk(x,p,music,seen,section); return; }
  if (!v.is<Object>()) return;
  for (const auto& kv : v.get<Object>()) {
    const auto& key = kv.first; const auto& x = kv.second;
    if (key == "tabRenderer") {
      if (flag(x,"selected") || (music && !x.contains("selected"))) walk(get(x,"content"),p,music,seen,section);
    } else if (key == "musicCarouselShelfRenderer" || key == "musicShelfRenderer" || key == "shelfRenderer") {
      string heading = text(get(x,"title"));
      if (heading.empty()) if (const auto* t = find(get(x,"header"),"title")) heading = text(*t);
      if (heading.empty()) if (const auto* t = find(get(x,"headerRenderer"),"title")) heading = text(*t);
      walk(get(x,"contents"),p,music,seen,heading.empty() ? section : heading);
      walk(get(x,"content"),p,music,seen,heading.empty() ? section : heading);
      walk(get(x,"continuations"),p,music,seen,section);
    } else if (key == "continuationCommand" || key == "nextContinuationData" || key == "nextRadioContinuationData") {
      string token = str(x,key == "continuationCommand" ? "token" : "continuation");
      if (p.continuation.empty() && token.size() <= 16384) p.continuation = token;
    } else if (key == "videoRenderer" || key == "gridVideoRenderer" || key == "playlistVideoRenderer" || key == "playlistPanelVideoRenderer" || key == "musicTwoRowItemRenderer" || key == "musicResponsiveListItemRenderer" || key == "lockupViewModel" || key == "tileRenderer" || key == "channelRenderer" || key == "gridChannelRenderer" || key == "playlistRenderer" || key == "gridPlaylistRenderer") {
      Item it = item(x,key,music,section);
      if (!it.id.empty() && p.items.size() < 240 && seen.insert(it.type+it.id).second) p.items.push_back(std::move(it));
    } else if (key != "header" && key != "menu" && key != "navigationEndpoint" && key != "onSelectCommand" && key != "onLongPressCommand" && key != "frameworkUpdates" && key != "playerOverlays" && key != "currentVideoEndpoint") walk(x,p,music,seen,section);
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
  string out = "ok\t" + clean(p.title) + "\t" + clean(p.subtitle) + "\t" + p.image + "\t" + (p.subscribed ? "1" : "0") + "\t" + (p.continuation.empty() ? "0" : "1") + "\t" + p.channelId + "\t" + p.banner + "\n";
  for (const auto& it : p.items) out += it.type + "\t" + it.id + "\t" + clean(it.title) + "\t" + clean(it.channel) + "\t" + std::to_string(it.duration) + "\t" + it.image + "\t" + it.channelId + "\t" + clean(it.section) + "\n";
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
  return kind == "channel" || kind == "playlist" || kind == "music" || kind == "music_search" || kind == "album" || kind == "artist" || kind == "radio" || kind == "music_playlist";
}
class Client {
  Transport& net;
  struct Context { J data; string version, visitor; } web, music, accountMusic;
  string token, language = "en";
  bool tokenRejected = false, musicTokenRejected = false;
public:
  string authError; // último motivo de recusa do token (vazio se aceito)
private:
  // Com o UA Tube360 o Music devolve a página de navegador incompatível, sem contexto InnerTube.
  static constexpr const char* agent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36";
  struct List {
    Page header;
    std::map<int,Page> pages;
    std::set<string> seen, tokens;
    string next;
    int offset = 0, count = 0;
    std::time_t created = 0;
  };
  std::map<string,List> lists;
  static string error(const Response& r, J& data) {
    if (!r.error.empty()) return "network_error";
    if (r.status != 200) return "HTTP " + std::to_string(r.status);
    return parse(r.body,data) ? "" : "invalid_json";
  }
  string bootstrap(bool isMusic) {
    auto& c = isMusic ? music : web;
    if (!c.version.empty()) return {};
    Request request; request.url = isMusic ? "https://music.youtube.com/" : "https://www.youtube.com/"; request.agent = agent;
    auto r = net.send(request);
    if (r.status != 200 || !r.error.empty()) return "session_unavailable";
    auto at = r.body.find("\"INNERTUBE_CONTEXT\"");
    if (at == string::npos) return "context_missing";
    at = r.body.find('{',at+19);
    auto end = jsonEnd(r.body,at);
    if (end == string::npos || !parse(r.body.substr(at,end-at),c.data)) return "invalid_context";
    const auto& client = get(c.data,"client");
    if (str(client,"clientName") != (isMusic ? "WEB_REMIX" : "WEB")) return "invalid_client";
    c.version = str(client,"clientVersion"); c.visitor = str(client,"visitorData");
    return c.version.empty() ? "version_missing" : "";
  }
  string bootstrapAccountMusic() {
    if (!accountMusic.version.empty()) return {};
    Request r; r.url = "https://www.youtube.com/sw.js_data"; r.agent = agent;
    // A sessão OAuth não herda os cookies da navegação pública no mesmo transporte.
    r.headers = {"Accept: */*","Accept-Language: " + language,"Referer: https://www.youtube.com/sw.js","Cookie: PREF=tz=UTC;"};
    auto res = net.send(r);
    if (res.status != 200 || !res.error.empty()) return "session_unavailable";
    J data;
    if (res.body.compare(0,4,")]}'") != 0 || !parse("{\"data\":"+res.body.substr(4)+"}",data)) return "invalid_context";
    auto at = [](const J& v, size_t i) -> const J& { static const J nil; const auto& a = array(v); return i < a.size() ? a[i] : nil; };
    const auto& device = at(at(at(at(get(data,"data"),0),2),0),0);
    string visitor = str(at(device,13));
    if (!device.is<Array>() || visitor.empty() || visitor.size() > 8192 ||
        !std::all_of(visitor.begin(),visitor.end(),[](unsigned char c) { return c > 32 && c < 127; })) return "invalid_context";
    Object client{{"hl",J(language)},{"gl",J(string(localeRegion(language)))},
      {"screenDensityFloat",J(1.0)},{"screenHeightPoints",J(1440.0)},
      {"screenPixelDensity",J(1.0)},{"screenWidthPoints",J(2560.0)},{"visitorData",at(device,13)},
      {"clientName",J(string("WEB_REMIX"))},{"clientVersion",J(string("1.20250219.01.00"))},
      {"userAgent",J(string(agent))},
      {"platform",J(string("DESKTOP"))},{"clientFormFactor",J(string("UNKNOWN_FORM_FACTOR"))},
      {"userInterfaceTheme",J(string("USER_INTERFACE_THEME_LIGHT"))},{"timeZone",J(str(at(device,79)).empty() ? string("UTC") : str(at(device,79)))},
      {"originalUrl",J(string("https://www.youtube.com"))},{"utcOffsetMinutes",J(0.0)},
      {"memoryTotalKbytes",J(string("8000000"))},
      {"mainAppWebInfo",J(Object{{"graftUrl",J(string("https://www.youtube.com"))},
        {"pwaInstallabilityStatus",J(string("PWA_INSTALLABILITY_STATUS_UNKNOWN"))},
        {"webDisplayMode",J(string("WEB_DISPLAY_MODE_BROWSER"))},{"isWebNativeShareAvailable",J(true)}})}};
    // Campos opcionais ficam ausentes, como no JSON.stringify da youtubei.js 16.0.1.
    for (auto field : {std::pair<const char*,size_t>{"remoteHost",3},{"osName",17},{"osVersion",18},
        {"deviceMake",11},{"deviceModel",12},{"browserName",86},{"browserVersion",87},
        {"rolloutToken",107},{"deviceExperimentId",103}})
      if (field.second < array(device).size()) client[field.first] = at(device,field.second);
    accountMusic.data = J(Object{{"client",J(client)},
      {"user",J(Object{{"enableSafetyMode",J(false)},{"lockedSafetyMode",J(false)}})},
      {"request",J(Object{{"useSsl",J(true)},{"internalExperimentFlags",J(Array{})}})}});
    accountMusic.version = str(J(client),"clientVersion"); accountMusic.visitor = visitor;
    return {};
  }
  string post(bool isMusic, const string& route, Object args, J& out) {
    if (isMusic && !token.empty() && !musicTokenRejected) {
      auto e = bootstrapAccountMusic(); if (!e.empty()) return e;
      auto context = accountMusic.data.get<Object>();
      auto client = get(accountMusic.data,"client").get<Object>();
      client["hl"] = J(language); client["gl"] = J(string(localeRegion(language))); context["client"] = J(client);
      Object payload = args; payload["context"] = J(context); payload["isAudioOnly"] = J(true);
      Request r; r.url = "https://music.youtube.com/youtubei/v1/"+route+"?prettyPrint=false&alt=json";
      r.body = J(payload).serialize(); r.agent = agent;
      // Cookie: vazio seleciona o handle sem pote do transporte, preservando a sessão pública.
      r.headers = {"Accept: */*","Accept-Language: *","Origin: https://music.youtube.com","Cookie:",
        "X-Youtube-Client-Name: 67","X-Youtube-Client-Version: " + accountMusic.version,
        "Authorization: Bearer " + token};
      r.headers.push_back("X-Goog-Visitor-Id: " + accountMusic.visitor);
      auto res = net.send(r);
      if (res.status != 400 && res.status != 401 && res.status != 403) {
        auto e = error(res,out); if (e.empty()) authError.clear(); return e;
      }
      musicTokenRejected = true; authError = route + " HTTP " + std::to_string(res.status);
      // A reserva pública precisa reconstruir URL, contexto e cabeçalhos, além de retirar o token.
    }
    auto e = bootstrap(isMusic); if (!e.empty()) return e;
    auto& c = isMusic ? music : web;
    auto context = c.data.get<Object>();
    auto client = get(c.data,"client").get<Object>();
    client["hl"] = J(language); client["gl"] = J(string(localeRegion(language))); context["client"] = J(client);
    args["context"] = J(context);
    string origin = isMusic ? "https://music.youtube.com" : "https://www.youtube.com";
    Request r; r.url = origin + "/youtubei/v1/" + route + "?prettyPrint=false"; r.body = J(args).serialize(); r.agent = agent;
    r.headers = {"Origin: " + origin,"X-Youtube-Client-Name: " + string(isMusic ? "67" : "1"),"X-Youtube-Client-Version: " + c.version};
    if (!c.visitor.empty() && c.visitor.find_first_of("\r\n") == string::npos) r.headers.push_back("X-Goog-Visitor-Id: " + c.visitor);
    bool authorized = !isMusic && !token.empty() && !tokenRejected;
    if (authorized) r.headers.push_back("Authorization: Bearer " + token);
    auto res = net.send(r);
    if (authorized && (res.status == 401 || res.status == 403)) {
      // Token da conta recusado aqui: segue deslogado (a tela não pode cair) e guarda o motivo para o log.
      tokenRejected = true; authError = route + " HTTP " + std::to_string(res.status);
      r.headers.pop_back();
      res = net.send(r);
    }
    return error(res,out);
  }
  string channelTab(J& data, const string& tab, bool isMusic) {
    const J* tabs = find(get(data,"contents"),"tabs");
    if (!tabs) return "channel_tabs_missing";
    string suffix = tab == "live" ? "/streams" : tab == "playlists" ? "/playlists" : "/videos";
    for (const auto& t : array(*tabs)) {
      const auto& r = get(t,"tabRenderer");
      string url;
      if (const auto* u = find(get(r,"endpoint"),"webCommandMetadata")) url = str(*u,"url");
      string title = text(get(r,"title"));
      bool match = url.size() >= suffix.size() && url.compare(url.size()-suffix.size(),suffix.size(),suffix) == 0;
      if (url.empty()) match = tab == "live" ? title == "Live" || title == "Ao vivo" : tab == "playlists" ? title == "Playlists" : title == "Videos" || title == "Vídeos";
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
  // Chamar no worker de navegação ao receber/renovar/remover o token da F11.
  bool setToken(const string& access) {
    if (access.size() > 8192 || !std::all_of(access.begin(),access.end(),[](unsigned char c) { return c > 32 && c < 127; })) return false;
    if (token != access) { token = access; tokenRejected = musicTokenRejected = false; authError.clear(); lists.clear(); web = {}; music = {}; accountMusic = {}; }
    return true;
  }
  Page browse(const std::map<string,string>& q) {
    auto value = [&](const char* name) { auto it = q.find(name); return it == q.end() ? string{} : it->second; };
    string kind = value("kind"), id = value("id"), search = value("q"), tab = value("tab"), filter = value("filter");
    language = localeLanguage(value("lang"));
    bool isMusic = kind != "channel" && kind != "playlist";
    Page failed;
    if ((kind == "channel" || kind == "artist") ? !channelId(id) : kind == "radio" ? !videoId(id) : (kind == "playlist" || kind == "music_playlist" || kind == "album") ? !identifier(id) : false) { failed.error = "invalid_navigation"; return failed; }
    string key = kind + "|" + id + "|" + search + "|" + tab + "|" + filter + "|" + language;
    int offset = std::max(0,std::atoi(value("offset").c_str()));
    if (lists.size() >= 12 && !lists.count(key)) lists.erase(lists.begin());
    auto& list = lists[key];
    if (!offset && (value("fresh") == "1" || std::time(nullptr)-list.created > 300)) list = {};
    if (auto it = list.pages.find(offset); it != list.pages.end()) return it->second;
    if (offset && (offset != list.offset || list.next.empty())) { failed.error = "continuation_expired"; return failed; }
    Object args; string route = "browse";
    if (offset) args["continuation"] = J(list.next);
    if (kind == "music_search") {
      route = "search";
      if (!offset) {
        args["query"] = J(search);
        static const char* params[] = {"","EgWKAQIIAQ%3D%3D","EgWKAQIYAQ%3D%3D","EgWKAQIgAQ%3D%3D"};
        int n = std::atoi(filter.c_str());
        if (n > 0 && n < 4) args["params"] = J(string(params[n]));
      }
    } else if (kind == "radio") {
      route = "next";
      if (!offset) { args["videoId"] = J(id); args["playlistId"] = J("RDAMVM"+id); }
    } else if (!offset) {
      string browseId = id;
      if (kind == "music") browseId = search == "charts" ? "FEmusic_explore" : "FEmusic_home";
      if ((kind == "playlist" || kind == "music_playlist") && id.compare(0,2,"VL") != 0) browseId = "VL"+id;
      args["browseId"] = J(browseId);
    }
    J data; failed.error = post(isMusic,route,args,data);
    if (!failed.error.empty()) return failed;
    if (!offset && kind == "channel") failed.error = channelTab(data,tab,isMusic);
    if (!offset && kind == "music" && search == "charts") {
      const auto* browse = findValue(data,"browseId","FEmusic_charts");
      if (!browse || str(*browse,"browseId") != "FEmusic_charts") failed.error = "charts_unavailable";
      else {
        Object charts; charts["browseId"] = J(string("FEmusic_charts"));
        if (!str(*browse,"params").empty()) charts["params"] = get(*browse,"params");
        failed.error = post(true,"browse",charts,data);
      }
    }
    if (!failed.error.empty()) return failed;
    Page p = decode(data,isMusic);
    if (!p.error.empty()) return p;
    if (kind == "channel") p.channelId = id;
    if (!offset) { list.header = p; list.header.items.clear(); list.created = std::time(nullptr); }
    else { p.title = list.header.title; p.subtitle = list.header.subtitle; p.image = list.header.image; p.banner = list.header.banner; p.channelId = list.header.channelId; p.subscribed = list.header.subscribed; }
    if (kind == "music" && search == "mixes") p.items.erase(std::remove_if(p.items.begin(),p.items.end(),[](const Item& i) { return i.type != "playlist"; }),p.items.end());
    p.items.erase(std::remove_if(p.items.begin(),p.items.end(),[&](const Item& i) { return !list.seen.insert(i.type+i.id).second; }),p.items.end());
    if (++list.count >= 40 || list.seen.size() >= 600 || p.items.empty() || !list.tokens.insert(p.continuation).second) p.continuation.clear();
    list.next = p.continuation; list.offset += int(p.items.size()); list.pages[offset] = p;
    return p;
  }
  string handle(const string& path) {
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
      auto e = post(true,"next",args,data);
      if (!e.empty()) return "error\t"+e+"\n";
      const auto* tabs = find(data,"tabs"); string id;
      if (tabs) for (const auto& t : array(*tabs)) {
        const auto* ep = find(get(t,"tabRenderer"),"browseEndpoint");
        if (ep && str(*ep,"browseId").compare(0,4,"MPLY") == 0) { id = str(*ep,"browseId"); break; }
      }
      if (id.empty()) return "ok\t\n";
      args.clear(); args["browseId"] = J(id);
      e = post(true,"browse",args,data);
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
      if (queue.items.size() > 256) queue.items.resize(256);
      queue.continuation.clear(); return wire(queue);
    }
    return wire(browse(q));
  }
};
}
