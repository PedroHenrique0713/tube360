#include "cast/lounge.hpp"
#include <cstdlib>
#include <iostream>

using std::string;
namespace {
void check(bool yes, const char* label) { if (!yes) { std::cerr << label << '\n'; std::exit(1); } }
struct Mock final : yt::Transport {
    std::vector<string> calls;
    yt::Response request(const string& url, const string& body, bool) override {
        calls.push_back(url + " " + body);
        if (url.find("generate_screen_id") != string::npos) return {200,"FAKE-SCREEN"};
        if (url.find("get_lounge_token_batch") != string::npos) return {200,R"({"screens":[{"loungeToken":"FAKE-TOKEN","expiration":61}]})"};
        if (url.find("get_pairing_code") != string::npos) return {200,"123456789012"};
        if (url.find("/youtubei/v1/next") != string::npos)
            return {200,R"({"contents":{"singleColumnWatchNextResults":{"autoplay":{"autoplay":{"sets":[{"nextVideoRenderer":{"autoplayEndpointRenderer":{"endpoint":{"watchEndpoint":{"videoId":"F14test0003"}}}}}]}}}}})"};
        if (body == "count=0") {
            string payload = R"([[0,["c","FAKE-SID","",8]],[1,["S","FAKE-GSESSION"]]])";
            return {200,std::to_string(payload.size())+"\n"+payload};
        }
        return {200,""};
    }
};
void frames() {
    cast::Frames frames;
    string payload = R"([[9,["loungeStatus",{"devices":"[{\"user\":\"Família\"}]"}]]])";
    string wire = std::to_string(payload.size()) + "\n" + payload;
    int count = 0;
    for (char c : wire) check(frames.feed(&c,1,[&](const string& f) { ++count; return f == payload; }),"fragmented frame");
    check(count == 1,"deliver exactly once");
    check(frames.feed(wire.data(),wire.size(),[&](const string&) { ++count; return true; }),"second frame");
    check(count == 2,"second delivery");
    cast::Frames bad;
    check(!bad.feed("999999999\n",10,[](const string&) { return true; }),"oversized frame");
    cast::Frames invalid;
    check(!invalid.feed("hello\n",6,[](const string&) { return true; }),"invalid prefix");
}
void protocol() {
    Mock mock; cast::Lounge lounge(mock,"FAKE-DEVICE");
    check(lounge.begin(1000),"session handshake");
    check(!lounge.expired(30999) && lounge.expired(31000),"refresh ahead of token expiry");
    check(lounge.pairingCode() && lounge.code == "123 456 789 012","TV code");
    check(lounge.pair("FAKE-PHONE"),"DIAL pairing registration");
    check(lounge.receive(R"([[3,["setPlaylist",{"videoIds":"F14test0001,F14test0002","videoId":"F14test0001","listId":"FAKE-QUEUE","currentTime":"4"}]]])"),"set queue");
    cast::Command command;
    check(lounge.pop(command) && command[0] == "play" && command[1] == "F14test0001" && std::stod(command[2]) == 4,"play position");
    lounge.observe({"F14test0001",0,12,1});
    check(lounge.receive(R"([[4,["pause",{}]]])") && lounge.pop(command) && command[0] == "pause","pause");
    check(lounge.receive(R"([[4,["pause",{}]]])") && !lounge.pop(command),"deduplicate replayed AID");
    check(lounge.receive(R"([[5,["setPlaylist",{"videoIds":"../invalid","videoId":"../invalid"}]]])") && !lounge.pop(command),"reject invalid IDs without changing queue");
    lounge.ended("wrong-video",1); check(!lounge.pop(command),"ignore stale ended");
    lounge.ended("F14test0001",1);
    check(lounge.pop(command) && command[1] == "F14test0002","advance once on ended");
    lounge.ended("F14test0001",1); check(!lounge.pop(command),"duplicate ended");
    check(lounge.receive(R"([[6,["seekTo",{"newTime":"7"}]]])") && lounge.pop(command),"seek restarts same video");
    lounge.ended("F14test0002",2); check(!lounge.pop(command),"reject ended from before same-video seek");
    check(lounge.nextInPlaylist() == "F14test0003","contextual playlist next");
    check(mock.calls.back().find("\"playlistId\":\"FAKE-QUEUE\"") != string::npos && mock.calls.back().find("\"playlistIndex\":1") != string::npos,"preserve playlist context");
    check(lounge.flush(),"publish state");
    auto post = mock.calls.back();
    check(post.find("SID=FAKE-SID") != string::npos && post.find("gsessionid=FAKE-GSESSION") != string::npos,"session on state update");
    check(post.find("req0__sc=") != string::npos && post.find("ofs=0") != string::npos,"BrowserChannel payload");
    lounge.observe({"F14test0002",7,12,1,3});
    lounge.observe({"F14test0001",0,12,1,0});
    check(lounge.currentId().empty() && !lounge.needsNext() && !lounge.pop(command),"local navigation releases phone queue");
    lounge.observe({"",0,0,-1,0,42,false});
    check(lounge.receive(R"([[7,["getVolume",{}]]])") && lounge.flush(),"report existing Nest volume");
    check(mock.calls.back().find("_volume=42") != string::npos,"phone slider uses receiver volume");
    check(lounge.receive(R"([[8,["setVolume",{"volume":"35","muted":"false"}]]])") && lounge.pop(command),"phone volume command");
    lounge.observe({"",0,0,-1,0,42,false});
    check(lounge.receive(R"([[9,["getVolume",{}]]])") && lounge.flush(),"volume after old sample");
    check(mock.calls.back().find("_volume=35") != string::npos,"old player sample cannot regress phone volume");
    check(!lounge.receive(string(100,'[')+string(100,']')),"bound JSON depth");
    check(!lounge.receive("{broken"),"reject malformed JSON");
}
}
int main() { frames(); protocol(); std::cout << "PASS framing, JSON limits, handshake, TTL, code, queue, AID replay, stale ended, outgoing bind\n"; }
