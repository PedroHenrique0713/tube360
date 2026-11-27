// Duração em feeds da conta: Inscrições manda o selo em thumbnailOverlays, sem lengthText (#8).
#include "yt/catalog.hpp"
#include <iostream>
using namespace yt::catalog;
int main() {
  const char* cases[] = {
    R"({"videoId":"abcdefghijk","title":{"runs":[{"text":"T"}]},"thumbnailOverlays":[{"thumbnailOverlayTimeStatusRenderer":{"text":{"simpleText":"12:34"}}}]})",
    R"({"videoId":"abcdefghijk","title":{"runs":[{"text":"T"}]},"lengthText":{"simpleText":"1:02:03"}})",
    R"({"videoId":"abcdefghijk","title":{"runs":[{"text":"T"}]},"thumbnailOverlays":[{"thumbnailBottomOverlayViewModel":{"badges":[{"thumbnailBadgeViewModel":{"text":"3:21"}}]}}]})",
  };
  int expect[] = {754, 3723, 201};
  for (int i = 0; i < 3; i++) {
    J v; if (!parse(cases[i], v)) { std::cerr << "FAIL parse " << i << '\n'; return 1; }
    auto it = item(v, "gridVideoRenderer", false, "");
    if (it.type != "video" || it.duration != expect[i]) { std::cerr << "FAIL case " << i << " duration=" << it.duration << '\n'; return 1; }
  }
  // Convenção: 0 = ao vivo (só com a marca, yt::liveMark, inclusive o badgeStyle do cliente TV), -1 = desconhecida, > 0 = segundos.
  struct Live { const char* json; int expect; } lives[] = {
    {R"({"videoId":"abcdefghijk","title":{"runs":[{"text":"T"}]},"thumbnailOverlays":[{"thumbnailOverlayTimeStatusRenderer":{"style":"LIVE","text":{"simpleText":"LIVE"}}}]})", 0},
    {R"({"videoId":"abcdefghijk","title":{"runs":[{"text":"T"}]},"badges":[{"metadataBadgeRenderer":{"style":"BADGE_STYLE_TYPE_LIVE_NOW"}}]})", 0},
    {R"({"videoId":"abcdefghijk","title":{"runs":[{"text":"T"}]},"thumbnailViewModel":{"overlays":[{"thumbnailBadgeViewModel":{"badgeStyle":"THUMBNAIL_OVERLAY_BADGE_STYLE_LIVE","text":"LIVE"}}]}})", 0},
    {R"({"videoId":"abcdefghijk","title":{"runs":[{"text":"T"}]}})", -1},
  };
  for (const auto& c : lives) {
    J v; if (!parse(c.json, v)) { std::cerr << "FAIL parse live\n"; return 1; }
    auto it = item(v, "videoRenderer", false, "");
    if (it.duration != c.expect) { std::cerr << "FAIL live marker duration=" << it.duration << " expected " << c.expect << '\n'; return 1; }
  }
  // O título do dia do Histórico (WEB) vem em itemSectionRenderer.header.itemSectionHeaderRenderer.title.
  J page; const char* history = R"({"contents":[{"itemSectionRenderer":{"header":{"itemSectionHeaderRenderer":{"title":{"simpleText":"Hoje"}}},"contents":[{"videoRenderer":{"videoId":"abcdefghijk","title":{"runs":[{"text":"A"}]}}}]}},{"itemSectionRenderer":{"header":{"itemSectionHeaderRenderer":{"title":{"simpleText":"Ontem"}}},"contents":[{"videoRenderer":{"videoId":"bcdefghijkl","title":{"runs":[{"text":"B"}]}}}]}}]})";
  if (!parse(history, page)) { std::cerr << "FAIL parse history\n"; return 1; }
  auto decoded = decode(page, false);
  if (decoded.items.size() != 2 || decoded.items[0].section != "Hoje" || decoded.items[1].section != "Ontem") { std::cerr << "FAIL history day headings\n"; return 1; }
  // #15: a URL do feed (hqdefault.jpg?sqp=..., vi_webp/...webp, //i.ytimg.com/...) vira a capa HD hq720.jpg do mesmo vídeo; o que não é capa de vídeo do ytimg não vira nada.
  struct Hd { const char* in; const char* out; } hds[] = {
    {"https://i.ytimg.com/vi/abcdefghijk/hqdefault.jpg?sqp=-oaymw&rs=AOn4", "https://i.ytimg.com/vi/abcdefghijk/hq720.jpg"},
    {"https://i.ytimg.com/vi_webp/abcdefghijk/hq720.webp", "https://i.ytimg.com/vi/abcdefghijk/hq720.jpg"},
    {"//i.ytimg.com/vi/abcdefghijk/oar2.jpg?x=1", "https://i.ytimg.com/vi/abcdefghijk/hq720.jpg"},
    {"https://yt3.ggpht.com/avatar=s88", ""},
    {"https://example.com/vi/abcdefghijk/hqdefault.jpg", ""},
  };
  for (const auto& c : hds) if (hdThumbnail(c.in) != c.out) { std::cerr << "FAIL hdThumbnail " << c.in << " -> " << hdThumbnail(c.in) << '\n'; return 1; }
  std::cout << "3 duration layouts, 4 live-mark cases and 2 history day headings and 5 HD thumbnail URLs parsed\n";
}
