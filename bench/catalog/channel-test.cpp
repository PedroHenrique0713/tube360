#include "yt/catalog.hpp"
#include <iostream>
using namespace yt::catalog;
using yt::string;
int checks = 0;
void check(bool ok, const char* message) { ++checks; if (!ok) { std::cerr << "FAIL " << message << '\n'; std::exit(1); } }

// Canal sintético no formato do YouTube de 2026-11 (pageHeaderViewModel + abas + chips de ordenação).
static const char* HOME = R"({"header":{"pageHeaderRenderer":{"content":{"pageHeaderViewModel":{
 "title":{"dynamicTextViewModel":{"text":{"content":"Canal Teste","attachmentRuns":[{"element":{"type":{"imageType":{"image":{"sources":[{"clientResource":{"imageName":"CHECK_CIRCLE_FILLED"}}]}}}}}]}}},
 "image":{"decoratedAvatarViewModel":{"avatar":{"avatarViewModel":{"image":{"sources":[{"url":"https://yt3.googleusercontent.com/a=s72","width":72,"height":72}]}}}}},
 "metadata":{"contentMetadataViewModel":{"metadataRows":[{"metadataParts":[{"text":{"content":"@canalteste"}}]},{"metadataParts":[{"text":{"content":"1,2 mi de inscritos"}},{"text":{"content":"403 vídeos"}}]}]}},
 "description":{"descriptionPreviewViewModel":{"description":{"content":"Bio do canal"}}},
 "banner":{"imageBannerViewModel":{"image":{"sources":[{"url":"https://yt3.googleusercontent.com/b=w1138","width":1138,"height":188}]}}}}}}},
 "contents":{"twoColumnBrowseResultsRenderer":{"tabs":[
  {"tabRenderer":{"title":"Início","selected":true,"endpoint":{"commandMetadata":{"webCommandMetadata":{"url":"/@canalteste/featured"}},"browseEndpoint":{"browseId":"UCaaaaaaaaaaaaaaaaaaaaaa","params":"HOMEP"}},"content":{"sectionListRenderer":{"contents":[{"itemSectionRenderer":{"contents":[{"shelfRenderer":{"title":{"simpleText":"Populares"},"content":{"horizontalListRenderer":{"items":[{"gridVideoRenderer":{"videoId":"AAAAAAAAAAA","title":{"simpleText":"V"}}}]}}}}]}}]}}}},
  {"tabRenderer":{"title":"Vídeos","endpoint":{"commandMetadata":{"webCommandMetadata":{"url":"/@canalteste/videos"}},"browseEndpoint":{"browseId":"UCaaaaaaaaaaaaaaaaaaaaaa","params":"VIDP"}}}}]}}})";
static const char* VIDEOS = R"({"contents":{"twoColumnBrowseResultsRenderer":{"tabs":[{"tabRenderer":{"title":"Vídeos","selected":true,"endpoint":{"commandMetadata":{"webCommandMetadata":{"url":"/@canalteste/videos"}},"browseEndpoint":{"browseId":"UCaaaaaaaaaaaaaaaaaaaaaa","params":"VIDP"}},
 "content":{"richGridRenderer":{"header":{"chipBarViewModel":{"chips":[
   {"chipViewModel":{"text":"Mais recentes","selected":true,"tapCommand":{"innertubeCommand":{"continuationCommand":{"token":"TOK0"}}}}},
   {"chipViewModel":{"text":"Em alta","tapCommand":{"innertubeCommand":{"continuationCommand":{"token":"TOK1"}}}}},
   {"chipViewModel":{"text":"Mais antigos","tapCommand":{"innertubeCommand":{"continuationCommand":{"token":"TOK2"}}}}}]}},
  "contents":[{"richItemRenderer":{"content":{"videoRenderer":{"videoId":"BBBBBBBBBBB","title":{"simpleText":"Recente"}}}}}]}}}}]}}})";
static const char* SEARCH = R"({"contents":{"twoColumnSearchResultsRenderer":{"primaryContents":{"sectionListRenderer":{"contents":[{"itemSectionRenderer":{"contents":[
 {"lockupViewModel":{"contentId":"DDDDDDDDDDD","contentType":"LOCKUP_CONTENT_TYPE_VIDEO","contentImage":{"thumbnailViewModel":{"overlays":[{"thumbnailBottomOverlayViewModel":{"badges":[{"thumbnailBadgeViewModel":{"text":"12:37"}}]}}]}},
  "metadata":{"lockupMetadataViewModel":{"title":{"content":"Lockup video"},"metadata":{"contentMetadataViewModel":{"metadataRows":[{"metadataParts":[
    {"avatarStack":{"avatarStackViewModel":{"text":{"content":"Mr.Canal"}}},"icon":{"accessibilityLabel":"Verified","ytIcon":{}}},
    {"text":{"content":"6.4K"},"accessibilityLabel":"6.4 thousand views","leadingIcon":{"name":"PLAY_ARROW_OUTLINED"}},
    {"text":{"content":"19h ago"},"accessibilityLabel":"19 hours ago"}]}]}}}}}},
 {"videoRenderer":{"videoId":"EEEEEEEEEEE","title":{"simpleText":"Sem selo de tempo"}}},
 {"videoRenderer":{"videoId":"FFFFFFFFFFF","title":{"simpleText":"Ao vivo"},"thumbnailOverlays":[{"thumbnailOverlayTimeStatusRenderer":{"style":"LIVE","text":{"runs":[{"text":"LIVE"}]}}}]}},
 {"channelRenderer":{"channelId":"UCbbbbbbbbbbbbbbbbbbbbbb","title":{"simpleText":"Canal"},"subscriptionButton":{"subscribed":true},"subscriberCountText":{"simpleText":"@canal"},"videoCountText":{"simpleText":"1 mil inscritos"}}}]}}]}}}}})";
