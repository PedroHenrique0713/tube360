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
  // Ao vivo só com a marca explícita; sem duração e sem marca fica 0 (desconhecida, nunca "AO VIVO").
  struct Live { const char* json; int expect; } lives[] = {
    {R"({"videoId":"abcdefghijk","title":{"runs":[{"text":"T"}]},"thumbnailOverlays":[{"thumbnailOverlayTimeStatusRenderer":{"style":"LIVE","text":{"simpleText":"LIVE"}}}]})", -1},
    {R"({"videoId":"abcdefghijk","title":{"runs":[{"text":"T"}]},"badges":[{"metadataBadgeRenderer":{"style":"BADGE_STYLE_TYPE_LIVE_NOW"}}]})", -1},
    {R"({"videoId":"abcdefghijk","title":{"runs":[{"text":"T"}]}})", 0},
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
  std::cout << "3 duration layouts, 3 live-marker cases and 2 history day headings parsed\n";
}
