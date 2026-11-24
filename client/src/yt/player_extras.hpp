#pragma once
#include "catalog.hpp"
#include "locale.hpp"
#include <map>

namespace yt::extras {
using namespace catalog;
struct Comment { string id, author, text, likes, time; };
struct Comments { std::vector<Comment> items; string continuation, error; };
struct Votes { int64_t likes = 0, dislikes = 0; bool available = false; };

inline string bounded(string s, size_t limit) {
  s = clean(std::move(s));
  if (s.size() > limit) {
    size_t end = limit;
    while (end && (static_cast<unsigned char>(s[end]) & 0xc0) == 0x80) --end;
    s.resize(end);
  }
  return s;
}
inline string token(const J& v) {
  if (const auto* cmd = find(v,"continuationCommand")) return str(*cmd,"token");
  if (const auto* next = find(v,"nextContinuationData")) return str(*next,"continuation");
  return {};
}
inline void threads(const J& v, const J& root, Comments& out, std::set<string>& seen) {
  if (out.items.size() >= 60) return;
  if (v.is<Object>()) {
    if (v.contains("commentThreadRenderer")) {
      const auto& thread = get(v,"commentThreadRenderer");
      Comment c;
      if (const auto* old = find(get(thread,"comment"),"commentRenderer")) {
        c = {str(*old,"commentId"),text(get(*old,"authorText")),text(get(*old,"contentText")),
             text(get(*old,"voteCount")),text(get(*old,"publishedTimeText"))};
      } else if (const auto* initial = find(thread,"commentViewModel")) {
        const J* view=initial;
        if (!view->contains("commentKey")) view=find(*view,"commentViewModel");
        if (!view) return;
        const auto& mutations = array(get(get(get(root,"frameworkUpdates"),"entityBatchUpdate"),"mutations"));
        for (const auto& m : mutations) if (str(m,"entityKey") == str(*view,"commentKey")) {
          const auto& entity = get(get(m,"payload"),"commentEntityPayload");
          const auto& props = get(entity,"properties");
          c = {str(props,"commentId"),str(get(entity,"author"),"displayName"),text(get(props,"content")),
               str(get(entity,"toolbar"),"likeCountNotliked"),str(props,"publishedTime")};
          if (c.id.empty()) c.id = str(*view,"commentId");
          break;
        }
      }
      if (!c.id.empty() && seen.insert(c.id).second) {
        c.author=bounded(c.author,240); c.text=bounded(c.text,16000);
        c.likes=bounded(c.likes.empty() ? "0" : c.likes,80); c.time=bounded(c.time,160);
        out.items.push_back(std::move(c));
      }
      return; // A continuação das respostas não pode virar a próxima página de comentários.
    }
    if (v.contains("continuationItemRenderer")) {
      auto t = token(get(v,"continuationItemRenderer"));
      if (t.size() <= 16384) out.continuation = std::move(t);
      return;
    }
    for (const auto& kv : v.get<Object>()) threads(kv.second,root,out,seen);
  } else for (const auto& x : array(v)) threads(x,root,out,seen);
}
inline Comments parseComments(const J& root) {
  Comments out; std::set<string> seen;
  if (const auto* section = findValue(root,"sectionIdentifier","comment-item-section")) threads(*section,root,out,seen);
  // Cabeçalho e corpo podem ter o mesmo targetId em comandos separados.
  for (const char* k : {"onResponseReceivedEndpoints","onResponseReceivedActions"}) {
    for (const auto& action : array(get(root,k))) {
      for (const char* cmd : {"reloadContinuationItemsCommand","appendContinuationItemsAction"}) {
        const auto& value = get(action,cmd);
        if (str(value,"targetId").find("comments-section") == 0) threads(value,root,out,seen);
      }
    }
  }
  return out;
}

// GetCommentsSectionParams do InnerTube (mesmo esquema publicado pelo YouTube.js).
inline string firstToken(const string& id) {
  auto field=[](unsigned key,const string& bytes) { return string(1,char((key<<3)|2))+char(bytes.size())+bytes; };
  string opts=field(4,id)+string("\x78\x02",2);
  string payload=field(2,field(2,id))+string("\x18\x06",2)+field(6,field(4,opts)+field(8,"comments-section"));
  static const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  string out; unsigned bits=0; int n=0;
  for (unsigned char c : payload) { bits=(bits<<8)|c; n+=8; while (n>=6) { n-=6; out+=alphabet[(bits>>n)&63]; } }
  if (n) out+=alphabet[(bits<<(6-n))&63];
  while (out.size()%4) out+='=';
  string escaped;
  for (char c : out) { if (c=='+') escaped+="%2B"; else if (c=='/') escaped+="%2F"; else if (c=='=') escaped+="%3D"; else escaped+=c; }
  return escaped;
}

class Client {
  Transport& net;
  J context;
  string language;
  struct Cache { Votes votes; uint64_t expires; };
  std::map<string,Cache> cache;
  Response next(const string& fields, const string& lang) {
    if (!context.is<Object>() || language != lang) {
      Request page; page.url="https://www.youtube.com/?hl="+lang;
      page.agent="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36";
      auto r = net.send(page);
      if (failureOf(r) != Failure::NONE) return r;
      auto key = r.body.find("\"INNERTUBE_CONTEXT\"");
      auto begin = key == string::npos ? string::npos : r.body.find('{',key+19);
      auto end = jsonEnd(r.body,begin);
      if (end == string::npos || end-begin > 65536 || !parse(r.body.substr(begin,end-begin),context) ||
          str(get(context,"client"),"clientName") != "WEB") return {0,{},"comments_context"};
      context.get<Object>()["client"].get<Object>()["hl"] = J(lang);
      context.get<Object>()["client"].get<Object>()["gl"] = J(string(localeRegion(lang)));
      language=lang;
    }
    Request r; r.url="https://www.youtube.com/youtubei/v1/next?prettyPrint=false";
    r.body="{\"context\":"+context.serialize()+","+fields+"}";
    r.headers={"X-Youtube-Client-Name: 1","X-Youtube-Client-Version: "+str(get(context,"client"),"clientVersion")};
    return net.send(r);
  }
public:
  explicit Client(Transport& t) : net(t) {}
  Comments comments(const string& id, const string& continuation, const string& lang) {
    if (!videoId(id) || continuation.size()>16384) return {{},{},"comments_input"};
    string cursor=continuation.empty() ? firstToken(id) : continuation;
    Comments out; std::set<string> visited;
    for (int page=0;page<3;++page) {
      visited.insert(cursor);
      auto r=next("\"continuation\":"+J(cursor).serialize(),lang); J root;
      if (failureOf(r)!=Failure::NONE || !parse(r.body,root) || root.contains("error")) return {{},{},"comments_request"};
      out=parseComments(root);
      if (visited.count(out.continuation)) out.continuation.clear();
      if (!out.items.empty() || out.continuation.empty()) break;
      cursor=out.continuation;
    }
    return out;
  }
  Votes votes(const string& id, uint64_t now) {
    if (!videoId(id)) return {};
    auto it=cache.find(id);
    if (it!=cache.end() && now<it->second.expires) return it->second.votes;
    auto r=net.request("https://returnyoutubedislikeapi.com/votes?videoId="+id);
    J root; Votes out;
    if (failureOf(r)==Failure::NONE && parse(r.body,root) && str(root,"id")==id && !flag(root,"deleted")) {
      auto number=[&](const char* k) { const auto& n=get(root,k); return n.is<double>() && std::isfinite(n.get<double>()) &&
          n.get<double>()>=0 && n.get<double>()<=1e12 && std::floor(n.get<double>())==n.get<double>(); };
      if (number("likes") && number("dislikes")) out={int64_t(get(root,"likes").get<double>()),int64_t(get(root,"dislikes").get<double>()),true};
    }
    if (cache.size()>=32) cache.erase(cache.begin());
    cache[id]={out,now+(out.available ? 300000 : 30000)};
    return out;
  }
};
}