static const char* POPULAR = R"({"onResponseReceivedActions":[{"reloadContinuationItemsCommand":{"continuationItems":[{"richItemRenderer":{"content":{"videoRenderer":{"videoId":"CCCCCCCCCCC","title":{"simpleText":"Popular"},"publishedTimeText":{"simpleText":"há 3 anos"},"viewCountText":{"simpleText":"9 mi de visualizações"},"ownerBadges":[{"metadataBadgeRenderer":{"style":"BADGE_STYLE_TYPE_VERIFIED"}}],"detailedMetadataSnippets":[{"snippetText":{"runs":[{"text":"trecho da descrição"}]}}]}}}}]}}]})";

struct Http : yt::Transport {
  // refuse: 1 = o WEB recusa o Bearer da TV com 400 só nos pedidos com params/continuation (aba, ordenação)
  //         2 = o WEB recusa o Bearer da TV com 400 em QUALQUER browse (modelo F47, o que o log do console mostra)
  std::vector<string> bodies; std::vector<bool> auth; int refuse = 1; int refused = 0;
  yt::Response request(const string& url, const string& body = {}, bool form = false) override { yt::Request r; r.url=url; r.body=body; return send(r); }
  yt::Response send(const yt::Request& r) override {
    if (r.url == "https://www.youtube.com/") return {200,R"({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"2.0"}}})",{}};
    bool bearer = false; for (const auto& h : r.headers) if (h.rfind("Authorization:",0)==0) bearer = true;
    bodies.push_back(r.body); auth.push_back(bearer);
    // O WEB recusa o Bearer da TV com 400 nas abas do canal (log do console de 2026-11-26).
    if (bearer && (refuse==2 || r.body.find("params")!=string::npos || r.body.find("continuation")!=string::npos)) { ++refused; return {400,R"({"error":{"code":400}})",{}}; }
    if (r.body.find("SEARCHQ")!=string::npos) return {200,SEARCH,{}};
    if (r.body.find("TOK1")!=string::npos) return {200,POPULAR,{}};
    if (r.body.find("VIDP")!=string::npos) return {200,VIDEOS,{}};
    return {200,HOME,{}};
  }
};

