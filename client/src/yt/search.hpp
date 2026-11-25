#pragma once
#include "client.hpp"
#include "locale.hpp"
#ifndef PICOJSON_USE_LOCALE
#define PICOJSON_USE_LOCALE 0
#endif
#include "picojson.h"
#include <algorithm>

namespace yt::search {
struct Filters {
    // Índices da interface; os valores do protobuf são independentes.
    int type=0, duration=0, date=0, order=0;
    bool operator==(const Filters& b) const { return type==b.type && duration==b.duration && date==b.date && order==b.order; }
    bool operator!=(const Filters& b) const { return !(*this==b); }
    void normalize() {
        type=std::clamp(type,0,4); duration=std::clamp(duration,0,3);
        date=std::clamp(date,0,4); order=std::clamp(order,0,2);
        if (type==2 || type==3) duration=date=0;
        if (type==4) duration=0;
    }
    void change(int row,int direction) {
        int* values[]={&type,&duration,&date,&order};
        const int counts[]={5,4,5,3};
        if (row<0 || row>3 || disabled(row)) return;
        *values[row]=(*values[row]+counts[row]+direction)%counts[row];
        if (row==1 && duration && type==0) type=1;
        normalize();
    }
    bool disabled(int row) const { return (row==1 && (type==2 || type==3 || type==4)) || (row==2 && (type==2 || type==3)); }
    std::string params() const {
        Filters f=*this; f.normalize();
        std::string inner, bytes;
        auto field=[](std::string& out,int tag,int value) { out+=char(tag<<3); out+=char(value); };
        if (f.date) field(inner,1,f.date+1);
        if (f.type && f.type!=4) field(inner,2,f.type);
        const int durations[]={0,4,5,2};
        if (f.duration) field(inner,3,durations[f.duration]);
        if (f.type==4) field(inner,8,1);
        if (f.order) field(bytes,1,f.order==1 ? 2 : 3);
        if (!inner.empty()) { bytes+=char(0x12); bytes+=char(inner.size()); bytes+=inner; }
        static const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (size_t i=0;i<bytes.size();i+=3) {
            unsigned n=unsigned((unsigned char)bytes[i])<<16;
            if (i+1<bytes.size()) n|=unsigned((unsigned char)bytes[i+1])<<8;
            if (i+2<bytes.size()) n|=(unsigned char)bytes[i+2];
            out+=alphabet[n>>18]; out+=alphabet[(n>>12)&63];
            out+=i+1<bytes.size() ? alphabet[(n>>6)&63] : '=';
            out+=i+2<bytes.size() ? alphabet[n&63] : '=';
        }
        // InnerTube recebe o base64 percent-encoded, como searchEndpoint.params da WEB.
        std::string encoded;
        for (char c:out) encoded+=c=='=' ? "%3D" : c=='+' ? "%2B" : c=='/' ? "%2F" : std::string(1,c);
        return encoded;
    }
};
inline std::string encode(const std::string& s) {
    static const char* hex="0123456789ABCDEF";
    std::string out;
    for (unsigned char c:s) {
        if ((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_' || c=='.' || c=='~') out+=char(c);
        else { out+='%'; out+=hex[c>>4]; out+=hex[c&15]; }
    }
    return out;
}
inline std::string url(const std::string& query,const std::string& lang) {
    auto language=localeLanguage(lang);
    return std::string("https://suggestqueries-clients6.youtube.com/complete/search?client=youtube&ds=yt&hl=")+language+"&gl="+localeRegion(language)+"&q="+encode(query);
}
inline std::vector<std::string> suggestions(std::string body,const std::string& query) {
    using J=picojson::value; using Array=picojson::array;
    if (body.size()>32768) return {};
    const std::string prefix="window.google.ac.h(";
    if (body.compare(0,prefix.size(),prefix)==0) {
        auto end=body.find_last_not_of(" \r\n\t;");
        if (end==std::string::npos || body[end]!=')') return {};
        body=body.substr(prefix.size(),end-prefix.size());
    }
    int depth=0,nodes=0; bool quoted=false,escaped=false;
    for (char c:body) {
        if (quoted) { if (escaped) escaped=false; else if (c=='\\') escaped=true; else if (c=='"') quoted=false; }
        else if (c=='"') quoted=true;
        else if (c=='[' || c=='{') { if (++depth>12 || ++nodes>1024) return {}; }
        else if (c==',' && ++nodes>1024) return {};
        else if ((c==']' || c=='}') && --depth<0) return {};
    }
    if (depth || quoted) return {};
    J data; std::string error;
    try { picojson::parse(data,body.begin(),body.end(),&error); } catch (const std::exception&) { return {}; }
    if (!error.empty() || !data.is<Array>()) return {};
    const auto& root=data.get<Array>();
    if (root.size()<2 || !root[0].is<std::string>() || root[0].get<std::string>()!=query || !root[1].is<Array>()) return {};
    std::vector<std::string> out;
    for (const auto& row:root[1].get<Array>()) {
        const J* text=&row;
        if (row.is<Array>()) { if (row.get<Array>().empty()) continue; text=&row.get<Array>()[0]; }
        if (!text->is<std::string>()) continue;
        auto value=text->get<std::string>();
        if (value.empty() || value.size()>256 || std::any_of(value.begin(),value.end(),[](unsigned char c) { return c<32 || c==127; })) continue;
        if (std::find(out.begin(),out.end(),value)==out.end()) out.push_back(std::move(value));
        if (out.size()==8) break;
    }
    return out;
}
inline std::vector<std::string> fetch(Transport& net,const std::string& query,const std::string& lang) {
    if (query.empty() || query.size()>256 || net.cancelled()) return {};
    Request request; request.url=url(query,lang); request.maxBytes=32768;
    request.connectTimeoutMs=1500; request.timeoutMs=2500;
    request.headers={"Cookie:","Accept: application/json"};
    auto response=net.send(request);
    if (response.status!=200 || !response.error.empty() || net.cancelled()) return {};
    return suggestions(std::move(response.body),query);
}
}
