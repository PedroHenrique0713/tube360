#include "sponsorblock.hpp"
#include "settings.hpp"
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <filesystem>

using namespace sponsorblock;
using K=i18n::Key;
int checks=0;
void check(bool ok,const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
std::string read(const char* path) { std::ifstream f(path); check(bool(f),"fixture exists"); return {std::istreambuf_iterator<char>(f),{}}; }
struct Net : yt::Transport {
    int calls=0; yt::Response reply;
    yt::Response request(const std::string&,const std::string&,bool) override { throw std::runtime_error("send required for body limit"); }
    yt::Response send(const yt::Request& r) override {
        ++calls;
        check(r.url=="https://sponsor.ajay.app/api/skipSegments/5f6b?categories=%5B%22sponsor%22%2C%22selfpromo%22%2C%22interaction%22%5D&actionTypes=%5B%22skip%22%5D","private API path and exact default categories/action");
        check(r.body.empty() && r.headers.empty() && r.maxBytes==512*1024,"bounded unauthenticated GET");
        return reply;
    }
};
int main(int argc,char** argv) {
    try {
        if (argc==3 && std::string(argv[1])=="--public") {
            auto net=yt::https(); yt::Request q; q.url=yt::sponsorblock::requestUrl(argv[2]); q.maxBytes=512*1024;
            auto r=net->send(q); auto segments=yt::sponsorblock::parse(r.body,argv[2]);
            std::printf("mbedTLS SponsorBlock: HTTP %ld error=%s bytes=%zu segments=%zu elapsed=%.3f s\n",r.status,r.error.c_str(),r.body.size(),segments.size(),r.seconds);
            for (const auto& s : segments) std::printf("  %.6f -> %.6f, duration %.3f\n",s.start,s.end,s.duration);
            return r.status!=200 || !r.error.empty() || segments.empty();
        }
        check(argc==2,"usage: sponsorblock-test public-prefix.json");
        auto publicBody=read(argv[1]);
        check(yt::sponsorblock::hashPrefix("wWC8wVI4E3E")=="5f6b","SHA-256 independent known answer");
        check(yt::sponsorblock::hashPrefix("bad&videoID=").empty(),"invalid video never hashed/requested");
        auto real=yt::sponsorblock::parse(publicBody,"wWC8wVI4E3E");
        check(real.size()==1 && std::abs(real[0].start-7.774863)<.000001 && std::abs(real[0].end-20.877296)<.000001,"recorded public prefix selects exact video among 81 collisions");
        auto wrong=yt::sponsorblock::parse(publicBody,"AAAAAAAAAAA"); check(wrong.empty(),"no marks from another video sharing a bucket");
        auto body=R"([{"videoID":"wWC8wVI4E3E","segments":[{"category":"sponsor","segment":[4,8],"videoDuration":40},{"category":"selfpromo","segment":[7,10]},{"category":"interaction","segment":[10,12]},{"category":"intro","segment":[0,3]},{"category":"outro","segment":[35,40]},{"category":"sponsor","actionType":"mute","segment":[20,21]},{"category":"sponsor","actionType":"full","segment":[0,0]},{"category":"sponsor","segment":[-1,5]},{"category":"sponsor","segment":[20,19]},{"category":"sponsor","segment":["1",2]},{"category":"sponsor","segment":[1,2,3]}]}])";
        auto parsed=yt::sponsorblock::parse(body,"wWC8wVI4E3E");
        check(parsed.size()==3,"only default skip categories with finite ordered times");
        for (const auto& bad : {std::string("{"),std::string("{}"),std::string(513*1024,' '),std::string(100,'[')+std::string(100,']'),std::string(body)+" garbage"})
            check(yt::sponsorblock::parse(bad,"wWC8wVI4E3E").empty(),"malformed/oversized/deep/trailing JSON ignored");
        Net net; net.reply={200,publicBody}; yt::sponsorblock::Client client(net);
        check(client.fetch("wWC8wVI4E3E",0).size()==1 && net.calls==1,"public response via transport");
        client.fetch("wWC8wVI4E3E",599999); check(net.calls==1,"positive cache survives same-video seeks");
        net.reply={503,"","offline"}; check(client.fetch("wWC8wVI4E3E",600000).empty() && net.calls==2,"API failure becomes empty result");
        client.fetch("wWC8wVI4E3E",659999); check(net.calls==2,"negative cache avoids error request loop");
        net.reply={404,""}; check(client.fetch("wWC8wVI4E3E",660000).empty() && net.calls==3,"404 silently empty");
        client.fetch("bad&videoID=",700000); check(net.calls==3,"invalid ID sends no HTTP");
        Playback playback; playback.segments=parsed;
        check(!playback.active(3.999,40) && playback.active(4,40).end==12 && !playback.active(12,40),"start inclusive/end exclusive and union of touching overlaps");
        check(playback.active(9,40).start==4,"undo returns to start of entire overlap group");
        check(!playback.update(5,40,100,OFF,true) && !playback.press(5,40,100,OFF),"off does nothing");
        check(!playback.update(5,40,100,BUTTON,true),"button mode never skips by itself");
        auto action=playback.press(5,40,100,BUTTON);
        check(action.kind==Action::SKIP && action.target==12 && !playback.canUndo(100),"one button seeks to end");
        check(bool(playback.active(5,40)),"manual rewind offers button again");
        for (bool ready : {false,true}) {
            playback.reset(); playback.segments=parsed;
            action=playback.update(5,40,100,AUTO,ready);
            check(bool(action)==ready,"automatic skip waits for active VOD playback");
        }
        check(action.kind==Action::SKIP && action.target==12 && playback.canUndo(100),"automatic skip and undo prompt");
        check(!playback.update(5,40,200,AUTO,true),"stale frame after seek cannot skip twice");
        action=playback.press(12,40,300,AUTO);
        check(action.kind==Action::UNDO && action.target==4,"Y returns to segment start");
        check(!playback.update(4,40,400,AUTO,true) && !playback.update(9,40,1000,AUTO,true),"undo suppresses whole overlap group for this opening");
        playback.reset(); playback.segments=real;
        check(!playback.active(8,30) && playback.active(8,667).end==20.877296,"stale video duration ignored, sub-second rounding accepted");
        action=playback.update(8,667,0,AUTO,true);
        playback.update(8,667,10000,AUTO,false);
        check(playback.canUndo(10000),"long seek does not consume undo window");
        playback.update(21,667,11000,AUTO,true);
        check(playback.canUndo(16999) && !playback.canUndo(17000),"six-second undo window after seek lands");
        playback.reset(); playback.segments={{0,5,40},{20,25,40}};
        check(playback.update(0,40,100,AUTO,true).target==5,"opening inside segment at zero");
        check(playback.update(21,40,200,AUTO,true).target==25,"second separate segment still skips");
        auto dir=(std::filesystem::temp_directory_path()/"sponsorblock-settings-XXXXXX").string();
        std::vector<char> tmp(dir.begin(),dir.end()); tmp.push_back(0); check(mkdtemp(tmp.data()),"isolated settings path"); dir=std::string(tmp.data())+"/";
        Settings settings; settings.load(dir); check(settings.sponsorBlock==BUTTON,"new settings default to button");
        for (int mode : {OFF,BUTTON,AUTO}) { settings.sponsorBlock=mode; check(settings.save(dir),"settings save"); Settings loaded; loaded.load(dir); check(loaded.sponsorBlock==mode,"all modes survive restart"); }
        { std::ofstream f(dir+"settings.txt"); f<<"sponsorblock=invalid\n"; }
        settings.load(dir); check(settings.sponsorBlock==BUTTON,"invalid settings retain default"); std::filesystem::remove_all(dir);
        for (auto lang : {i18n::Lang::PT,i18n::Lang::EN,i18n::Lang::ES}) for (auto key : {K::SponsorOff,K::SponsorButton,K::SponsorAuto,K::SponsorWarning,K::SponsorCredit,K::SponsorSkip,K::SponsorSkipped,K::SponsorUndo})
            check(i18n::get(lang,key)[0],"translated SponsorBlock string");
        std::printf("PASS %d checks: public API contract, privacy, bounds, categories, cache, skip/undo, settings, pt/en/es\n",checks);
    } catch (const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; }
}
