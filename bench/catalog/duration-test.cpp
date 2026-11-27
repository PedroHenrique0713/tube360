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
  std::cout << "3 duration layouts parsed\n";
}
