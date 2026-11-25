#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "fribidi.h"

namespace textlayout {
inline uint32_t decode(const std::string& s, size_t& i) {
  unsigned char c = (unsigned char)s[i++];
  if (c < 0x80) return c;
  int n = c >= 0xC2 && c <= 0xDF ? 1 : c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xF0 && c <= 0xF4 ? 3 : 0;
  if (!n) return 0xFFFD;
  uint32_t cp = c & (0x7F >> (n + 1));
  for (int k = 0; k < n; ++k) {
    if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
  }
  return cp < (n == 1 ? 0x80u : n == 2 ? 0x800u : 0x10000u) || cp > 0x10FFFF ||
         (cp >= 0xD800 && cp <= 0xDFFF) ? 0xFFFD : cp;
}
inline bool arabic(uint32_t cp) {
  return (cp >= 0x600 && cp <= 0x6FF) || (cp >= 0x750 && cp <= 0x77F) ||
         (cp >= 0x8A0 && cp <= 0x8FF) || (cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF);
}
inline bool mark(uint32_t cp) { return fribidi_get_bidi_type(cp) == FRIBIDI_TYPE_NSM; }
inline bool invisible(uint32_t cp) {
  auto t = fribidi_get_bidi_type(cp);
  return FRIBIDI_IS_EXPLICIT_OR_BN(t) || FRIBIDI_IS_ISOLATE(t) || cp == 0x200E || cp == 0x200F || cp == 0x61C;
}
struct Line { std::vector<FriBidiChar> glyphs; bool rtl = false, hasArabic = false; };
inline Line shape(const std::string& s) {
  Line out;
  for (size_t i = 0; i < s.size();) {
    auto cp = decode(s, i); out.glyphs.push_back(cp); out.hasArabic |= arabic(cp);
  }
  if (out.glyphs.empty()) return out;
  // Cada linha chega em ordem lógica; números, parênteses e isoladores seguem UAX #9.
  FriBidiParType base = FRIBIDI_PAR_ON;
  std::vector<FriBidiChar> visual(out.glyphs.size());
  if (fribidi_log2vis(out.glyphs.data(), (FriBidiStrIndex)out.glyphs.size(), &base,
                      visual.data(), nullptr, nullptr, nullptr)) out.glyphs.swap(visual);
  out.rtl = FRIBIDI_IS_RTL(base);
  out.glyphs.erase(std::remove_if(out.glyphs.begin(), out.glyphs.end(), invisible), out.glyphs.end());
  return out;
}
// Cache por texto, independente da fonte/resolução. Estável: nenhum shaping nem alocação por quadro.
struct Cache {
  static constexpr size_t limit = 512, byteLimit = 512 * 1024;
  std::map<std::string, Line> lines;
  size_t bytes = 0;
  Line scratch;
  const Line& get(const std::string& s) {
    auto it = lines.find(s); if (it != lines.end()) return it->second;
    size_t cost = s.size() * 5 + 128;
    if (cost > byteLimit) { scratch = shape(s); return scratch; }
    while (!lines.empty() && (lines.size() >= limit || bytes + cost > byteLimit)) {
      bytes -= lines.begin()->first.size() * 5 + 128; lines.erase(lines.begin());
    }
    bytes += cost; return lines.emplace(s, shape(s)).first->second;
  }
};
struct Prepared {
  int font=0;
  std::string logical;
  Line visual;
};
template<class Supported> inline Prepared prepare(int font,const std::string& s,Supported supported) {
  Prepared out; out.font=font; bool omitted=false;
  for(size_t i=0;i<s.size();) {
    size_t start=i;auto cp=decode(s,i);
    // ZWJ/ZWNJ e controles bidi participam da composição, mesmo sem tinta no atlas.
    if(invisible(cp) || supported(cp))out.logical.append(s,start,i-start);
    else omitted=true;
  }
  if(omitted) {
    auto first=out.logical.find_first_not_of(" ");
    if(first==std::string::npos)out.logical.clear();
    else out.logical=out.logical.substr(first,out.logical.find_last_not_of(" ")-first+1);
  }
  out.visual=shape(out.logical);
  out.visual.glyphs.erase(std::remove_if(out.visual.glyphs.begin(),out.visual.glyphs.end(),
      [&](uint32_t cp){return !supported(cp);}),out.visual.glyphs.end());
  if(out.visual.glyphs.empty())out.logical.clear();
  return out;
}
// A cobertura varia por fonte. Filtra e compõe só no miss, antes de medir/cortar/desenhar.
struct PreparedCache {
  static constexpr size_t limit=512,byteLimit=512*1024;
  std::map<std::string,std::vector<Prepared>> texts;
  size_t bytes=0,entries=0,builds=0;
  Prepared scratch;
  template<class Supported> const Prepared& get(int font,const std::string& s,Supported supported) {
    auto it=texts.find(s);
    if(it!=texts.end())for(const auto& value:it->second)if(value.font==font)return value;
    ++builds;auto value=prepare(font,s,supported);
    size_t cost=s.size()+value.logical.size()+value.visual.glyphs.capacity()*sizeof(FriBidiChar)+128;
    if(cost>byteLimit){scratch=std::move(value);return scratch;}
    std::string key=s;
    if(entries>=limit || bytes+cost>byteLimit){texts.clear();entries=bytes=0;}
    bytes+=cost;++entries;auto& variants=texts[std::move(key)];variants.push_back(std::move(value));return variants.back();
  }
};
struct Fit {
  int font = 0, maxLines = 0;
  float width = 0;
  std::string shortened;
  std::vector<std::string> lines;
};
struct FitCache {
  static constexpr size_t byteLimit=512*1024;
  std::map<std::string,std::vector<Fit>> texts;
  size_t bytes=0;
  Fit scratch;
  const Fit* find(int font,const std::string& s,float width,int maxLines) const {
    auto it=texts.find(s); if (it==texts.end()) return nullptr;
    for (const auto& fit:it->second) if (fit.font==font && fit.width==width && fit.maxLines==maxLines) return &fit;
    return nullptr;
  }
  const Fit& save(const std::string& s,Fit fit) {
    size_t cost=s.size()+fit.shortened.size()+128;
    for (const auto& line:fit.lines) cost+=line.size()+32;
    if (cost>byteLimit) { scratch=std::move(fit); return scratch; }
    std::string key=s;
    if (texts.size()>=128 || bytes+cost>byteLimit) { texts.clear(); bytes=0; }
    bytes+=cost; auto& variants=texts[std::move(key)]; variants.push_back(std::move(fit)); return variants.back();
  }
};
}  // namespace textlayout
