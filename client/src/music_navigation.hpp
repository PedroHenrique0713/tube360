#pragma once
#include "navigation.hpp"

struct MusicNavigation : Navigation {
  struct Decoration { std::string banner; std::vector<std::string> headings; };
  std::map<uint64_t,Decoration> decorations;
  bool localQueue = false;
  size_t queueIndex = 0;
  std::string queueStart;
  bool nativePage() const { return !pages.empty() && decorations.count(pages.back().stamp); }
  void clearLocalQueue() { localQueue = false; ownsQueue = false; queue.clear(); queueStart.clear(); }
  void arrived(const Result& r, gfx::Gfx& g, i18n::Lang lang) {
    Navigation::arrived(r,g,lang);
    for (auto it = decorations.begin(); it != decorations.end();) {
      bool present = std::any_of(pages.begin(),pages.end(),[&](const NavPage& p) { return p.stamp == it->first; });
      if (!present) it = decorations.erase(it); else ++it;
    }
    if (!r.catalog) return;
    for (const auto& p : pages) if (p.stamp == r.stamp) {
      auto& d = decorations[p.stamp];
      auto line = net::split(r.feed.title.substr(0,r.feed.title.find('\n')),'\t');
      if (line.size() > 7 && !line[7].empty()) d.banner = "!"+line[7];
      for (const auto& item : p.items) d.headings.push_back(gfx::ellipsize(g,ui::F_CHAN,item.section,1020));
    }
  }
  void draw(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, uint64_t frame, uint64_t now, i18n::Lang lang) {
    if (!pages.empty()) {
      auto d = decorations.find(page().stamp);
      if (d != decorations.end() && !d->second.banner.empty()) {
        if (const auto* art = thumbs.get(worker,d->second.banner,frame,true)) {
          gfx::image(g,art->tex,96,0,1184,112);
          gfx::rect(g,96,0,1184,112,{0,0,0,.68f});
        }
      }
    }
    Navigation::draw(g,thumbs,worker,frame,now,lang);
    if (!pages.empty() && page().music) {
      auto d = decorations.find(page().stamp);
      size_t index = size_t(std::max(0,page().sel))/4*4;
      if (d != decorations.end() && index < d->second.headings.size()) gfx::text(g,ui::F_CHAN,d->second.headings[index],136,220,ui::MUTED);
    }
  }
};
