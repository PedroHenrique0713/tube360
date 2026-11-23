#pragma once

struct NavItem {
  std::string type, id, title, channel, image, channelId, section;
  int duration = 0;
  std::vector<std::string> titleLines; std::string byline;
  Video video() const { Video v; v.id = id; v.title = title; v.channel = channel; v.dur = duration; return v; }
};
struct NavPage {
  std::string kind, id, query, tab = "videos", title, subtitle, image, channelId, error, displayTitle, displaySub;
  std::vector<NavItem> items, channels;
  bool music = false, loading = true, more = false, subscribed = false;
  int sel = 0, channelSel = 0, zone = 3, tabSel = 0, offset = 0;
  float scroll = 0;
  uint64_t stamp = 0;
  std::string path() const {
    return "/api/nav?kind=" + kind + "&id=" + net::urlEncode(id) + "&q=" + net::urlEncode(query) + "&tab=" + tab + "&filter=" + std::to_string(tabSel);
  }
};
struct Navigation {
  std::vector<NavPage> pages;
  uint64_t serial = 0, actionStamp = 0;
  bool ownsQueue = false, cancelQueue = false, busy = false, musicSession = false, musicNow = false, lyricsOpen = false, unsubscribe = false;
  std::string actionPath, lyrics, lyricsId;
  std::vector<std::string> lyricLines; int lyricScroll = 0;
  std::vector<NavItem> queue;
  int nowSel = 0, nowZone = 0, menuSel = 0;
  bool menu = false;
  NavItem menuItem;
  NavPage& page() { return pages.back(); }
  void load(Worker& worker, i18n::Lang lang, bool fresh = false, bool append = false) {
    auto& p = page(); p.loading = true; p.error.clear(); p.stamp = ++serial;
    p.more = false;
    if (!append) { p.offset = 0; p.items.clear(); p.channels.clear(); }
    std::string path = p.path() + "&lang=" + i18n::code(lang) + "&offset=" + std::to_string(p.offset) + "&fresh=" + (fresh ? "1" : "0");
    worker.add({6, path, p.stamp}, true);
  }
  void open(Worker& worker, i18n::Lang lang, std::string kind, std::string id = "", std::string query = "", bool music = false) {
    // As telas guardam seleção e rolagem; B não refaz a consulta de onde o usuário veio.
    if (pages.size() >= 12) pages.erase(pages.begin() + 1);
    NavPage p; p.kind = kind; p.id = id; p.query = query; p.music = music;
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
    if (!append) { p.title = h[1]; p.subtitle = h[2]; p.image = h[3]; p.subscribed = h[4] == "1"; p.channelId = h[6]; }
    if (p.kind == "channel" && p.id.empty()) p.id = p.channelId;
    p.more = h[5] == "1";
    for (size_t i = 1; i < lines.size(); i++) {
      auto f = net::split(lines[i], '\t'); if (f.size() < 8 || f[1].empty()) continue;
      NavItem item{f[0], f[1], f[2], f[3], f[5], f[6], f[7], std::atoi(f[4].c_str())};
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
      if (!parse(r.feed.title, p, !first) && p.error.empty()) p.error = i18n::get(lang, K::BadResponse);
      if (first && p.kind == "library" && p.error.empty()) {
        NavItem history; history.type = "history"; history.id = "HISTORY"; history.title = i18n::get(lang, K::History);
        NavItem later; later.type = "later"; later.id = "WL"; later.title = i18n::get(lang, K::WatchLater);
        NavItem mixes; mixes.type = "mixes"; mixes.id = "MIXES"; mixes.title = i18n::get(lang, K::Mixes);
        p.items.insert(p.items.begin(), {history, later, mixes});
      }
      for (auto& item : p.items) {
        item.titleLines = gfx::wrap(g, ui::F_KEY, item.title, 246, 2);
        std::string by = item.type == "album" ? std::string(i18n::get(lang, K::Album)) + (item.channel.empty() ? "" : " · " + item.channel) :
            item.type == "artist" ? i18n::get(lang, K::Artist) : item.channel;
        item.byline = gfx::ellipsize(g, ui::F_CHAN, by, 246);
      }
      int font = p.music ? ui::F_HERO : ui::F_BIG;
      std::string title = p.kind == "music" || p.kind == "library" || p.kind == "subscriptions" || p.kind == "later" || p.kind == "history" || p.title.empty() ? i18n::get(lang, titleKey(p)) : p.title;
      p.displayTitle = gfx::ellipsize(g, font, title, 680 - (p.image.empty() ? 0 : p.music ? 110 : 96));
      std::string sub = p.kind == "music" ? i18n::get(lang, K::MusicIntro) : p.subtitle;
      if (p.kind == "playlist" || p.kind == "music_playlist" || p.kind == "album" || p.kind == "later" || p.kind == "radio")
        sub = std::string(i18n::get(lang, K::QueueLimit)) + (sub.empty() ? "" : " · " + sub);
      p.displaySub = gfx::ellipsize(g, ui::F_CHAN, sub, 680);
      LOG("navegação %s: %u itens, %u canais, foco %d, zona %d", p.kind.c_str(), (unsigned)p.items.size(), (unsigned)p.channels.size(), p.sel, p.zone);
      return;
    }
  }
  void action(Worker& worker, const std::string& path) {
    if (busy) return;
    busy = true; actionPath = path; actionStamp = ++serial; worker.add({6, path, actionStamp}, true);
  }
  static void artwork(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, const std::string& id, uint64_t frame, float x, float y, float w, float h, bool square = false) {
    gfx::rect(g, x, y, w, h, ui::SURFACE2, square ? 6 : 12);
    if (id.empty()) return;
    if (const auto* s = thumbs.get(worker, id, frame)) {
      if (square) {
        float fraction = (float)thumbs.height / thumbs.width;
        gfx::imagePart(g, s->tex, x, y, w, h, (1 - fraction) / 2, 0, (1 + fraction) / 2, 1, ui::TEXT);
      } else gfx::image(g, s->tex, x, y, w, h, 12);
    }
  }
  static K titleKey(const NavPage& p) {
    if (p.kind == "library") return K::Library;
    if (p.kind == "subscriptions") return K::Subscriptions;
    if (p.kind == "history") return K::History;
    if (p.kind == "later") return K::WatchLater;
    if (p.kind == "music") return K::Music;
    if (p.kind == "radio") return K::Radio;
    if (p.kind == "search" || p.kind == "music_search") return K::Results;
    if (p.kind == "playlist" || p.kind == "music_playlist") return K::Playlists;
    if (p.kind == "album") return K::Album;
    if (p.kind == "artist") return K::Artist;
    return K::Channel;
  }
  void draw(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, uint64_t frame, uint64_t now, i18n::Lang lang) {
    if (pages.empty()) return;
    auto tr = [&](K k) { return i18n::get(lang, k); };
    auto& p = page();
    if (p.music) gfx::hgrad(g, 96, 0, 1184, 224, rgb(0x32161f), ui::BG);
    float tx = 136;
    if (!p.image.empty()) { artwork(g, thumbs, worker, p.image, frame, tx, p.music ? 46 : 28, p.music ? 86 : 72, p.music ? 86 : 72, true); tx += p.music ? 110 : 96; }

    int titleFont = p.music ? ui::F_HERO : ui::F_BIG;
    gfx::text(g, titleFont, p.displayTitle.empty() ? tr(titleKey(p)) : p.displayTitle, tx, p.music ? 42 : 28, ui::TEXT);
    gfx::text(g, ui::F_CHAN, p.displaySub, tx, p.music ? 103 : 82, ui::MUTED);
    bool channel = p.kind == "channel", playlist = p.kind == "playlist" || p.kind == "album" || p.kind == "music_playlist" || p.kind == "later" || p.kind == "radio";
    if (channel || playlist) {
      bool f = p.zone == 0;
      gfx::rect(g, 964, p.music ? 64 : 32, 252, 54, f ? ui::TEXT : ui::SURFACE2, 27);
      gfx::text(g, ui::F_KEY, tr(channel ? p.subscribed ? K::Subscribed : K::Subscribe : K::PlayAll), 1090, p.music ? 80 : 48, f ? ui::INK : ui::TEXT, 1);
    }
    bool tabs = channel || p.kind == "music" || p.kind == "search" || p.kind == "music_search";
    if (tabs) {
      const K channelTabs[] = {K::Videos,K::Live,K::Playlists};
      const K musicTabs[] = {K::QuickPicks,K::Charts,K::Mixes};
      const K searchTabs[] = {K::AllResults,K::Videos,K::Channel};
      const K songTabs[] = {K::AllResults,K::Songs,K::Albums,K::Artists};
      int count = p.kind == "music_search" ? 4 : 3; float x = 136;
      for (int i = 0; i < count; i++) {
        K key = channel ? channelTabs[i] : p.kind == "music" ? musicTabs[i] : p.kind == "music_search" ? songTabs[i] : searchTabs[i];
        float w = gfx::textWidth(g, ui::F_KEY, tr(key)) + 40;
        bool active = i == p.tabSel, focus = active && p.zone == 1;
        gfx::rect(g, x, p.music ? 166 : 120, w, 42, focus ? ui::TEXT : active ? ui::SURFACE2 : ui::BG, 23);
        gfx::text(g, ui::F_KEY, tr(key), x + w / 2, p.music ? 177 : 131, focus ? ui::INK : ui::TEXT, 1);
        if (active && !focus) gfx::hgrad(g, x + 20, p.music ? 211 : 163, w - 40, 3, ui::A1, ui::A2, 1.5f);
        x += w + 14;
      }
    }
    float top = p.music ? 246 : 178;
    if (p.kind == "subscriptions" && !p.channels.empty()) {
      int first = std::max(0, p.channelSel - 6);
      for (int c = first; c < std::min(first + 7, (int)p.channels.size()); c++) {
        float x = 136 + (c - first) * 154; const auto& item = p.channels[(size_t)c];
        bool f = p.zone == 2 && c == p.channelSel;
        if (f) gfx::rect(g, x - 5, 166 - 5, 106, 106, ui::TEXT, 53);
        artwork(g, thumbs, worker, item.image, frame, x, 166, 96, 96, true);
        gfx::text(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, item.title, 142), x, 276, f ? ui::TEXT : ui::MUTED);
      }
      top = 342;
    }
    int cols = 4; float cw = 246, art = p.music ? 216 : 246, h = p.music ? 216 : 138, rowH = p.music ? 314 : 238;
    float want = p.zone != 3 ? 0 : (p.sel / cols) * rowH;
    p.scroll += (want - p.scroll) * 0.24f;
    for (size_t i = 0; i < p.items.size(); i++) {
      const auto& item = p.items[i];
      float x = 136 + (i % cols) * (cw + 24), y = top + (i / cols) * rowH - p.scroll;
      if (y < top - 2 || y + h + 92 > 656) continue;
      bool f = p.zone == 3 && (int)i == p.sel;
      if (f) gfx::rect(g, x - 5, y - 5, art + 10, h + 10, ui::TEXT, 12);
      artwork(g, thumbs, worker, item.image, frame, x, y, art, h, p.music || item.type == "channel" || item.type == "artist");
      if (item.image.empty()) {
        K k = item.type == "history" ? K::History : item.type == "later" ? K::WatchLater : item.type == "mixes" ? K::Mixes : K::Playlists;
        gfx::text(g, ui::F_H2, gfx::ellipsize(g, ui::F_H2, tr(k), art - 32), x + 16, y + h / 2 - 12, ui::MUTED);
      }
      float ty = y + h + 16;
      for (const auto& line : item.titleLines) { gfx::text(g, ui::F_KEY, line, x, ty, ui::TEXT); ty += 27; }
      gfx::text(g, ui::F_CHAN, item.byline, x, ty + 4, ui::MUTED);
    }
    if (p.items.empty() && !p.loading) {
      std::string msg = !p.error.empty() ? p.error : tr(channel && p.tab == "live" ? K::NoLive : K::Empty);
      float y = top + 42;
      for (const auto& line : gfx::wrap(g, ui::F_H2, msg, 1010, 3)) { gfx::text(g, ui::F_H2, line, 136, y, ui::MUTED); y += 34; }
    }
    gfx::rect(g, 96, 656, 1184, 64, ui::BG);
    if (p.loading) { spinner(g, 1188, 146, 12, now / 1000.0); gfx::text(g, ui::F_CHAN, tr(p.offset ? K::MoreLoading : K::Loading), 936, 130, ui::MUTED); }
    if (!p.error.empty() && !p.items.empty()) gfx::text(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, p.error, 1030), 136, 620, ui::A2);
    float hx = hint(g, 136, 676, "A", XA, tr(p.zone == 0 ? channel ? p.subscribed ? K::Subscribed : K::Subscribe : playlist ? K::PlayAll : K::Open : K::Open));
    hx = hint(g, hx, 676, "B", XB, tr(K::Back));
    hx = hint(g, hx, 676, "X", XX, tr(K::Refresh));
    hx = hint(g, hx, 676, "Y", XY, tr(K::Search));
    if (p.zone == 3 && !p.items.empty() && p.sel < (int)p.items.size() && p.items[(size_t)p.sel].type == "video") hint(g, hx, 676, "Menu", XG, tr(K::MoreOptions));
  }
  void drawMenu(gfx::Gfx& g, i18n::Lang lang) {
    auto tr = [&](K k) { return i18n::get(lang, k); };
    gfx::rect(g, 0, 0, 1280, 720, {0,0,0,0.8f});
    gfx::rect(g, 270, 150, 740, 424, ui::SURFACE, 28);
    gfx::text(g, ui::F_BIG, tr(K::CardMenu), 308, 180, ui::TEXT);
    gfx::text(g, ui::F_KEY, gfx::ellipsize(g, ui::F_KEY, menuItem.title, 662), 308, 235, ui::TEXT);
    gfx::text(g, ui::F_CHAN, gfx::ellipsize(g, ui::F_CHAN, menuItem.channel + " · " + fmtTime(menuItem.duration), 662), 308, 272, ui::MUTED);
    const K keys[] = {K::GoChannel,K::WatchLater,K::Radio};
    for (int i = 0; i < 3; i++) {
      gfx::rect(g, 304, 320 + i * 60, 672, 50, i == menuSel ? ui::TEXT : ui::SURFACE2, 16);
      gfx::text(g, ui::F_KEY, tr(keys[i]), 328, 334 + i * 60, i == menuSel ? ui::INK : ui::TEXT);
    }
    float hx = hint(g, 308, 528, "A", XA, tr(K::Open)); hint(g, hx, 528, "B", XB, tr(K::Back));
  }
  void drawNow(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, const Video& playing, double pos, double duration, bool paused, bool waiting, K error, uint64_t frame, i18n::Lang lang) {
    auto tr = [&](K k) { return i18n::get(lang, k); };
    gfx::hgrad(g, 0, 0, 620, 720, rgb(0x37151f), ui::BG);
    gfx::text(g, ui::F_H2, tr(K::NowPlaying), 64, 40, ui::MUTED);
    const NavItem* current = nullptr; size_t index = 0;
    for (size_t i = 0; i < queue.size(); i++) if (queue[i].id == playing.id) { current = &queue[i]; index = i; break; }
    artwork(g, thumbs, worker, current ? current->image : playing.id, frame, 64, 104, 350, 350, true);
    gfx::text(g, ui::F_BIG, gfx::ellipsize(g, ui::F_BIG, playing.title, 714), 466, 104, ui::TEXT);
    gfx::text(g, ui::F_KEY, gfx::ellipsize(g, ui::F_KEY, current ? current->channel : playing.channel, 714), 466, 158, ui::MUTED);
    if (error != K::None) gfx::text(g, ui::F_KEY, gfx::ellipsize(g, ui::F_KEY, tr(error), 714), 466, 210, ui::A2);
    else if (waiting) gfx::text(g, ui::F_KEY, tr(K::Loading), 466, 210, ui::A2);
    else if (lyricsOpen) {
      float y = 226;
      if (lyricLines.empty()) gfx::text(g, ui::F_KEY, tr(K::NoLyrics), 466, y, ui::MUTED);
      for (size_t i = lyricScroll; i < std::min(lyricLines.size(), (size_t)lyricScroll + 9); i++) { gfx::text(g, ui::F_KEY, lyricLines[i], 466, y, ui::TEXT); y += 30; }
    } else {
      gfx::text(g, ui::F_H2, tr(K::Queue), 466, 222, ui::TEXT);
      for (size_t n = 0; n < 4 && index + n + 1 < queue.size(); n++) {
        const auto& v = queue[index + n + 1]; float y = 272 + n * 58;
        gfx::text(g, ui::F_CHAN, std::to_string(n + 1), 470, y, ui::A2);
        gfx::text(g, ui::F_KEY, gfx::ellipsize(g, ui::F_KEY, v.title, 660), 510, y, ui::TEXT);
      }
    }
    float fraction = duration > 0 ? (float)std::clamp(pos / duration, 0.0, 1.0) : 0;
    gfx::rect(g, 64, 516, 1152, 6, ui::SURFACE2, 3);
    if (fraction > 0) gfx::hgrad(g, 64, 516, std::max(6.f, fraction * 1152), 6, ui::A1, ui::A2, 3);
    gfx::text(g, ui::F_CHAN, fmtTime(pos), 64, 538, ui::MUTED); gfx::text(g, ui::F_CHAN, fmtTime(duration), 1216, 538, ui::MUTED, 2);
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
