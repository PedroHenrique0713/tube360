#pragma once
#include "music_shortcut_art.hpp"

struct NavItem {
  std::string type, id, title, channel, image, channelId, section;
  int duration = 0;
  yt::Membership members = yt::Membership::None;
  std::vector<std::string> titleLines; std::string byline;
  bool shortForm = false;
  Video video() const { Video v; v.id = id; v.title = title; v.channel = channel; v.dur = std::max(0,duration); v.live = duration == 0; v.members = members; return v; }
  bool pill() const { return type == "chip" && image.empty(); }  // chip de clima ou botão do Explorar (os atalhos da Biblioteca são "chip" com capa)
};
struct NavPage : ItemGrid {
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
  bool musicHome() const { return kind == "music" || kind == "chip"; }
  void grid(int cols, bool hide) { ItemGrid::build(items,sel,cols,hide && !(kind == "channel" && tab == "shorts"),kind == "history" ? 1 : 0); }
  int gridRow() const { return ItemGrid::gridRow(sel); }
  int gridPosition() const { return ItemGrid::gridPosition(sel); }
  bool moveGrid(int horizontal, int vertical) { return ItemGrid::moveGrid(sel,horizontal,vertical); }
  float scroll = 0;
  uint64_t stamp = 0;
  std::string path() const {
    return "/api/nav?kind=" + kind + "&id=" + net::urlEncode(id) + "&q=" + net::urlEncode(query) + "&tab=" + tab + "&filter=" + std::to_string(tabSel) + (kind=="search" ? "&params="+net::urlEncode(filters.params()) : "");
  }
};
struct Navigation;
// O que uma página precisa para se desenhar: o quadro, as miniaturas, a página aberta e se o foco está no conteúdo.
struct NavDraw {
  gfx::Gfx& g; Thumbs& thumbs; Worker& worker; uint64_t frame, now; i18n::Lang lang; bool contentFocus, hideShorts;
  Navigation& nav; NavPage& p;
  const char* tr(K key) const { return i18n::get(lang, key); }
};
struct Navigation {
  int columnsSetting = 4;  // Configurações > colunas; a grade das páginas de navegação segue o mesmo ajuste da Home
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
    if (!append) { p.offset = 0; p.items.clear(); p.channels.clear(); p.channelSel = p.shelf = 0; p.shelves.clear(); p.gridSize = -1; }
    if (p.kind == "local_later" || p.kind == "local_history" || (p.kind == "library" && !signedIn)) { p.loading=false; localRevision=0; return; }
    std::string path = p.path() + "&lang=" + i18n::code(lang) + "&offset=" + std::to_string(p.offset) + "&fresh=" + (fresh ? "1" : "0");
    Job job{6, path, p.stamp};
    job.query = p.query; job.lang = lang; worker.add(job, true);
  }
  void open(Worker& worker, i18n::Lang lang, std::string kind, std::string id = "", std::string query = "", bool music = false, yt::search::Filters filters = {}) {
    // As telas guardam seleção e rolagem; B não refaz a consulta de onde o usuário veio.
    if (pages.size() >= 12) pages.erase(pages.begin() + 1);
    NavPage p; p.kind = kind; p.id = id; p.query = query; p.music = music; p.filters=filters;
    if (kind == "music") p.tabSel = query == "explore" ? 1 : query == "library" ? 2 : query == "charts" ? 3 : query == "mixes" ? 4 : 0;
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
      if (f.size() > 9) item.shortForm = f[9] == "short";
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
        for (const auto& v : r.feed.items) {
          NavItem item{"video",v.id,v.title,v.channel,v.id,"","",v.live ? 0 : v.dur > 0 ? v.dur : -1,v.members};
          item.shortForm=v.shortForm; p.items.push_back(std::move(item));
        }
        p.offset = (int)p.items.size();
      } else if (!parse(r.feed.title, p, !first) && p.error.empty()) p.error = i18n::get(lang, K::BadResponse);
      if (p.error=="account_music_unavailable") p.error=i18n::get(lang,K::MusicAccountUnavailable);
      if (p.error=="signed_out") p.error=i18n::get(lang,K::AccountRequired);
      if (p.musicHome()) p.musicLabel=p.musicSource.empty() ? "" : gfx::ellipsize(g,ui::F_TINY,
          i18n::get(lang,p.musicSource=="public" ? K::PublicMusic : K::YourMusic),ui::narrow ? ui::W-364 : 480);
      if (first && p.kind == "music" && (p.query.empty() || p.query == "library") && p.error.empty() && (p.musicSource=="web" || p.musicSource=="tv")) {
        std::vector<NavItem> shortcuts;
        for (auto entry : {std::pair<const char*,K>{"music_recent",K::MusicRecent},{"music_library",K::Playlists},{"music_playlist",K::MusicLiked},
                           {"music_songs",K::Songs},{"music_albums",K::Albums},{"music_artists",K::Artists}}) {
          NavItem item; item.type=entry.first; item.id=item.type=="music_playlist" ? "VLLM" : "ACCOUNT";
          // Biblioteca (músicas, álbuns, artistas): páginas do tipo chip, com o browseId da conta e o rótulo no id
          const std::string label=i18n::get(lang,entry.second);
          if (item.type=="music_songs") { item.type="chip"; item.id="FEmusic_liked_videos||"+label; }
          else if (item.type=="music_albums") { item.type="chip"; item.id="FEmusic_liked_albums||"+label; }
          else if (item.type=="music_artists") { item.type="chip"; item.id="FEmusic_library_corpus_track_artists||"+label; }
          item.image="local:"+std::string(entry.first);
          item.title=label; item.section=i18n::get(lang,K::YourMusic);
          shortcuts.push_back(std::move(item));
        }
        auto after = p.items.begin();
        while (after != p.items.end() && after->pill()) ++after;
        p.items.insert(after,shortcuts.begin(),shortcuts.end());
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
      // A Home da Música abre no conteúdo, não nos chips do topo
      if (first && p.musicHome() && p.shelves.size() > 1 && p.items[(size_t)p.shelves[0].first].pill()) { p.shelf = 1; p.sel = p.shelves[1].selected; }
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
          const float cardText = (ui::narrow ? 112 : 140) + 18;  // largura do cartão da prateleira (drawMusicPage)
          item.titleLines = {gfx::ellipsize(g, ui::F_SMALL, item.title, cardText)};
          item.byline = gfx::ellipsize(g, ui::F_TINY, item.channel, cardText);
          continue;
        }
        float width = std::floor((ui::W-200-(ui::columns(columnsSetting)-1)*24)/ui::columns(columnsSetting));
        if (item.shortForm) width = std::min(width*.5f,112.f);
        item.titleLines = gfx::wrap(g, item.shortForm ? ui::F_SMALL : ui::F_KEY, item.title, width-(p.kind=="library" && !item.shortForm ? 24 : 0), 2);
        std::string by = item.type == "album" ? std::string(i18n::get(lang, K::Album)) + (item.channel.empty() ? "" : " · " + item.channel) :
            item.type == "artist" ? i18n::get(lang, K::Artist) : item.channel;
        if (item.section == "top" && p.kind == "music_search") by = std::string(i18n::get(lang, K::MusicTopResult)) + (by.empty() ? "" : " · " + by);
        item.byline = gfx::ellipsize(g, item.shortForm ? ui::F_TINY : ui::F_CHAN, by, width);
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
      for (const auto& e:p.kind=="local_later"?local->later:local->history) p.items.push_back({"video",e.id,e.title,e.channel,e.id,"","",e.duration > 0 ? e.duration : -1});
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
  // páginas cujo botão do cabeçalho é "Reproduzir tudo"
  static bool playlistKind(const NavPage& p) { return p.kind == "playlist" || p.kind == "album" || p.kind == "music_playlist" || p.kind == "music_recent" || p.kind == "later" || p.kind == "radio"; }
  static K titleKey(const NavPage& p) {
    if (p.kind == "local_later") return K::LocalLater;
    if (p.kind == "local_history") return K::LocalHistory;
    if (p.kind == "library") return K::Library;
    if (p.kind == "subscriptions") return K::Subscriptions;
    if (p.kind == "history") return K::History;
    if (p.kind == "later") return K::WatchLater;
    if (p.kind == "music" || p.kind == "chip") return K::Music;
    if (p.kind == "music_recent") return K::MusicRecent;
    if (p.kind == "music_library") return K::Playlists;
    if (p.kind == "radio") return K::Radio;
    if (p.kind == "search" || p.kind == "music_search") return K::Results;
    if (p.kind == "playlist" || p.kind == "music_playlist") return K::Playlists;
    if (p.kind == "album") return K::Album;
    if (p.kind == "artist") return K::Artist;
    return K::Channel;
  }
  // Despachante: o fundo, a página do tipo certo (cada uma no arquivo do seu dono), a mensagem de vazio e o rodapé.
  void draw(gfx::Gfx& g, Thumbs& thumbs, Worker& worker, uint64_t frame, uint64_t now, i18n::Lang lang, bool contentFocus, bool hideShorts = false) {
    if (pages.empty()) return;
    refreshLocal(g,lang);
    auto& p = page();
    NavDraw d{g, thumbs, worker, frame, now, lang, contentFocus, hideShorts, *this, p};
    drawBanner(d);
    float top = p.music || p.musicHome() || p.kind == "music_search" ? drawMusicPage(d) : p.kind == "channel" ? drawChannelPage(d) : p.kind == "search" ? drawSearchPage(d) : drawCollectionPage(d);
    drawEmpty(d, top);
    drawFooter(d);
  }
  void drawBanner(NavDraw& d);
  float drawMusicPage(NavDraw& d);       // ui_music.hpp
  float drawChannelPage(NavDraw& d);     // ui_channel.hpp
  float drawSearchPage(NavDraw& d);      // ui_search.hpp
  float drawCollectionPage(NavDraw& d);  // ui_collection.hpp
  void drawEmpty(NavDraw& d, float top);
  void drawFooter(NavDraw& d);
  void drawMenu(gfx::Gfx& g, i18n::Lang lang);
  // ---- F61 Início e coleção: funções novas desta frente só aqui

  // ---- F62 Busca e canal: funções novas desta frente só aqui

  // ---- F63 Player e Shorts: funções novas desta frente só aqui

  // ---- F64 Configurações e controle: funções novas desta frente só aqui
  // Linhas do Menu: as do vídeo em foco (se houver) e, sempre por último, "Atualizar" a página.
  enum class MenuKind { Channel, Later, Radio, Local, Refresh };
  struct MenuEntry { MenuKind kind; K label; };
  bool pageMenu() const { return menuItem.id.empty(); }
  std::vector<MenuEntry> menuEntries() const {
    std::vector<MenuEntry> list;
    if (!pageMenu()) {
      const bool saved = local && local->contains(menuItem.id);
      const K localLabel = saved ? K::RemoveLocalLater : K::LocalLater;
      list.push_back({MenuKind::Channel, K::GoChannel});
      if (signedIn) { list.push_back({MenuKind::Later, K::WatchLater}); list.push_back({MenuKind::Radio, K::Radio}); list.push_back({MenuKind::Local, localLabel}); }
      else { list.push_back({MenuKind::Local, localLabel}); list.push_back({MenuKind::Radio, K::Radio}); }
    }
    list.push_back({MenuKind::Refresh, K::Refresh});
    return list;
  }

  // ---- F65 Música: funções novas desta frente só aqui

  // ---- F66 controle pelo celular: funções novas desta frente só aqui

  // ---- F67 membros tocando: funções novas desta frente só aqui

};
