#include "player_extras.hpp"
#include <stdexcept>
#include <cstdio>

namespace {
std::atomic_bool firstStarted{false}, releaseFirst{false};
std::atomic_int pageCalls{0};
struct Fixture : yt::Transport {
  yt::Response request(const std::string& url,const std::string& body,bool) override {
    if (url.find("/votes?")!=std::string::npos) {
      if (url.find("fixture0001")!=std::string::npos) {
        firstStarted=true; while (!releaseFirst) SDL_Delay(1);
        return {200,R"({"id":"fixture0001","likes":10,"dislikes":1})"};
      }
      return {200,R"({"id":"fixture0002","likes":500,"dislikes":25})"};
    }
    if (url.find("/next?")!=std::string::npos) {
      if (++pageCalls==2) return {503,"","fixture offline"};
      const char* text=pageCalls==1 ? "First" : "Second";
      return {200,std::string(R"({"onResponseReceivedEndpoints":[{"reloadContinuationItemsCommand":{"targetId":"comments-section","slot":"RELOAD_CONTINUATION_SLOT_HEADER","continuationItems":[]}},{"appendContinuationItemsAction":{"targetId":"comments-section","continuationItems":[{"commentThreadRenderer":{"comment":{"commentRenderer":{"commentId":")")+text+R"(","authorText":{"simpleText":"Author"},"contentText":{"simpleText":")"+text+R"("}}}}},{"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"MORE"}}}}]}}]})"};
    }
    return {200,R"({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"fixture"}}})"};
  }
};
void check(bool value,const char* msg) { if (!value) throw std::runtime_error(msg); }
template<class F> void waitFor(F predicate) {
  auto until=SDL_GetTicks()+3000;
  while (!predicate() && SDL_GetTicks()<until) SDL_Delay(1);
  check(predicate(),"worker timed out");
}
}
namespace yt { std::unique_ptr<Transport> https(const std::atomic_bool*) { return std::make_unique<Fixture>(); } }
int main() {
  SDL_Init(0);
  try {
    PlayerExtras extras; extras.init(); extras.select("fixture0001","pt");
    waitFor([]{ return firstStarted.load(); });
    extras.select("fixture0002","en"); releaseFirst=true;
    waitFor([&]{ extras.poll(); return extras.votes.available; });
    check(extras.votes.likes==500 && extras.votes.dislikes==25,"stale votes replaced current video");
    extras.request(); waitFor([&]{ extras.poll(); return !extras.loading; });
    if (extras.page.items.size()!=1) std::fprintf(stderr,"comments diagnostic: items=%zu token=%s error=%s calls=%d\n",extras.page.items.size(),extras.page.continuation.c_str(),extras.page.error.c_str(),pageCalls.load());
    check(extras.page.items.size()==1 && extras.page.items[0].text=="First" && extras.page.continuation=="MORE","first comments page");
    extras.request(true); waitFor([&]{ extras.poll(); return !extras.loading; });
    check(!extras.page.error.empty() && extras.page.items[0].text=="First" && extras.page.continuation=="MORE","failed continuation lost retry token/current page");
    extras.request(true); waitFor([&]{ extras.poll(); return !extras.loading; });
    check(extras.page.error.empty() && extras.page.items[0].text=="Second","continuation retry failed");
    extras.close();
    std::puts("PASS worker: stale response fencing, header/body commands, continuation failure retains page/token, retry succeeds, clean shutdown");
  } catch (const std::exception& e) { releaseFirst=true; std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; }
  SDL_Quit();
}