int main() {
  // Modelo 1: o 400 só atinge aba/ordenação. Modelo 2: qualquer browse com Bearer dá 400 (inclusive a raiz do canal).
  for (int model : {1,2}) {
    Http h; h.refuse = model; Client client(h); client.setToken("fake-a");
    auto home = client.browse({{"kind","channel"},{"id","UCaaaaaaaaaaaaaaaaaaaaaa"},{"tab","home"},{"lang","pt"}});
    check(home.error.empty(),"Home tab loads");
    check(home.verified,"header verified check read from the attachment run");
    check(home.description=="Bio do canal","header description");
    check(home.subtitle.find("@canalteste")!=string::npos && home.subtitle.find("403 vídeos")!=string::npos,"subtitle carries handle, subscribers and video count");
    check(!home.banner.empty(),"banner kept");
    check(home.items.size()==1 && home.items[0].id=="AAAAAAAAAAA" && home.items[0].section=="Populares","Home tab keeps the creator's section");
    check(!home.sortable,"Home tab has no sort chips");
    check(!home.subscribedKnown && !home.subscribed,"signed in but the page has no subscription flag (public/refused): state is unknown, never shown as false");
    check(wire(home).substr(0,wire(home).find('\n')).size()>2 && wire(home).substr(0,wire(home).find('\n')).back()=='0',"wire header says the subscription state is unknown");
    auto w = wire(home); auto head = w.substr(0,w.find('\n'));
    check(head.find("\t1\tBio do canal")!=string::npos,"wire header carries verified and description");

    h.bodies.clear(); h.auth.clear(); h.refused = 0;
    auto vids = client.browse({{"kind","channel"},{"id","UCaaaaaaaaaaaaaaaaaaaaaa"},{"tab","videos"},{"lang","pt"}});
    check(vids.error.empty() && vids.items.size()==1 && vids.items[0].id=="BBBBBBBBBBB","Videos tab loads, no 400 reaches the user");
    check(vids.sortable,"Videos tab reports the sort chips");
    if (model==1) check(h.auth.size()==2 && !h.auth[1],"tab request goes without Bearer");
    if (model==2) check(h.refused==0,"after the first refusal the session stops sending the Bearer (no more 400s)");

    h.bodies.clear(); h.auth.clear(); h.refused = 0;
    auto pop = client.browse({{"kind","channel"},{"id","UCaaaaaaaaaaaaaaaaaaaaaa"},{"tab","videos"},{"sort","1"},{"lang","pt"}});
    check(pop.error.empty() && pop.items.size()==1 && pop.items[0].id=="CCCCCCCCCCC","sort Trending follows the chip continuation");
    check(h.bodies.back().find("TOK1")!=string::npos && !h.auth.back(),"sort request uses the chip token, public");
    check(pop.verified && pop.sortable && pop.title.find("Canal")==0,"header and sort chips survive the sort continuation");
    const auto& it = pop.items[0];
    check(it.verified && it.date=="há 3 anos" && it.views=="9 mi de visualizações" && it.description=="trecho da descrição","result card fields: verified, date, views, description");
    auto pw = wire(pop); auto row = pw.substr(pw.find('\n')+1);
    check(row.find("\thá 3 anos\t1\t9 mi de visualizações\ttrecho da descrição\n")!=string::npos,"wire item carries date, verified, views, description");
    check(pw.substr(0,pw.find('\n')).find("\t1\t")!=string::npos,"wire header carries the sortable flag");
  }
  {  // sem conta: nunca inscrito, então o estado vale
    Http h; Client client(h);
    auto home = client.browse({{"kind","channel"},{"id","UCaaaaaaaaaaaaaaaaaaaaaa"},{"tab","home"},{"lang","pt"}});
    check(home.error.empty() && home.subscribedKnown && !home.subscribed,"guest: not subscribed is a known state");
  }
  {  // busca: lockupViewModel (formato real), duração desconhecida x ao vivo, resultado de canal
    Http h; Client client(h);
    auto s = client.browse({{"kind","search"},{"q","SEARCHQ"},{"lang","en"}});
    check(s.error.empty() && s.items.size()==4,"search page parses");
    const auto& l = s.items[0];
    check(l.id=="DDDDDDDDDDD" && l.channel=="Mr.Canal" && l.verified && l.views=="6.4 thousand views" && l.date=="19 hours ago" && l.duration==757,"lockupViewModel keeps channel, verified, views, date and duration");
    check(s.items[1].duration==-1,"video without a time badge has unknown duration, not 0 (no false LIVE)");
    check(s.items[2].duration==0,"video with the live mark keeps 0 (LIVE)");
    check(s.items[3].type=="channel" && s.items[3].date=="@canal","channel result keeps the handle (search is public: it carries no subscription state)");
    auto w = wire(s); check(w.find("\t1\t6.4 thousand views\t")!=string::npos,"search wire carries the lockup metadata");
  }
  {  // curso (lockup COURSE): o 1º texto é o canal, nunca a data
    J course;
    check(parse(R"({"contentId":"PLcourse0001","contentType":"LOCKUP_CONTENT_TYPE_COURSE","metadata":{"lockupMetadataViewModel":{"title":{"content":"Aulas"},"metadata":{"contentMetadataViewModel":{"metadataRows":[{"metadataParts":[{"text":{"content":"Canal USP"}}]},{"metadataParts":[{"text":{"content":"Curso"}}]}]}}}}})",course),"course lockup parses");
    auto it = item(course,"lockupViewModel",false,"");
    check(it.channel=="Canal USP" && it.date.empty(),"course lockup keeps the channel out of the date field");
  }
  {  // lockupViewModel do cliente TV: badgeStyle ..._LIVE = ao vivo (0); sem selo de tempo nem de live = desconhecida (-1)
    J live, plain;
    check(parse(R"({"contentId":"GGGGGGGGGGG","contentType":"LOCKUP_CONTENT_TYPE_VIDEO","contentImage":{"thumbnailViewModel":{"overlays":[{"thumbnailBottomOverlayViewModel":{"badges":[{"thumbnailBadgeViewModel":{"text":"AO VIVO","badgeStyle":"THUMBNAIL_OVERLAY_BADGE_STYLE_LIVE"}}]}}]}}})",live),"live lockup parses");
    check(parse(R"({"contentId":"HHHHHHHHHHH","contentType":"LOCKUP_CONTENT_TYPE_VIDEO"})",plain),"plain lockup parses");
    check(yt::liveMark(live) && !yt::liveMark(plain),"shared liveMark sees the TV lockup live badge");
    check(item(live,"lockupViewModel",false,"").duration==0 && item(plain,"lockupViewModel",false,"").duration==-1,"live lockup is 0, unknown lockup is -1");
  }
  std::cout << "ok " << checks << " checks\n";
}
