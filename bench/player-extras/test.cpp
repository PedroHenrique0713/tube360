#include "audio_tempo.hpp"
#include "yt/player_extras.hpp"
#include "player_ui.hpp"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <fstream>
#include <sstream>

void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
using namespace yt::extras;
using namespace yt::catalog;
struct Fake : yt::Transport {
  int calls=0, nextCalls=0; std::string root, page, response, votesAgent; std::vector<std::string> bodies, sequence;
  yt::Response send(const yt::Request& r) override {
    if (r.url.find("/votes?")!=std::string::npos) votesAgent=r.agent;
    return request(r.url,r.body,r.form);
  }
  yt::Response request(const std::string& url,const std::string& body,bool) override {
    ++calls;
    if (url.find("/votes?")!=std::string::npos) return {200,response};
    if (url.find("/youtubei/")!=std::string::npos) {
      ++nextCalls; bodies.push_back(body);
      if (!sequence.empty()) { auto value=sequence.front(); sequence.erase(sequence.begin()); return {200,value}; }
      return {200,page};
    }
    return {200,root};
  }
};
void parser() {
  Fake net; net.root=R"(<script>ytcfg.set({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"fixture"}}});</script>)";
  net.page=R"({"contents":{"itemSectionRenderer":{"sectionIdentifier":"comment-item-section","contents":[{"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"FIRST"}}}}]}}})";
  yt::extras::Client client(net);
  auto first=client.comments("fixture0001","","pt");
  require(first.continuation.empty() && net.nextCalls==2,"empty repeated continuation stops");
  J doc;
  require(parse(R"({"onResponseReceivedEndpoints":[{"appendContinuationItemsAction":{"targetId":"comments-section","continuationItems":[{"commentThreadRenderer":{"comment":{"commentRenderer":{"commentId":"old","authorText":{"simpleText":"Ana"},"contentText":{"runs":[{"text":"Olá "},{"text":"mundo"}]},"voteCount":{"simpleText":"21"},"publishedTimeText":{"simpleText":"há 2 dias"}}},"replies":{"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"WRONG_REPLY"}}}}}},{"commentThreadRenderer":{"commentViewModel":{"commentViewModel":{"commentId":"new","commentKey":"entity"}}}},{"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"SECOND"}}}}]}}],"frameworkUpdates":{"entityBatchUpdate":{"mutations":[{"entityKey":"entity","payload":{"commentEntityPayload":{"properties":{"commentId":"new","content":{"content":"Moderno"},"publishedTime":"há 1 hora"},"author":{"displayName":"Bruno"},"toolbar":{"likeCountNotliked":"7"}}}}]}}})",doc),"fixture JSON");
  auto page=parseComments(doc);
  require(page.items.size()==2 && page.continuation=="SECOND","both renderer types and reply token isolation");
  require(page.items[0].author=="Ana" && page.items[0].text=="Olá mundo" && page.items[0].likes=="21" && page.items[0].time=="há 2 dias","legacy metadata");
  require(page.items[1].author=="Bruno" && page.items[1].text=="Moderno" && page.items[1].likes=="7","entity metadata");
  net.sequence={R"({"onResponseReceivedEndpoints":[{"appendContinuationItemsAction":{"targetId":"comments-section","continuationItems":[{"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"AFTER_EMPTY"}}}}]}}]})",doc.serialize()};
  require(client.comments("fixture0001","EMPTY_START","pt").items.size()==2 && net.sequence.empty(),"empty page continues to visible comments");
  net.page=doc.serialize(); page=client.comments("fixture0001","FIRST","en");
  require(page.items.size()==2 && net.bodies.back().find("\"continuation\":\"FIRST\"")!=std::string::npos,"continuation POST");
  require(net.bodies.back().find("\"hl\":\"en\"")!=std::string::npos,"language context");
  require(client.comments("fixture0001","FIRST","es").items.size()==2,"Spanish comments");
  require(net.bodies.back().find("\"hl\":\"es\"")!=std::string::npos && net.bodies.back().find("\"gl\":\"MX\"")!=std::string::npos,"Spanish comments locale on the wire");
  net.page=R"({"contents":{"itemSectionRenderer":{"sectionIdentifier":"related-items","contents":[{"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"WRONG_RELATED"}}}}]}}})";
  require(client.comments("fixture0001","","en").continuation.empty(),"related token isolation");
  net.page="{"; require(!client.comments("fixture0001","","en").error.empty(),"malformed JSON fails");
  net.response=R"({"id":"fixture0001","likes":123,"dislikes":0})";
  auto votes=client.votes("fixture0001",1000); int calls=net.calls;
  require(votes.available && votes.dislikes==0,"zero votes is valid");
  require(!net.votesAgent.empty(),"public votes request carries its browser identity");
  require(client.votes("fixture0001",300999).available && net.calls==calls,"5 minute cache");
  net.response=R"({"id":"fixture0001","likes":123,"dislikes":-1})";
  require(!client.votes("fixture0001",301000).available && net.calls==calls+1,"expired invalid vote silently unavailable");
  calls=net.calls; client.votes("fixture0001",302000); require(net.calls==calls,"negative cache");
  client.votes("bad&videoId=oops",400000); require(net.calls==calls,"invalid ID sends no request");
  require(!parse(std::string(81,'{'),doc),"depth guard");
  PlayerUI ui; ui.reset(0); ui.options=true; ui.tracks=false;
  for (int option : {PlayerUI::COMMENTS,PlayerUI::SPEED,PlayerUI::LOOP}) {
    ui.selected=option-1; ui.direction(1,1,0); require(ui.selected==option,"controller reaches extras");
    ui.panel=PlayerUI::COMMENTS_PANEL; require(ui.back(2) && ui.panel==PlayerUI::NONE,"B closes comments");
  }
  std::puts("PASS comments: legacy/entity metadata, initial/next pages, reply/related token isolation, language, errors; votes TTL/zero/invalid; controller");
}
void tempo() {
  constexpr int frames=44100*3;
  std::vector<float> input(frames*2);
  for (int i=0;i<frames;++i) { input[i*2]=.6f*std::sin(i*2*3.141592653589793*440/44100); input[i*2+1]=.4f*std::sin(i*2*3.141592653589793*660/44100); }
  for (double speed : audio::speeds) {
    audio::Tempo stretch; stretch.reset(speed); std::vector<float> output;
    auto sink=[&](const float* pcm,int n) { output.insert(output.end(),pcm,pcm+n*2); return true; };
    auto start=std::chrono::steady_clock::now();
    for (int i=0;i<frames;i+=1024) require(stretch.push(input.data()+i*2,std::min(1024,frames-i),sink),"streaming tempo");
    require(stretch.push(nullptr,0,sink,true),"flush tempo");
    require(std::abs(int64_t(output.size()/2)-std::llround(frames/speed))<=1,"tempo duration");
    for (float v : output) require(std::isfinite(v) && std::fabs(v)<1.01,"PCM finite/clipped");
    auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    for (int channel=0;channel<2;++channel) {
      int crossings=0, first=4410, end=(int)output.size()/2-4410;
      for (int i=first+1;i<end;++i) if (output[(i-1)*2+channel]<=0 && output[i*2+channel]>0) ++crossings;
      double hz=crossings*44100.0/(end-first);
      require(std::abs(hz-(channel ? 660 : 440))<8,"pitch preserved in both stereo channels");
      std::printf("tempo %.2fx channel %d: %.2f Hz, duration %.4f s, %.2f ms CPU-wall\n",speed,channel,hz,output.size()/88200.,ms);
    }
  }
}
int main(int argc,char** argv) {
  try {
    if (argc==3 && std::string(argv[1])=="--shape") {
      std::ifstream f(argv[2]); std::ostringstream s; s<<f.rdbuf(); J root;
      require(bool(f) && parse(s.str(),root),"recorded public response");
      auto page=parseComments(root); require(!page.items.empty() && !page.continuation.empty(),"public comments and continuation");
      std::printf("PASS recorded public response: %zu comments, continuation available\n",page.items.size()); return 0;
    }
    if (argc==3 && std::string(argv[1])=="--public") {
      auto net=yt::https();
      struct Trace : yt::Transport {
        yt::Transport& net;
        explicit Trace(yt::Transport& n) : net(n) {}
        yt::Response request(const std::string& u,const std::string& b,bool f) override { return send({u,b,{},f,{}}); }
        yt::Response send(const yt::Request& q) override {
          auto r=net.send(q);
          if (q.url.find("/next?")!=std::string::npos) if (const char* path=std::getenv("XT_EXTRAS_TRACE")) std::ofstream(path)<<r.body;
          return r;
        }
      } trace(*net);
      yt::extras::Client client(trace);
      auto page=client.comments(argv[2],"","pt");
      std::printf("public comments: %zu, continuation=%d, error=%s\n",page.items.size(),!page.continuation.empty(),page.error.c_str());
      auto votes=client.votes(argv[2],0); std::printf("RYD TLS: available=%d likes=%lld dislikes=%lld\n",votes.available,(long long)votes.likes,(long long)votes.dislikes);
      return !page.error.empty() || page.items.empty();
    }
    parser(); tempo();
  } catch (const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; }
}
