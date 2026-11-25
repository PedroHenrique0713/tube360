#include "common.hpp"
#include "monocypher-ed25519.h"
#include <mbedtls/sha256.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace updates {
const J& get(const J& j,const string& k) { static const J nil; return j.is<O>() ? j.get(k) : nil; }
string text(const J& j) { return j.is<string>() ? j.get<string>() : ""; }
string text(const J& j,const string& k) { return text(get(j,k)); }
int number(const J& j,int low,int high) {
    if (!j.is<double>()) return -1;
    double n=j.get<double>();
    return std::isfinite(n) && n>=low && n<=high && std::floor(n)==n ? int(n) : -1;
}
bool fields(const J& j,std::initializer_list<const char*> keys) {
    if (!j.is<O>() || j.get<O>().size()!=keys.size()) return false;
    return std::all_of(keys.begin(),keys.end(),[&](const char* k){ return j.contains(k); });
}
bool parse(const string& s,J& j) {
    if (s.empty() || s.size()>maxDocument*5) return false;
    int depth=0, nodes=0; bool quote=false, escape=false;
    for (unsigned char c:s) {
        if (quote) { if (escape) escape=false; else if (c=='\\') escape=true; else if (c=='"') quote=false; }
        else if (c=='"') quote=true;
        else if (c=='{' || c=='[') { if (++depth>12 || ++nodes>4096) return false; }
        else if (c=='}' || c==']') { if (--depth<0) return false; }
        else if (c==',' && ++nodes>4096) return false;
    }
    if (quote || depth) return false;
    try { return picojson::parse(j,s).empty() && j.is<O>(); } catch (...) { return false; }
}
bool ascii(const string& s,size_t max,bool empty) {
    return (empty || !s.empty()) && s.size()<=max && std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=32 && c<127;});
}
string host(const string& u) {
    if (u.compare(0,8,"https://") || u.size()>8192 || u.find_first_of("@\\#\r\n\t ")!=string::npos) return {};
    if (std::any_of(u.begin(),u.end(),[](unsigned char c){return c<33 || c>126;})) return {};
    auto end=u.find_first_of("/?",8); if (end==string::npos) end=u.size();
    auto h=u.substr(8,end-8);
    if (h.empty() || h.size()>253 || h.front()=='.' || h.back()=='.' ||
        h.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-")!=string::npos || h.find("..")!=string::npos) return {};
    return h;
}
static bool domain(const string& h,const string& d) { return h==d || (h.size()>d.size() && h.compare(h.size()-d.size(),d.size(),d)==0 && h[h.size()-d.size()-1]=='.'); }
bool youtubeUrl(const string& u) {
    auto h=host(u);
    for (const auto* d:{"youtube.com","googlevideo.com","ytimg.com","ggpht.com","googleusercontent.com","googleapis.com","google.com"}) if (domain(h,d)) return true;
    return false;
}
bool updateUrl(const string& u) {
#ifdef TUBE360_UPDATE_TEST_CONFIG
    const string local=TUBE360_UPDATE_TEST_BASE;
    if(u.compare(0,local.size(),local)==0 && ascii(u,2048) &&
        u.find_first_of("@\\#%?")==string::npos && u.find("..") == string::npos)return true;
#endif
    auto h=host(u);
    auto path=u.substr(0,u.find('?'));
    if(path.find("/../")!=string::npos || path.find("/./")!=string::npos || path.find('%')!=string::npos)return false;
    if (h=="raw.githubusercontent.com") return u.compare(0,string("https://raw.githubusercontent.com/PedroHenrique0713/tube360/").size(),"https://raw.githubusercontent.com/PedroHenrique0713/tube360/")==0;
    if (h=="github.com") return u.compare(0,string("https://github.com/PedroHenrique0713/tube360/releases/").size(),"https://github.com/PedroHenrique0713/tube360/releases/")==0;
    return h=="release-assets.githubusercontent.com" || h=="objects.githubusercontent.com";
}
string read(const string& path,size_t max) {
    auto* f=std::fopen(path.c_str(),"rb"); if (!f) return {};
    string s; char b[8192]; size_t n;
    while ((n=std::fread(b,1,sizeof(b),f))) { if (s.size()+n>max) { std::fclose(f); return {}; } s.append(b,n); }
    bool ok=!std::ferror(f); std::fclose(f); return ok ? s : "";
}
bool write(const string& path,const string& s) {
    string temp=path+".tmp";
    int fd=::open(temp.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600); if (fd<0) return false;
    bool ok=true; size_t at=0;
    while (at<s.size()) { auto n=::write(fd,s.data()+at,s.size()-at); if (n<=0) {ok=false; break;} at+=size_t(n); }
#ifndef __XSDK__
    if (ok && ::fsync(fd)) ok=false;
#endif
    if (::close(fd)) ok=false;
    if (ok) ok=std::rename(temp.c_str(),path.c_str())==0;
    if (!ok) std::remove(temp.c_str());
    return ok;
}
bool exists(const string& p) { auto* f=std::fopen(p.c_str(),"rb"); if (!f) return false; std::fclose(f); return true; }
static string hex(const unsigned char* b,size_t n) { const char* d="0123456789abcdef"; string s; for(size_t i=0;i<n;i++){s+=d[b[i]>>4];s+=d[b[i]&15];} return s; }
string sha256(const string& s) { unsigned char h[32]; return mbedtls_sha256(reinterpret_cast<const unsigned char*>(s.data()),s.size(),h,0)==0 ? hex(h,32) : ""; }
string fileHash(const string& path,const Trace& trace) {
    if(trace)trace("hash inicio: "+path);
    auto* f=std::fopen(path.c_str(),"rb"); if (!f) {if(trace)trace("hash erro: abrir "+path);return {};}
    mbedtls_sha256_context c; mbedtls_sha256_init(&c); bool ok=mbedtls_sha256_starts(&c,0)==0;
    unsigned char b[32768], h[32]; size_t n=0,total=0;
    size_t report=1024*1024;
    while (ok && (n=std::fread(b,1,sizeof(b),f))) {
        total+=n; ok=total<=64*1024*1024 && mbedtls_sha256_update(&c,b,n)==0;
        if(trace && total>=report){trace("hash bytes="+std::to_string(total)+": "+path);report+=1024*1024;}
    }
    ok=ok && !std::ferror(f) && mbedtls_sha256_finish(&c,h)==0;
    std::fclose(f); mbedtls_sha256_free(&c);
    if(trace)trace(string("hash ")+(ok?"fim":"erro")+" bytes="+std::to_string(total)+": "+path);
    return ok ? hex(h,32) : "";
}
static bool unhex(string s,unsigned char* b,size_t n) {
    if (!s.empty() && s.back()=='\n') s.pop_back();
    if (s.size()!=n*2 || s.find_first_not_of("0123456789abcdef")!=string::npos) return false;
    auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(size_t i=0;i<n;i++) b[i]=static_cast<unsigned char>((digit(s[i*2])<<4)|digit(s[i*2+1]));
    return true;
}
bool verify(const string& bytes,const string& sig,const string& key) {
    unsigned char s[64],k[32];
    return bytes.size()<=maxDocument && unhex(sig,s,64) && unhex(key,k,32) &&
        crypto_ed25519_check(s,k,reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size())==0;
}
string envelope(const string& bytes,const string& signature) { return J(O{{"bytes",J(bytes)},{"signature",J(signature)}}).serialize(); }
bool unpack(const string& raw,string& bytes,string& signature) {
    J j; if (!parse(raw,j) || !fields(j,{"bytes","signature"})) return false;
    bytes=text(j,"bytes"); signature=text(j,"signature"); return !bytes.empty() && bytes.size()<=maxDocument;
}
}
