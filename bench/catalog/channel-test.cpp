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
static const char* POPULAR = R"({"onResponseReceivedActions":[{"reloadContinuationItemsCommand":{"continuationItems":[{"richItemRenderer":{"content":{"videoRenderer":{"videoId":"CCCCCCCCCCC","title":{"simpleText":"Popular"},"publishedTimeText":{"simpleText":"há 3 anos"},"viewCountText":{"simpleText":"9 mi de visualizações"},"ownerBadges":[{"metadataBadgeRenderer":{"style":"BADGE_STYLE_TYPE_VERIFIED"}}],"detailedMetadataSnippets":[{"snippetText":{"runs":[{"text":"trecho da descrição"}]}}]}}}}]}}]})";

struct Http : yt::Transport {
  std::vector<string> bodies; std::vector<bool> auth; bool refuseAuth400 = true;
  yt::Response request(const string& url, const string& body = {}, bool form = false) override { yt::Request r; r.url=url; r.body=body; return send(r); }
  yt::Response send(const yt::Request& r) override {
    if (r.url == "https://www.youtube.com/") return {200,R"({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"2.0"}}})",{}};
    bool bearer = false; for (const auto& h : r.headers) if (h.rfind("Authorization:",0)==0) bearer = true;
    bodies.push_back(r.body); auth.push_back(bearer);
    // O WEB recusa o Bearer da TV com 400 nas abas do canal (log do console de 2026-11-26).
    if (bearer && r.body.find("params")!=string::npos) return {400,"{}",{}};
    if (r.body.find("TOK1")!=string::npos) return {200,POPULAR,{}};
    if (r.body.find("VIDP")!=string::npos) return {200,VIDEOS,{}};
    return {200,HOME,{}};
  }
};

int main() {
  Http h; Client client(h); client.setToken("fake-a");
  auto home = client.browse({{"kind","channel"},{"id","UCaaaaaaaaaaaaaaaaaaaaaa"},{"tab","home"},{"lang","pt"}});
  check(home.error.empty(),"Home tab loads");
  check(home.verified,"header verified check read from the attachment run");
  check(home.description=="Bio do canal","header description");
  check(home.subtitle.find("@canalteste")!=string::npos && home.subtitle.find("403 vídeos")!=string::npos,"subtitle carries handle, subscribers and video count");
  check(!home.banner.empty(),"banner kept");
  check(home.items.size()==1 && home.items[0].id=="AAAAAAAAAAA" && home.items[0].section=="Populares","Home tab keeps the creator's section");
  auto w = wire(home); auto head = w.substr(0,w.find('\n'));
  check(head.size()>=2 && head.find("\t1\tBio do canal")!=string::npos,"wire header carries verified and description");

  h.bodies.clear(); h.auth.clear();
  auto vids = client.browse({{"kind","channel"},{"id","UCaaaaaaaaaaaaaaaaaaaaaa"},{"tab","videos"},{"lang","pt"}});
  check(vids.error.empty() && vids.items.size()==1 && vids.items[0].id=="BBBBBBBBBBB","Videos tab loads, no 400 with the TV token");
  check(h.auth.size()==2 && h.auth[0] && !h.auth[1],"tab request goes without Bearer (public content)");

  h.bodies.clear(); h.auth.clear();
  auto pop = client.browse({{"kind","channel"},{"id","UCaaaaaaaaaaaaaaaaaaaaaa"},{"tab","videos"},{"sort","1"},{"lang","pt"}});
  check(pop.error.empty() && pop.items.size()==1 && pop.items[0].id=="CCCCCCCCCCC","sort Popular follows the chip continuation");
  check(h.bodies.back().find("TOK1")!=string::npos && !h.auth.back(),"sort request uses the chip token, public");
  check(pop.verified && pop.title.find("Canal")==0,"header survives the sort continuation");
  const auto& it = pop.items[0];
  check(it.verified && it.date=="há 3 anos" && it.views=="9 mi de visualizações" && it.description=="trecho da descrição","result card fields: verified, date, views, description");
  auto pw = wire(pop); auto row = pw.substr(pw.find('\n')+1);
  check(row.find("\thá 3 anos\t1\t9 mi de visualizações\ttrecho da descrição\n")!=string::npos,"wire item carries date, verified, views, description");
  std::cout << "ok " << checks << " checks\n";
}
