#pragma once
#include "music_shortcut_art.hpp"

struct NavItem {
  std::string type, id, title, channel, image, channelId, section;
  int duration = 0;
  yt::Membership members = yt::Membership::None;
  std::vector<std::string> titleLines; std::string byline;
  Video video() const { Video v; v.id = id; v.title = title; v.channel = channel; v.dur = duration; v.members = members; return v; }
};
struct NavPage {
  yt::search::Filters filters;
  std::string filterLabel;
  std::vector<std::string> filterLines;
  std::string kind, id, query, tab = "videos", title, subtitle, image, channelId, error, displayTitle, displaySub, banner, historyVideo, laterVideo;
  std::vector<NavItem> items, channels;
  bool music = false, direct = false, native = false, loading = true, more = false, subscribed = false;
  std::string musicSource, musicLabel;
  int sel = 0, channelSel = 0, zone = 3, tabSel = 0, offset = 0;
  struct Shelf { int first, end, selected; std::string title; };
  std::vector<Shelf> shelves;
  int shelf = 0;
  bool musicHome() const { return kind == "music"; }
  float scroll = 0;
  uint64_t stamp = 0;
  std::string path() const {
    return "/api/nav?kind=" + kind + "&id=" + net::urlEncode(id) + "&q=" + net::urlEncode(query) + "&tab=" + tab + "&filter=" + std::to_string(tabSel) + (kind=="search" ? "&params="+net::urlEncode(filters.params()) : "");
  }
};
struct Navigation {
  local::Library* local = nullptr;
  bool signedIn = false;
  uint64_t localRevision = 0;
  i18n::Lang localLang = i18n::Lang::Count;
  std::vector<NavPage> pages;
  uint64_t serial = 0, actionStamp = 0;
  bool ownsQueue = false, cancelQueue = false, busy = false, musicSession = false, musicNow = false, lyricsOpen = false, unsubscribe = false;
  std::string actionPath, lyrics, lyricsId;
  std::vector<std::string> lyricLines; int lyricScroll = 0;
  std::vector<NavItem> queue;
  std::string nowId, nowImage, nowTitle, nowArtist, nowError, noLyrics;
  std::vector<std::string> nextTitles;
  K nowErrorKey = K::None;
  int nowSel = 0, nowZone = 0, menuSel = 0;
  bool menu = false;
  NavItem menuItem;
  NavPage& page() { return pages.back(); }
  void load(Worker& worker, i18n::Lang lang, bool fresh = false, bool append = false) {
    auto& p = page(); p.loading = true; p.error.clear(); p.stamp = ++serial;
    p.more = false;
    if (!append) { p.offset = 0; p.items.clear(); p.channels.clear(); p.channelSel = p.shelf = 0; p.shelves.clear(); }
    if (p.kind == "local_later" || p.kind == "local_history" || (p.kind == "library" && !signedIn)) { p.loading=false; localRevision=0; return; }
    std::string path = p.path() + "&lang=" + i18n::code(lang) + "&offset=" + std::to_string(p.offset) + "&fresh=" + (fresh ? "1" : "0");
    Job job{6, path, p.stamp};
    job.query = p.query; job.lang = lang; worker.add(job, true);
  }
  void open(Worker& worker, i18n::Lang lang, std::string kind, std::string id = "", std::string query = "", bool music = false, yt::search::Filters filters = {}) {
    // As telas guardam seleção e rolagem; B não refaz a consulta de onde o usuário veio.
    if (pages.size() >= 12) pages.erase(pages.begin() + 1);
    NavPage p; p.kind = kind; p.id = id; p.query = query; p.music = music; p.filters=filters;
    if (kind == "music") p.tabSel = query == "charts" ? 1 : query == "mixes" ? 2 : 0;
    pages.push_back(std::move(p)); load(worker, lang);
  }
  void channelFromVideo(Worker& worker, i18n::Lang lang, const std::string& id) {
    NavPage p; p.kind = "channel"; p.stamp = ++serial; pages.push_back(std::move(p));
    worker.add({6, "/api/nav/channel?id=" + id + "&lang=" + i18n::code(lang), page().stamp}, true);
  }
  static bool parse(const std::string& body, NavPage& p, bool append = false) {
    auto lines = net::split(body, '\n'); auto h = net::split(lines[0], '\t');
    if (h.empty() || h[0] != "ok" || h.size() < 7) { p.error = h.size() > 1 ? h[1] : ""; return false; }
    if (!append) { p.title = h[1]; p.subtitle = h[2]; p.image = h[3]; p.subscribed = h[4] == "1"; p.channelId = h[6];
      p.banner = h.size() > 7 ? h[7] : ""; p.historyVideo = h.size() > 8 ? h[8] : ""; p.laterVideo = h.size() > 9 ? h[9] : "";
      p.musicSource = h.size() > 10 ? h[10] : "";
    }
    if (p.kind == "channel" && p.id.empty()) p.id = p.channelId;
    p.more = h[5] == "1";
    for (size_t i = 1; i < lines.size(); i++) {
      auto f = net::split(lines[i], '\t'); if (f.size() < 8 || f[1].empty()) continue;
      NavItem item{f[0], f[1], f[2], f[3], f[5], f[6], f[7], std::atoi(f[4].c_str())};
      if (f.size() > 8) item.members = yt::membershipWire(f[8]);
      p.offset++;
      if (p.kind == "subscriptions" && item.type == "channel") p.channels.push_back(item);
      else p.items.push_back(item);
    }
    return true;
  }
  void arrived(const Result& r, gfx::Gfx& g, i18n::Lang lang) {
    for (auto& p : pages) if (p.stamp == r.stamp) {
      p.loading = false;
      if (r.feed.error != K::None) { p.error = i18n::get(lang, K::NoResponse); return; }
      bool first = p.offset == 0;
      p.native = r.catalog || r.localAccount;
      size_t start = p.items.size();
      p.direct = r.feed.direct;
      if (p.direct) {
        p.title = p.query; p.subtitle = i18n::get(lang, K::Console); p.more = false;
        for (const auto& v : r.feed.items) p.items.push_back({"video",v.id,v.title,v.channel,v.id,"","",v.dur,v.members});
        p.offset = (int)p.items.size();
      } else if (!parse(r.feed.title, p, !first) && p.error.empty()) p.error = i18n::get(lang, K::BadResponse);
      if (p.error=="account_music_unavailable") p.error=i18n::get(lang,K::MusicAccountUnavailable);
      if (p.error=="signed_out") p.error=i18n::get(lang,K::AccountRequired);
      if (p.musicHome()) p.musicLabel=p.musicSource.empty() ? "" : gfx::ellipsize(g,ui::F_TINY,
          i18n::get(lang,p.musicSource=="public" ? K::PublicMusic : K::YourMusic),ui::narrow ? ui::W-364 : 480);
      if (first && p.musicHome() && p.query.empty() && p.error.empty() && (p.musicSource=="web" || p.musicSource=="tv")) {
        std::vector<NavItem> shortcuts;
        for (auto entry : {std::pair<const char*,K>{"music_recent",K::MusicRecent},{"music_library",K::Playlists},{"music_playlist",K::MusicLiked}}) {
          NavItem item; item.type=entry.first; item.id=item.type=="music_playlist" ? "VLLM" : "ACCOUNT";
          item.image="local:"+item.type;
          item.title=i18n::get(lang,entry.second); item.section=i18n::get(lang,K::YourMusic);
          shortcuts.push_back(std::move(item));
        }
        p.items.insert(p.items.begin(),shortcuts.begin(),shortcuts.end());
      }
      if (p.kind=="library") localRevision=0;
      if (first && p.kind == "library" && p.error.empty()) {
        NavItem history; history.type = "history"; history.id = "HISTORY"; history.title = i18n::get(lang, K::History);
        NavItem later; later.type = "later"; later.id = "WL"; later.title = i18n::get(lang, K::WatchLater);
        NavItem mixes; mixes.type = "mixes"; mixes.id = "MIXES"; mixes.title = i18n::get(lang, K::Mixes);
        history.image = p.historyVideo; later.image = p.laterVideo;
        for (const auto& item : p.items) if (item.id.starts_with("RD") || item.id.starts_with("VLRD")) { mixes.image = item.image; break; }
        p.items.insert(p.items.begin(), {history, later, mixes});
      }
      format(p,g,lang,start);
      return;
    }
  }
  void format(NavPage& p, gfx::Gfx& g, i18n::Lang lang, size_t start=0) {
      for (size_t i = start; i < p.items.size(); i++) {
        auto& item = p.items[i];
        if (p.musicHome()) {
          if (p.shelves.empty() || (i && item.section != p.items[i-1].section)) {
            const std::string heading = item.section.empty() ? i18n::get(lang, K::Music) : item.section;
            p.shelves.push_back({(int)i, (int)i, (int)i, gfx::ellipsize(g, ui::F_KEY, heading, 1056)});
          }
          p.shelves.back().end = (int)i + 1;
          item.titleLines = {gfx::ellipsize(g, ui::F_SMALL, item.title, 140)};
          item.byline = gfx::ellipsize(g, ui::F_TINY, item.channel, 140);
          continue;
        }
        item.titleLines = gfx::wrap(g, ui::F_KEY, item.title, (ui::W-200-(ui::columns(4)-1)*24)/ui::columns(4)-(p.kind=="library" ? 24 : 0), 2);
        std::string by = item.type == "album" ? std::string(i18n::get(lang, K::Album)) + (item.channel.empty() ? "" : " · " + item.channel) :
            item.type == "artist" ? i18n::get(lang, K::Artist) : item.channel;
        item.byline = gfx::ellipsize(g, ui::F_CHAN, by, (ui::W-200-(ui::columns(4)-1)*24)/ui::columns(4));
      }
      for (auto& item : p.channels) item.byline = gfx::ellipsize(g, ui::F_CHAN, item.title, 142);
      int font = p.music && !p.musicHome() ? ui::F_HERO : ui::F_BIG;
      std::string title = p.kind == "music" || p.kind == "library" || p.kind == "subscriptions" || p.kind == "later" || p.kind == "history" || p.title.empty() ? i18n::get(lang, titleKey(p)) : p.title;
      p.displayTitle = gfx::ellipsize(g, font, title, ui::W-200-(p.kind=="channel" || p.kind=="playlist" || p.kind=="album" || p.kind=="music_playlist" ? 280 : 0)-(p.image.empty() ? 0 : p.music ? 110 : 96));
      std::string sub = p.subtitle;
      if (p.kind == "playlist" || p.kind == "music_playlist" || p.kind == "album" || p.kind == "later" || p.kind == "radio")
        sub = std::string(i18n::get(lang, K::QueueLimit)) + (sub.empty() ? "" : " · " + sub);
      p.displaySub = gfx::ellipsize(g, ui::F_CHAN, sub, 680);
      LOG("navegação %s: %u itens, %u canais, foco %d, zona %d", p.kind.c_str(), (unsigned)p.items.size(), (unsigned)p.channels.size(), p.sel, p.zone);
  }
  void refreshLocal(gfx::Gfx& g, i18n::Lang lang) {
    if (!local || pages.empty()) return;
    auto& p=page();
    bool list=p.kind=="local_later" || p.kind=="local_history", library=p.kind=="library";
    if ((!list && !library) || (localRevision==local->revision && localLang==lang)) return;
    localRevision=local->revision; localLang=lang;
    if (list) {
      p.items.clear(); p.loading=p.more=false; p.direct=true; p.image.clear();
      p.subtitle=i18n::get(lang,K::LocalOnly);
      for (const auto& e:p.kind=="local_later"?local->later:local->history) p.items.push_back({"video",e.id,e.title,e.channel,e.id,"","",e.duration});
    } else {
      p.items.erase(std::remove_if(p.items.begin(),p.items.end(),[](const NavItem& i){return i.type=="local_later" || i.type=="local_history";}),p.items.end());
      NavItem later,history; later.type="local_later";later.id="LOCAL_LATER";later.title=i18n::get(lang,K::LocalLater);
      history.type="local_history";history.id="LOCAL_HISTORY";history.title=i18n::get(lang,K::LocalHistory);
      if(!local->later.empty())later.image=local->later.front().id;
      if(!local->history.empty())history.image=local->history.front().id;
      p.items.insert(p.items.begin(),{later,history});
      if(!signedIn){p.loading=p.more=false;p.direct=true;p.error.clear();p.subtitle=i18n::get(lang,K::LocalOnly);}
    }
    if(!p.items.empty())p.sel=std::clamp(p.sel,0,int(p.items.size())-1);else p.sel=0;
    format(p,g,lang);
  }
  void action(Worker& worker, const std::string& path) {
    if (busy) return;
    busy = true; actionPath = path; actionStamp = ++serial; worker.add({6, path, actionStamp}, true);
  }
  static void artwork(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, const std::string& id, uint64_t frame, float x, float y, float w, float h, bool square = false, bool round = false, bool direct = false) {
    if (musicShortcutArt(g,id,x,y,w,h)) return;
    float radius = round ? w / 2 : square ? 6 : 12;
    const auto* s = id.empty() ? nullptr : thumbs.get(worker, id, frame, direct);
    if (s) {
      float crop = square ? (float)thumbs.height / thumbs.width : 1;
      gfx::image(g, s->tex, x, y, w, h, radius, {1,1,1,1}, crop);
    } else gfx::rect(g, x, y, w, h, ui::SURFACE2, radius);
  }
  static K titleKey(const NavPage& p) {
    if (p.kind == "local_later") return K::LocalLater;
    if (p.kind == "local_history") return K::LocalHistory;
    if (p.kind == "library") return K::Library;
    if (p.kind == "subscriptions") return K::Subscriptions;
    if (p.kind == "history") return K::History;
    if (p.kind == "later") return K::WatchLater;
    if (p.kind == "music") return K::Music;
    if (p.kind == "music_recent") return K::MusicRecent;
    if (p.kind == "music_library") return K::Playlists;
    if (p.kind == "radio") return K::Radio;
    if (p.kind == "search" || p.kind == "music_search") return K::Results;
    if (p.kind == "playlist" || p.kind == "music_playlist") return K::Playlists;
    if (p.kind == "album") return K::Album;
    if (p.kind == "artist") return K::Artist;
    return K::Channel;
  }
  void draw(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, uint64_t frame, uint64_t now, i18n::Lang lang, bool contentFocus) {
    if (pages.empty()) return;
    refreshLocal(g,lang);
    auto tr = [&](K k) { return i18n::get(lang, k); };
    auto& p = page();
    bool home = p.musicHome();
    bool hasBanner = !p.banner.empty();
    if (hasBanner) {
      if (const auto* cover = thumbs.get(worker, p.banner, frame, p.direct, true)) {
        gfx::image(g, cover->tex, 96, 0, Thumbs::bannerWidth, Thumbs::bannerHeight);
        gfx::hgrad(g, 96, 0, 780, Thumbs::bannerHeight, {0,0,0,0.55f}, {0,0,0,0});
        gfx::vgrad(g, 96, 0, Thumbs::bannerWidth, Thumbs::bannerHeight, {0,0,0,0.10f}, ui::BG);
      }
    }
    if (p.music && !home) gfx::hgrad(g, 96, 0, 1184, 224, rgb(0x32161f), ui::BG);
    float tx = 136;
    if (!home && !p.image.empty()) { artwork(g, thumbs, worker, p.image, frame, tx, p.music ? 46 : 28, p.music ? 86 : 72, p.music ? 86 : 72, true, p.kind == "channel" || p.kind == "artist"); tx += p.music ? 110 : 96; }

    int titleFont = p.music && !p.musicHome() ? ui::F_HERO : ui::F_BIG;
    gfx::textBox(g, titleFont, p.displayTitle.empty() ? tr(titleKey(p)) : p.displayTitle, tx, p.music && !home ? 42 : 28, home ? (ui::narrow ? 150 : 480) : ui::W-tx-64, ui::TEXT);
    if (home && !p.musicLabel.empty()) gfx::textBox(g, ui::F_TINY, p.musicLabel, ui::narrow ? 300 : tx, ui::narrow ? 32 : 69, ui::W-(ui::narrow ? 300 : tx)-64, ui::MUTED);
    if (!home) gfx::text(g, ui::F_CHAN, p.displaySub, tx, p.music ? 103 : 82, ui::MUTED);
    bool channel = p.kind == "channel", playlist = p.kind == "playlist" || p.kind == "album" || p.kind == "music_playlist" || p.kind == "music_recent" || p.kind == "later" || p.kind == "radio";
    if (channel || playlist) {
      bool f = contentFocus && p.zone == 0;
      gfx::rect(g, ui::W-316, p.music ? 64 : 32, 252, 54, f ? ui::TEXT : ui::SURFACE2, 27);
      gfx::text(g, ui::F_KEY, tr(channel ? p.subscribed ? K::Subscribed : K::Subscribe : K::PlayAll), ui::W-190, p.music ? 80 : 48, f ? ui::INK : ui::TEXT, 1);
    }
    if (p.kind=="search") {
      float hx=hint(g,136,126,"LB",XG,tr(K::SearchFilters));
      for (size_t row=0;row<p.filterLines.size();++row) gfx::textBox(g,ui::F_CHAN,p.filterLines[row],hx,122+row*30,ui::W-hx-64,ui::A2);
    }
    bool tabs = channel || p.kind == "music" || p.kind == "music_search";
    if (tabs) {
      const K channelTabs[] = {K::Videos,K::ChannelShorts,K::ChannelLive,K::Playlists};
      const K musicTabs[] = {K::Home,K::Charts,K::Mixes};
      const K searchTabs[] = {K::AllResults,K::Videos,K::Channel};
      const K songTabs[] = {K::AllResults,K::Songs,K::Albums,K::Artists};
      int count = channel || p.kind == "music_search" ? 4 : 3; float x = home && !ui::narrow ? 650 : 136;
      float tabY = home ? (ui::narrow ? 88 : 32) : p.music ? 166 : 120;
      for (int i = 0; i < count; i++) {
        K key = channel ? channelTabs[i] : p.kind == "music" ? musicTabs[i] : p.kind == "music_search" ? songTabs[i] : searchTabs[i];
        float w = gfx::textWidth(g, ui::F_KEY, tr(key)) + 40;
        bool active = i == p.tabSel, focus = contentFocus && active && p.zone == 1;
        gfx::rect(g, x, tabY, w, 42, focus ? ui::TEXT : active ? ui::SURFACE2 : ui::BG, 23);
        gfx::text(g, ui::F_KEY, tr(key), x + w / 2, tabY + 11, focus ? ui::INK : ui::TEXT, 1);
        if (active && !focus) gfx::hgrad(g, x + 20, tabY + 43, w - 40, 3, ui::A1, ui::A2, 1.5f);
        x += w + 14;
      }
    }
    float top = p.kind == "library" ? 120 : p.kind=="search" ? 210 : p.music ? 246 : 178;
    if (p.kind == "subscriptions" && !p.channels.empty()) {
      int channelCount=ui::narrow ? 4 : ui::large ? 5 : 7;
      int first = std::max(0, p.channelSel-channelCount+1);
      for (int c = first; c < std::min(first+channelCount, (int)p.channels.size()); c++) {
        float x = 136 + (c - first) * 154; const auto& item = p.channels[(size_t)c];
        bool f = contentFocus && p.zone == 2 && c == p.channelSel;
        if (f) gfx::rect(g, x - 5, 166 - 5, 106, 106, ui::TEXT, 53);
        artwork(g, thumbs, worker, item.image, frame, x, 166, 96, 96, true, true);
        gfx::text(g, ui::F_CHAN, item.byline, x, 276, f ? ui::TEXT : ui::MUTED);
      }
      top = 342;
    }
    int cols=ui::columns(4); float cw=std::floor((ui::W-200-(cols-1)*24)/cols), art=p.music ? std::min(cw,246.f) : cw, h=p.music ? art : cw*9/16, rowH=h+(p.kind=="library" ? 28 : ui::large ? 114 : 100);
    float want = p.zone != 3 ? 0 : (p.sel / cols) * rowH;
    p.scroll += (want - p.scroll) * 0.24f;
    if (home) {
      int shelfCount=ui::large ? 2 : 3, shelfColumns=ui::narrow ? 4 : ui::large ? 5 : 7;
      int firstShelf = std::max(0, p.shelf-shelfCount+1);
      for (int row = firstShelf; row < std::min(firstShelf+shelfCount, (int)p.shelves.size()); row++) {
        const auto& shelf = p.shelves[row];
        float y = (ui::narrow ? 150 : 99) + (row-firstShelf)*(ui::large ? 216 : 184);
        gfx::text(g, ui::F_KEY, shelf.title, 136, y, ui::TEXT);
        int first = std::max(shelf.first, shelf.selected-shelfColumns+1);
        for (int i = first; i < std::min(shelf.end, first+shelfColumns); i++) {
          const auto& item = p.items[i]; float x = 136 + (i - first) * 155;
          bool f = contentFocus && p.zone == 3 && i == p.sel;
          if (f) gfx::rect(g, x - 4, y + 26, 118, 118, ui::TEXT, 9);
          artwork(g, thumbs, worker, item.image, frame, x, y + 30, 110, 110, true);
          memberBadge(g, item.members, lang, x + 5, y + 35);
          gfx::textBox(g, ui::F_SMALL, item.titleLines[0], x, y + 143, 135, ui::TEXT);
          gfx::textBox(g, ui::F_TINY, item.byline, x, y + (ui::large ? 178 : 163), 135, ui::MUTED);
        }
      }
    } else for (size_t i = 0; i < p.items.size(); i++) {
      const auto& item = p.items[i];
      float x = 136 + (i % cols) * (cw + 24), y = top + (i / cols) * rowH - p.scroll;
      if (y < top - 2 || y + h + (p.kind == "library" ? 0 : 92) > 656) continue;
      bool f = contentFocus && p.zone == 3 && (int)i == p.sel;
      if (f) gfx::rect(g, x - 5, y - 5, art + 10, h + 10, ui::TEXT, 12);
      artwork(g, thumbs, worker, item.image, frame, x, y, art, h, p.music || item.type == "channel" || item.type == "artist", item.type == "channel" || item.type == "artist", p.direct);
      memberBadge(g, item.members, lang, x + 7, y + 7);
      const auto* saved=local && (!signedIn || p.kind=="local_later" || p.kind=="local_history")?local::Library::find(local->history,item.id):nullptr;
      double pos=saved?saved->position:0,duration=saved?saved->duration:item.duration;
      if(item.type=="video" && pos>0 && duration>0){gfx::rect(g,x+4,y+h-6,art-8,4,ui::SURFACE2,2);gfx::rect(g,x+4,y+h-6,(art-8)*float(std::clamp(pos/duration,0.0,1.0)),4,ui::A1,2);}
      if (p.kind == "library") {
        gfx::vgrad(g, x, y, art, h, {0,0,0,0}, {0,0,0,0.94f}, 12);
        float labelY = y + h - 12 - 27 * item.titleLines.size();
        for (const auto& line : item.titleLines) { gfx::textBox(g, ui::F_KEY, line, x + 12, labelY, art-24, ui::TEXT); labelY += 27; }
        continue;
      }
      if (item.image.empty()) {
        K k = item.type == "history" ? K::History : item.type == "later" ? K::WatchLater : item.type == "mixes" ? K::Mixes : K::Playlists;
        gfx::text(g, ui::F_H2, gfx::ellipsize(g, ui::F_H2, tr(k), art - 32), x + 16, y + h / 2 - 12, ui::MUTED);
      }
      float ty = y + h + 16;
      for (const auto& line : item.titleLines) { gfx::textBox(g, ui::F_KEY, line, x, ty, cw, ui::TEXT); ty += ui::large ? 34 : 27; }
      gfx::textBox(g, ui::F_CHAN, item.byline, x, ty + 4, cw, ui::MUTED);
    }
    if (p.items.empty() && !p.loading && !p.more) {
      std::string msg = !p.error.empty() ? p.error : tr(channel && p.tab == "live" ? K::NoLive : K::Empty);
      float y = top + 42;
      for (const auto& line : gfx::wrap(g, ui::F_H2, msg, ui::W-200, 3)) { gfx::textBox(g, ui::F_H2, line, 136, y, ui::W-200, ui::MUTED); y += 34; }
    }
    gfx::rect(g, 96, 646, ui::W-96, 74, ui::BG);
    if (p.loading) { spinner(g, ui::W-64, 146, 12, now / 1000.0); gfx::text(g, ui::F_CHAN, tr(p.offset ? K::MoreLoading : K::Loading), ui::W-324, 130, ui::MUTED); }
    float hx = hint(g, 136, 676, "A", XA, tr(p.zone == 0 ? channel ? p.subscribed ? K::Subscribed : K::Subscribe : playlist ? K::PlayAll : K::Open : K::Open));
    hx = hint(g, hx, 676, "B", XB, tr(K::Back));
    bool videoMenu=p.zone == 3 && !p.items.empty() && p.sel < (int)p.items.size() && p.items[(size_t)p.sel].type == "video";
    // Reserva a dica de salvar no rodapé curto; X/Y continuam disponíveis.
    if (!videoMenu || (!ui::narrow && !ui::large)) {
      hx = hint(g, hx, 676, "X", XX, tr(K::Refresh));
      hx = hint(g, hx, 676, "Y", XY, tr(K::Search));
    }
    if (videoMenu) hint(g, hx, 676, "Menu", XG, tr(K::MoreOptions));
  }
  void drawMenu(gfx::Gfx& g, i18n::Lang lang) {
    auto tr = [&](K k) { return i18n::get(lang, k); };
    gfx::rect(g, 0, 0, ui::W, 720, {0,0,0,0.8f});
    float left=(ui::W-740)/2;
    gfx::rect(g, left, 150, 740, 480, ui::SURFACE, 28);
    gfx::text(g, ui::F_BIG, tr(K::CardMenu), left+38, 180, ui::TEXT);
    gfx::text(g, ui::F_KEY, gfx::ellipsize(g, ui::F_KEY, menuItem.title, 662), left+38, 235, ui::TEXT);
    gfx::text(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, menuItem.channel + " · " + fmtTime(menuItem.duration), 662), left+38, 272, ui::MUTED);
    const K keys[] = {K::GoChannel, !signedIn ? (local && local->contains(menuItem.id) ? K::RemoveLocalLater : K::LocalLater) : K::WatchLater, K::Radio, local && local->contains(menuItem.id) ? K::RemoveLocalLater : K::LocalLater};
    for (int i = 0; i < (signedIn ? 4 : 3); i++) {
      gfx::rect(g, left+34, 320 + i * 60, 672, 50, i == menuSel ? ui::TEXT : ui::SURFACE2, 16);
      gfx::text(g, ui::F_KEY, tr(keys[i]), left+58, 334 + i * 60, i == menuSel ? ui::INK : ui::TEXT);
    }
    float hx = hint(g, left+38, 584, "A", XA, tr(K::Open)); hint(g, hx, 584, "B", XB, tr(K::Back));
  }
  void prepareNow(gfx::Gfx& g, Video& playing, i18n::Lang lang) {
    if (nowId == playing.id) return;
    lyricsOpen = false; lyrics.clear(); lyricsId.clear(); lyricLines.clear(); lyricScroll = 0;
    nowId = playing.id; size_t index = 0; const NavItem* current = nullptr;
    for (size_t i = 0; i < queue.size(); i++) if (queue[i].id == playing.id) { current = &queue[i]; index = i; break; }
    if (current) { playing.title = current->title; playing.channel = current->channel; }
    nowImage = current && !current->image.empty() ? current->image : playing.id;
    nowTitle = gfx::ellipsize(g, ui::F_BIG, playing.title, ui::W-(ui::narrow ? 404 : 530));
    nowArtist = gfx::ellipsize(g, ui::F_KEY, playing.channel, ui::W-(ui::narrow ? 404 : 530));
    nextTitles.clear();
    for (size_t i = index + 1; i < std::min(queue.size(), index + 5); i++) nextTitles.push_back(gfx::ellipsize(g, ui::F_KEY, queue[i].title, ui::W-(ui::narrow ? 448 : 574)));
    noLyrics = i18n::get(lang, K::NoLyrics); nowError.clear(); nowErrorKey = K::None;
  }
  void drawNow(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, const Video& playing, double pos, double duration, bool paused, bool waiting, K error, uint64_t frame, i18n::Lang lang) {
    (void)playing;
    auto tr = [&](K k) { return i18n::get(lang, k); };
    float contentX=ui::narrow ? 340 : 466;
    gfx::hgrad(g, 0, 0, 620, 720, rgb(0x37151f), ui::BG);
    gfx::text(g, ui::F_H2, tr(K::NowPlaying), 64, 40, ui::MUTED);
    artwork(g, thumbs, worker, nowImage, frame, 64, 104, ui::narrow ? 230 : 350, ui::narrow ? 230 : 350, true);
    gfx::textBox(g, ui::F_BIG, nowTitle, contentX, 104, ui::W-contentX-64, ui::TEXT);
    gfx::textBox(g, ui::F_KEY, nowArtist, contentX, 158, ui::W-contentX-64, ui::MUTED);
    if (error != nowErrorKey) { nowErrorKey = error; nowError = error == K::None ? "" : gfx::ellipsize(g, ui::F_KEY, tr(error), 714); }
    if (error != K::None) gfx::text(g, ui::F_KEY, nowError, contentX, 210, ui::A2);
    else if (waiting) gfx::text(g, ui::F_KEY, tr(K::Loading), contentX, 210, ui::A2);
    else if (lyricsOpen) {
      float y = 226;
      if (lyricLines.empty()) gfx::text(g, ui::F_KEY, noLyrics, contentX, y, ui::MUTED);
      for (size_t i = lyricScroll; i < std::min(lyricLines.size(), (size_t)lyricScroll + 9); i++) { gfx::textBox(g, ui::F_KEY, lyricLines[i], contentX, y, ui::W-contentX-64, ui::TEXT); y += 30; }
    } else {
      gfx::text(g, ui::F_H2, tr(K::Queue), contentX, 222, ui::TEXT);
      for (size_t n = 0; n < nextTitles.size(); n++) {
        float y = 272 + n * 58;
        gfx::text(g, ui::F_CHAN, std::to_string(n + 1), contentX+4, y, ui::A2);
        gfx::textBox(g, ui::F_KEY, nextTitles[n], contentX+44, y, ui::W-contentX-108, ui::TEXT);
      }
    }
    float fraction = duration > 0 ? (float)std::clamp(pos / duration, 0.0, 1.0) : 0;
    gfx::rect(g, 64, 516, ui::W-128, 6, ui::SURFACE2, 3);
    if (fraction > 0) gfx::hgrad(g, 64, 516, std::max(6.f, fraction * (ui::W-128)), 6, ui::A1, ui::A2, 3);
    gfx::text(g, ui::F_CHAN, fmtTime(pos), 64, 538, ui::MUTED); gfx::text(g, ui::F_CHAN, fmtTime(duration), ui::W-64, 538, ui::MUTED, 2);
    const K buttons[] = {paused ? K::Resume : K::Pause,K::NextTrack,K::Lyrics,K::ShowVideo}; float x = 64;
    for (int i = 0; i < 4; i++) {
      float w = gfx::textWidth(g, ui::F_KEY, tr(buttons[i])) + 56;
      gfx::rect(g, x, 592, w, 52, nowSel == i ? ui::TEXT : ui::SURFACE2, 26);
      gfx::text(g, ui::F_KEY, tr(buttons[i]), x + w / 2, 607, nowSel == i ? ui::INK : ui::TEXT, 1); x += w + 18;
    }
    float hx = hint(g, 64, 677, tr(K::BtnHorizontal), XG, tr(K::ChooseRow));
    hx = hint(g, hx, 677, "A", XA, tr(K::Open)); hint(g, hx, 677, "B", XB, tr(K::Back));
  }
};
