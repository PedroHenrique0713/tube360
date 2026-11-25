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
        check(r.connectTimeoutMs==2500 && r.timeoutMs==3500,"SponsorBlock-specific request deadlines");
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
        yt::sponsorblock::Diagnostic diagnostic;
        yt::sponsorblock::parse(publicBody,"wWC8wVI4E3E",&diagnostic);
        check(diagnostic.state==yt::sponsorblock::State::SEGMENTS && diagnostic.bucket==81 && diagnostic.matched,"diagnostic proves exact match inside public bucket");
        yt::sponsorblock::parse(publicBody,"AAAAAAAAAAA",&diagnostic);
        check(diagnostic.state==yt::sponsorblock::State::EMPTY && diagnostic.reason=="video_not_in_bucket","missing video is an empty response, not a TLS or parser failure");
        yt::sponsorblock::parse("[{]}","wWC8wVI4E3E",&diagnostic);
        check(diagnostic.state==yt::sponsorblock::State::PARSE,"malformed response is a parser failure");
        yt::sponsorblock::parse(R"([{"videoID":"wWC8wVI4E3E","segments":{}}])","wWC8wVI4E3E",&diagnostic);
        check(diagnostic.state==yt::sponsorblock::State::PARSE && diagnostic.reason=="segments_not_array","wrong response schema is a parser failure");
        auto outcome=[&](yt::Response response) {
            Net mock; mock.reply=std::move(response); yt::sponsorblock::Client subject(mock);
            subject.fetch("wWC8wVI4E3E",0); auto first=subject.diagnostic();
            subject.fetch("wWC8wVI4E3E",1);
            check(mock.calls==1 && subject.diagnostic().cached && first.state==subject.diagnostic().state,"cache preserves diagnostic instead of turning failure into empty");
            return first;
        };
        yt::Response tlsFailure; tlsFailure.failure=yt::Failure::TLS; tlsFailure.error="tls_peer_verification";
        tlsFailure.transportCode=60; tlsFailure.tlsCode=-0x2700; tlsFailure.tlsVerify=8; tlsFailure.transportDetail="untrusted\nissuer";
        diagnostic=outcome(tlsFailure);
        check(diagnostic.state==yt::sponsorblock::State::TLS && diagnostic.transport==60 && diagnostic.tls==-0x2700 && diagnostic.verify==8,"TLS retains curl/mbedTLS/verify codes");
        check(diagnostic.line().find("untrusted issuer")!=std::string::npos && diagnostic.line().find('\n')==std::string::npos,"diagnostic is bounded and single-line");
        check(outcome({403,"blocked"}).state==yt::sponsorblock::State::HTTP,"HTTP refusal differs from no segments");
        check(outcome({503,"unavailable"}).state==yt::sponsorblock::State::HTTP,"HTTP server failure differs from no segments");
        check(outcome({404,{}}).state==yt::sponsorblock::State::EMPTY,"404 is documented empty");
        check(outcome({200,"[{]}"}).state==yt::sponsorblock::State::PARSE,"HTTP 200 malformed JSON differs from empty");
        check(outcome({0,{},"Could not resolve host"}).state==yt::sponsorblock::State::NETWORK,"DNS failure differs from empty");
        check(outcome({0,{},"cancelled"}).state==yt::sponsorblock::State::CANCELLED,"cancelled request differs from empty");
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
