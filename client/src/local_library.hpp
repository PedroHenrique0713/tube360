#pragma once
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include "picojson.h"

namespace local {
struct Entry {
  std::string id, title, channel;
  int duration = 0;
  double position = 0;
  uint64_t order = 0;
};
struct Library {
  static constexpr size_t laterLimit = 200, historyLimit = 500, byteLimit = 1024*1024;
  std::vector<Entry> later, history;
  uint64_t revision = 1, sequence = 0;
  static bool validId(const std::string& id) {
    return id.size()==11 && id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")==std::string::npos;
  }
  static std::string label(std::string s, size_t limit) {
    for (auto& c:s) if ((unsigned char)c<32) c=' ';
    if (s.size()>limit) { size_t n=limit; while(n && ((unsigned char)s[n]&0xc0)==0x80)--n; s.resize(n); }
    return s;
  }
  static bool resumable(double pos, double dur) {
    return std::isfinite(pos) && std::isfinite(dur) && dur>20 && pos>=10 && pos<dur-10 && pos/dur<0.95;
  }
  static void erase(std::vector<Entry>& list, const std::string& id) {
    list.erase(std::remove_if(list.begin(),list.end(),[&](const Entry& e){return e.id==id;}),list.end());
  }
  static const Entry* find(const std::vector<Entry>& list, const std::string& id) {
    auto it=std::find_if(list.begin(),list.end(),[&](const Entry& e){return e.id==id;}); return it==list.end()?nullptr:&*it;
  }
  Entry clean(Entry e) { e.title=label(e.title,384); e.channel=label(e.channel,192); e.duration=std::clamp(e.duration,0,604800); e.order=++sequence; return e; }
  bool contains(const std::string& id) const { return find(later,id)!=nullptr; }
  double position(const std::string& id) const { auto e=find(history,id); return e ? e->position : 0; }
  bool toggle(Entry e) {
    if (!validId(e.id)) return false;
    bool remove=contains(e.id); erase(later,e.id);
    if (!remove) later.insert(later.begin(),clean(std::move(e)));
    if(later.size()>laterLimit)later.resize(laterLimit);
    ++revision; return true;
  }
  void record(Entry e, bool ended=false) {
    if (!validId(e.id) || !std::isfinite(e.position)) return;
    if (ended || !resumable(e.position,e.duration)) {
      if (find(history,e.id)) { erase(history,e.id); ++revision; }
      return;
    }
    auto old=find(history,e.id);
    if (old && std::abs(old->position-e.position)<1 && old->duration==e.duration) return;
    erase(history,e.id); history.insert(history.begin(),clean(std::move(e)));
    if(history.size()>historyLimit)history.resize(historyLimit);
    ++revision;
  }
  void clear(bool watched) { (watched?history:later).clear(); ++revision; }
  std::string encode() const {
    using J=picojson::value; using O=picojson::object; using A=picojson::array;
    auto entries=[](const std::vector<Entry>& list) { A a; for(const auto& e:list)a.emplace_back(O{{"id",J(e.id)},{"title",J(e.title)},{"channel",J(e.channel)},{"duration",J(double(e.duration))},{"position",J(e.position)},{"order",J(double(e.order))}}); return J(a); };
    return J(O{{"version",J(1.0)},{"later",entries(later)},{"history",entries(history)}}).serialize();
  }
  bool decode(const std::string& raw) {
    using J=picojson::value; using O=picojson::object; using A=picojson::array;
    // Impede profundidade arbitrária antes de entrar no parser recursivo.
    int depth=0; bool quoted=false,escaped=false;
    if(raw.size()>byteLimit)return false;
    for(char c:raw) { if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;}else if(c=='"')quoted=true;else if(c=='{'||c=='['){if(++depth>8)return false;}else if(c=='}'||c==']'){if(--depth<0)return false;} }
    J j; std::string error;
    auto end=picojson::parse(j,raw.begin(),raw.end(),&error);
    if(std::any_of(end,raw.end(),[](char c){return c!=' ' && c!='\r' && c!='\n' && c!='\t';}))return false;
    if(!error.empty() || !j.is<O>() || !j.get("version").is<double>() || j.get("version").get<double>()!=1)return false;
    Library next;
    auto read=[&](const char* key,std::vector<Entry>& list,size_t limit,bool watched) {
      const J& a=j.get(key); if(!a.is<A>() || a.get<A>().size()>limit)return false;
      for(const auto& v:a.get<A>()) {
        if(!v.is<O>())return false;
        for(auto k:{"id","title","channel"})if(!v.get(k).is<std::string>())return false;
        for(auto k:{"duration","position","order"})if(!v.get(k).is<double>() || !std::isfinite(v.get(k).get<double>()))return false;
        double d=v.get("duration").get<double>(),pos=v.get("position").get<double>(),o=v.get("order").get<double>();
        Entry e{v.get("id").get<std::string>(),label(v.get("title").get<std::string>(),384),label(v.get("channel").get<std::string>(),192)};
        if(!validId(e.id) || d<0 || d>604800 || std::floor(d)!=d || pos<0 || o<0 || o>9007199254740991.0 || std::floor(o)!=o || find(list,e.id))return false;
        e.duration=int(d); e.position=pos; e.order=uint64_t(o);
        next.sequence=std::max(next.sequence,e.order);
        if(!watched || resumable(pos,d))list.push_back(std::move(e));
      }
      std::stable_sort(list.begin(),list.end(),[](const Entry& a,const Entry& b){return a.order>b.order;}); return true;
    };
    if(!read("later",next.later,laterLimit,false) || !read("history",next.history,historyLimit,true))return false;
    next.revision=revision+1; *this=std::move(next); return true;
  }
  enum Load { Missing, Loaded, Corrupt };
  Load load(const std::string& path) {
    FILE* f=std::fopen(path.c_str(),"rb"); if(!f)return errno==ENOENT?Missing:Corrupt;
    std::string raw; char block[4096]; size_t n;
    while((n=std::fread(block,1,sizeof(block),f)) && raw.size()+n<=byteLimit)raw.append(block,n);
    bool ok=!std::ferror(f) && std::feof(f); std::fclose(f);
    return ok && decode(raw)?Loaded:Corrupt;
  }
  bool save(const std::string& path) const {
    const auto raw=encode(); if(raw.size()>byteLimit)return false;
    const auto tmp=path+".tmp";
    int flags=O_WRONLY|O_CREAT|O_TRUNC;
#ifdef O_NOFOLLOW
    flags|=O_NOFOLLOW;
#endif
    int fd=::open(tmp.c_str(),flags,0600); if(fd<0)return false;
    bool ok=true; size_t at=0;
    while(at<raw.size()){auto n=::write(fd,raw.data()+at,raw.size()-at);if(n<0 && errno==EINTR)continue;if(n<=0){ok=false;break;}at+=size_t(n);}
#ifndef __XSDK__
    if(ok && ::fsync(fd))ok=false;
#endif
    if(::close(fd))ok=false;
    if(ok)ok=std::rename(tmp.c_str(),path.c_str())==0;
    if(!ok)std::remove(tmp.c_str());
    return ok;
  }
};
}
