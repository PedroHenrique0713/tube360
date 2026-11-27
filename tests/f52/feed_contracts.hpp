#pragma once

static std::string feedFixture() {
  std::ifstream file(std::filesystem::path(__FILE__).parent_path()/"fixtures/feeds.json");
  require(bool(file),"mixed feed fixture exists");
  return {std::istreambuf_iterator<char>(file),{}};
}
struct FeedMock : yt::Transport {
  std::string body=feedFixture(), nextBody, expectedBrowse;
  bool account=false;
  int posts=0, continuations=0;
  yt::Response request(const std::string& url,const std::string& body={},bool=false) override { return send({url,body}); }
  yt::Response send(const yt::Request& r) override {
    using namespace yt::catalog;
    if (r.body.empty()) {
      require(!account && r.url=="https://www.youtube.com/","public WEB bootstrap only");
      return {200,R"(<script>ytcfg.set({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"fixture"}}})</script>)"};
    }
    require(r.url.starts_with("https://www.youtube.com/youtubei/v1/"),"feed transport rejects unrelated endpoints");
    J data; require(parse(r.body,data),"feed request JSON");
    bool bearer=std::find(r.headers.begin(),r.headers.end(),"Authorization: Bearer F52_ACCESS")!=r.headers.end();
    require(bearer==account,"public feed anonymous, account feed uses fictional Bearer");
    ++posts;
    if (!str(data,"continuation").empty()) {
      require(str(data,"continuation")=="F52_NEXT" && !nextBody.empty(),"continuation cursor belongs to feed");
      ++continuations; return {200,nextBody};
    }
    auto browse=str(data,"browseId");
    if (browse=="FEchannels") return {200,R"({"contents":[{"channelRenderer":{"channelId":"UC0000000000000000000001","title":{"simpleText":"Fixture channel"}}}]})"};
    if (!expectedBrowse.empty()) require(browse==expectedBrowse,"account feed browse ID");
    return {200,body};
  }
};
static void seedFeedAccount(const std::string& path) {
  std::ofstream file(path);
  file<<R"({"access_token":"F52_ACCESS","refresh_token":"F52_REFRESH","expires_at":5000,"client":{"client_id":"fixture-client","client_secret":"fixture-secret"},"name":"F52 fixture"})";
}
static Feed parseNativeFeed(const yt::AccountReply& reply) {
  require(reply.ok(),"native account feed succeeds");
  Feed feed; parseFeed(std::vector<uint8_t>(reply.body.begin(),reply.body.end()),feed);
  require(feed.error==K::None,"native feed wire parses"); return feed;
}
static void feedContract(const std::string& dir) {
  FeedMock web; yt::Client client(web);
  for (auto page:{client.home(),client.search("F52 fixture")}) {
    require(page.error.empty() && page.cards.size()==33,"public Home/search retain all 33 videos and Shorts");
    require(std::count_if(page.cards.begin(),page.cards.end(),[](const auto& c){return c.shortForm;})==13,"both Short renderers retained without duration heuristic");
    require(page.cards[4].id=="f52vid00101" && page.cards[4].title=="Short 101" && page.cards[9].shortForm,"legacy endpoint/headline and modern onTap/overlay");
  }
  auto next=client.next("f52vid00101");
  require(next.cards.size()==32 && std::count_if(next.cards.begin(),next.cards.end(),[](const auto& c){return c.shortForm;})==12,"public next excludes current Short and preserves remaining markers");
  std::cout<<"PASS public feeds: 33 cards, 13 Shorts; WEB Home/search/next, both onTap forms\n";
  for (auto [kind,browse]:{std::pair{"home","FEwhat_to_watch"},{"subs","FEsubscriptions"},{"history","FEhistory"},{"later","VLWL"}}) {
    const std::string path=dir+kind+"-token.json"; seedFeedAccount(path);
    FeedMock net; net.account=true; net.expectedBrowse=browse; yt::ConsoleAccount account(net,path);
    auto feed=parseNativeFeed(account.request(std::string("/api/feed?kind=")+kind,1000,10));
    require(feed.items.size()==24 && feed.more,"first native page retains raw 24-item offset and has more");
    require(std::count_if(feed.items.begin(),feed.items.end(),[](const auto& v){return v.shortForm;})==13,"13 Shorts survive native wire");
    int selected=0; feed.layout.build(feed.items,selected,4,false);
    require(feed.layout.rows[1].shorts && feed.layout.rows[1].items.size()==13,"native Home/feed groups Shorts at first Short");
    auto reply=account.request(std::string("/api/feed?kind=")+kind+"&offset=24",1001,11);
    require(reply.ok(),"second native page succeeds");
    parseFeed(std::vector<uint8_t>(reply.body.begin(),reply.body.end()),feed);
    require(feed.items.size()==33 && !feed.more && net.posts==1,"cached second page preserves all source indices and ends pagination");
    selected=4; feed.layout.build(feed.items,selected,4,true);
    size_t count=0; for (const auto& row:feed.layout.rows) { require(!row.shorts,"hide has no Short row"); count+=row.items.size(); }
    require(count==20 && selected==5 && feed.items.size()==33,"hide repairs focused Short without deleting raw feed items");
    std::cout<<"PASS account "<<kind<<": 24+9 raw items, 13 Shorts in row, 20 ordinary videos in hide\n";
  }
  const std::string path=dir+"subscriptions-token.json"; seedFeedAccount(path);
  FeedMock net; net.account=true; net.expectedBrowse="FEsubscriptions"; yt::ConsoleAccount account(net,path);
  auto reply=account.request("/api/nav?kind=subscriptions",1000,10);
  NavPage page; page.kind="subscriptions";
  require(reply.ok() && Navigation::parse(reply.body,page),"native subscriptions listing parses");
  page.grid(4,false);
  require(!page.channels.empty() && page.rows[1].shorts && page.rows[1].items.size()==13,"native subscriptions keeps channel strip and Short row");
  page.grid(4,true); require(std::none_of(page.rows.begin(),page.rows.end(),[](const auto& row){return row.shorts;}),"native subscription listing obeys hide");
  FeedMock continuation; continuation.account=true; continuation.expectedBrowse="FEwhat_to_watch";
  continuation.body=R"({"contents":[)";
  for (int i=0;i<24;++i) {
    if (i) continuation.body+=",";
    continuation.body+="{\"reelItemRenderer\":{\"videoId\":\"f52vid"+std::to_string(10100+i)+"\",\"headline\":{\"simpleText\":\"Short\"}}}";
  }
  continuation.body+=R"(,{"continuationCommand":{"token":"F52_NEXT"}}]})";
  continuation.nextBody=R"({"contents":[{"videoRenderer":{"videoId":"f52vid00001","title":{"simpleText":"Visible after hidden page"}}},{"reelItemRenderer":{"videoId":"f52vid10200","headline":{"simpleText":"New Short"}}},{"reelItemRenderer":{"videoId":"f52vid10100","headline":{"simpleText":"Duplicate"}}}]})";
  yt::ConsoleAccount continued(continuation,path);
  auto feed=parseNativeFeed(continued.request("/api/feed?kind=home",1000,10));
  int selected=0; feed.layout.build(feed.items,selected,4,true);
  require(feed.items.size()==24 && feed.layout.rows.empty() && feed.more,"all-hidden first page retains cursor for automatic continuation");
  auto tail=continued.request("/api/feed?kind=home&offset=24",1001,11);
  require(tail.ok(),"all-hidden page can continue"); parseFeed(std::vector<uint8_t>(tail.body.begin(),tail.body.end()),feed);
  feed.layout.build(feed.items,selected,4,true);
  require(feed.items.size()==26 && !feed.more && continuation.continuations==1 && feed.layout.rows.size()==1 && selected==24,"continuation deduplicates Shorts and exposes ordinary video without offset drift");
  std::cout<<"PASS account continuation: 24 hidden Shorts -> video at raw index 24; duplicate skipped; more=0\n";
  // Início da conta (TVHTML5): prateleiras `shelfRenderer` com o título em `shelfHeaderRenderer.avatarLockup`, cada uma com
  // 5 `tileRenderer`; a próxima página vem por `nextContinuationData`. Tudo inventado, só a estrutura é a do YouTube.
  {
    auto tile=[](const std::string& id,const std::string& title,const std::string& channel,const std::string& length) {
      return "{\"tileRenderer\":{\"style\":\"TILE_STYLE_YTLR_DEFAULT\",\"contentType\":\"TILE_CONTENT_TYPE_VIDEO\",\"contentId\":\""+id+"\","
             "\"header\":{\"tileHeaderRenderer\":{\"thumbnailOverlays\":[{\"thumbnailOverlayTimeStatusRenderer\":{\"text\":{\"simpleText\":\""+length+"\"},\"style\":\"DEFAULT\"}}]}},"
             "\"metadata\":{\"tileMetadataRenderer\":{\"title\":{\"simpleText\":\""+title+"\"},\"lines\":[{\"lineRenderer\":{\"items\":[{\"lineItemRenderer\":{\"text\":{\"runs\":[{\"text\":\""+channel+"\"}]}}}]}}]}},"
             "\"onSelectCommand\":{\"watchEndpoint\":{\"videoId\":\""+id+"\"}}}}";
    };
    auto shelf=[&](const std::string& title,const std::string& prefix) {
      std::string items;
      for (int i=0;i<5;++i) { if (i) items+=","; items+=tile(prefix+std::to_string(1000+i).substr(1),title+" item "+std::to_string(i+1),"Canal "+prefix,"3:0"+std::to_string(i)); }
      return "{\"shelfRenderer\":{\"headerRenderer\":{\"shelfHeaderRenderer\":{\"avatarLockup\":{\"avatarLockupRenderer\":{\"title\":{\"runs\":[{\"text\":\""+title+"\"}]}}}}},"
             "\"content\":{\"horizontalListRenderer\":{\"items\":["+items+"]}}}}";
    };
    FeedMock shelves; shelves.account=true; shelves.expectedBrowse="FEwhat_to_watch";
    shelves.body=R"({"contents":{"tvBrowseRenderer":{"content":{"tvSurfaceContentRenderer":{"content":{"sectionListRenderer":{"contents":[)"+
        shelf("Prateleira A","f61shlfA")+","+shelf("Prateleira B","f61shlfB")+R"(],"continuations":[{"nextContinuationData":{"continuation":"F52_NEXT"}}]}}}}}}})";
    shelves.nextBody=R"({"continuationContents":{"sectionListContinuation":{"contents":[)"+shelf("Prateleira C","f61shlfC")+"]}}}";
    yt::ConsoleAccount home(shelves,path);
    auto feed=parseNativeFeed(home.request("/api/feed?kind=home",1000,10));
    // A conta pede a continuação sozinha enquanto a página (24 itens) não enche: 2 prateleiras + 1 da continuação.
    require(feed.items.size()==15 && !feed.more && shelves.continuations==1,"Home shelves: first page fills from the continuation (5+5+5 tiles)");
    require(feed.items[0].section=="Prateleira A" && feed.items[4].section=="Prateleira A" && feed.items[5].section=="Prateleira B" && feed.items[10].section=="Prateleira C","Home shelves: the shelf title travels as section on the wire");
    require(feed.items[0].dur==180 && feed.items[0].channel=="Canal f61shlfA","Home shelves: duration and channel come from the tile");
    int selected=0; feed.layout.build(feed.items,selected,4,false,2);
    require(feed.layout.rows.size()==3 && feed.layout.rows[0].carousel && feed.layout.rows[0].items.size()==5 && feed.layout.rows[0].heading=="Prateleira A" && feed.layout.rows[1].heading=="Prateleira B" && feed.layout.rows[2].heading=="Prateleira C","Home shelves: one scrolling row per shelf, in YouTube's order, with its title");
    feed.layout.build(feed.items,selected,4,false,0);
    require(feed.layout.rows.size()==4 && feed.layout.rows[0].heading.empty(),"Home shelves: without grouping the list stays a plain grid");
    feed.layout.build(feed.items,selected,4,false,2);
    selected=0; feed.layout.moveGrid(selected,1,0); feed.layout.moveGrid(selected,1,0); feed.layout.moveGrid(selected,0,1);
    require(selected==7,"Home shelves: D-pad keeps the column when moving to the next shelf");
    {  // um Short no meio ou no começo do dia não pode esconder o título do dia (#29)
      std::vector<Video> day; auto add=[&](const char* id,const char* section,bool shortForm) { Video v; v.id=id; v.section=section; v.shortForm=shortForm; day.push_back(v); };
      add("f61dayvid01","Hoje",false); add("f61dayshr01","Hoje",true); add("f61dayvid02","Hoje",false);
      add("f61dayshr02","Ontem",true); add("f61dayvid03","Ontem",false);
      ItemGrid grid; int sel=0; grid.build(day,sel,4,false,1);
      int headings=0; for (const auto& row:grid.rows) headings+=!row.heading.empty();
      require(headings==2 && grid.rows[0].heading=="Hoje","History days: a Short between videos of one day adds no heading, and the next day still gets its own");
    }
    {  // ao vivo só com a marca explícita; estreia agendada e vídeo sem duração nunca viram "AO VIVO" (nav e feed da conta)
      FeedMock live; live.account=true; live.expectedBrowse="FEsubscriptions";
      live.body=R"({"contents":[{"videoRenderer":{"videoId":"f61livvid01","title":{"simpleText":"Live"},"thumbnailOverlays":[{"thumbnailOverlayTimeStatusRenderer":{"style":"LIVE","text":{"simpleText":"LIVE"}}}]}},{"videoRenderer":{"videoId":"f61livvid02","title":{"simpleText":"No length"}}},{"videoRenderer":{"videoId":"f61livvid03","title":{"simpleText":"Normal"},"lengthText":{"simpleText":"4:05"}}}]})";
      yt::ConsoleAccount account(live,path);
      auto reply=account.request("/api/nav?kind=subscriptions",1000,10);
      NavPage page; page.kind="subscriptions";
      require(reply.ok() && Navigation::parse(reply.body,page) && page.items.size()==3,"live fixture lists three videos");
      require(page.items[0].duration<0 && page.items[1].duration==0 && page.items[2].duration==245,"nav: LIVE marker -> -1, no length -> 0 (unknown), 4:05 -> 245");
      auto feed=parseNativeFeed(account.request("/api/feed?kind=subs",1001,11));
      require(feed.items.size()==3 && feed.items[0].live && !feed.items[1].live && feed.items[1].dur==0 && !feed.items[2].live && feed.items[2].dur==245,"feed wire: live flag only with the marker");
    }
    {  // Histórico WEB: o título do dia vem em itemSectionRenderer.header.itemSectionHeaderRenderer
      FeedMock days; days.account=true; days.expectedBrowse="FEhistory";
      days.body=R"({"contents":[{"itemSectionRenderer":{"header":{"itemSectionHeaderRenderer":{"title":{"simpleText":"Hoje"}}},"contents":[{"videoRenderer":{"videoId":"f61hisvid01","title":{"simpleText":"A"}}}]}},{"itemSectionRenderer":{"header":{"itemSectionHeaderRenderer":{"title":{"simpleText":"Ontem"}}},"contents":[{"videoRenderer":{"videoId":"f61hisvid02","title":{"simpleText":"B"}}}]}}]})";
      yt::ConsoleAccount account(days,path);
      NavPage page; page.kind="history"; auto reply=account.request("/api/nav?kind=history",1000,10);
      require(reply.ok() && Navigation::parse(reply.body,page) && page.items.size()==2 && page.items[0].section=="Hoje" && page.items[1].section=="Ontem","History: day headings from itemSectionHeaderRenderer reach the NavItem section");
    }
    std::cout<<"PASS account home shelves: titled scrolling rows from the TV Home, continuation appends\n";
  }
  FeedMock variants; variants.account=true; variants.expectedBrowse="FEwhat_to_watch";
  variants.body=R"({"contents":[{"tileRenderer":{"contentId":"f52vid00201","onSelectCommand":{"watchEndpoint":{"videoId":"f52vid00201"}},"metadata":{"tileMetadataRenderer":{"title":{"simpleText":"TV Short"}}},"header":{"tileHeaderRenderer":{"thumbnailOverlays":[{"thumbnailOverlayTimeStatusRenderer":{"style":"SHORTS"}}]}}}},{"videoRenderer":{"videoId":"f52vid00202","title":{"simpleText":"Badge Short"},"badges":[{"metadataBadgeRenderer":{"style":"BADGE_STYLE_TYPE_SHORTS"}}]}},{"videoRenderer":{"videoId":"f52vid00203","title":{"simpleText":"Upcoming"},"isUpcoming":true}},{"reelItemRenderer":{"videoId":"f52vid00204","headline":{"simpleText":"Unplayable"},"isPlayable":false}}]})";
  yt::ConsoleAccount tv(variants,path); auto legacy=parseNativeFeed(tv.request("/api/feed?kind=home",1000,10));
  require(legacy.items.size()==2 && legacy.items[0].shortForm && legacy.items[1].shortForm,"TV Short styles are marked while upcoming/unplayable stay excluded");
  FeedMock channel; channel.account=true; channel.expectedBrowse="UC0000000000000000000001";
  channel.body=R"({"contents":[{"shelfRenderer":{"title":{"simpleText":"Videos"},"content":{"items":[{"videoRenderer":{"videoId":"f52vid00001","title":{"simpleText":"Video"}}}]}}},{"shelfRenderer":{"title":{"simpleText":"Shorts"},"content":{"items":[{"reelItemRenderer":{"videoId":"f52vid00101","headline":{"simpleText":"Channel Short"}}}]}}}]})";
  yt::ConsoleAccount channelAccount(channel,path); auto shorts=channelAccount.request("/api/nav?kind=channel&id=UC0000000000000000000001&tab=shorts",1000,10);
  NavPage tab; tab.kind="channel"; tab.tab="shorts";
  require(shorts.ok() && Navigation::parse(shorts.body,tab),"native channel Shorts listing parses"); tab.grid(4,true);
  require(tab.items.size()==1 && tab.items[0].shortForm && tab.rows.size()==1 && tab.rows[0].shorts,"native channel Shorts tab remains available in hide mode");
  std::cout<<"PASS account listings: subscription channels, TV overlays/badges, playability and channel Shorts tab\n";
}
