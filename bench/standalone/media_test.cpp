// Contratos de mídia exercitados sem rede, inclusive a borda de duas playlists com janelas diferentes.
#include "yt/media.hpp"
#include <deque>
#include <iostream>

namespace {
using yt::string;
void check(bool condition, const char* what) {
    if (!condition) { std::cerr << what << '\n'; std::exit(1); }
}
struct Mock : yt::Transport {
    struct Step { string url, range; yt::Response reply; };
    std::deque<Step> steps;
    yt::Response request(const string& url,const string&,bool) override {
        yt::Request req; req.url=url; return send(req);
    }
    yt::Response send(const yt::Request& req) override {
        check(!steps.empty(),"unexpected transfer");
        auto s=steps.front(); steps.pop_front();
        check(req.url==s.url,"wrong media or sequence");
        check(s.range.empty() ? req.headers.empty() : req.headers==std::vector<string>{s.range},"wrong byte range");
        return s.reply;
    }
};
void ranges() {
    auto net=std::make_unique<Mock>(); auto& m=*net;
    string url="https://a.googlevideo.com/v", relocated="https://b.googlevideo.com/v";
    m.steps.push_back({url,"Range: bytes=0-1048575",{302,"","","",relocated}});
    m.steps.push_back({relocated,"Range: bytes=0-1048575",{206,"abcdef","","bytes 0-5/6"}});
    SDL_AtomicInt received{}; yt::RangeFile f(std::move(net),url,&received);
    uint8_t out[8]{};
    check(f.read(out,3)==3 && !std::memcmp(out,"abc",3),"first range");
    check(yt::RangeFile::seek(&f,0,AVSEEK_SIZE)==6,"content length");
    check(yt::RangeFile::seek(&f,-2,SEEK_END)==4 && f.read(out,4)==2 && !std::memcmp(out,"ef",2),"seek within window");
    check(f.read(out,1)==AVERROR_EOF && m.steps.empty(),"EOF without transfer");

    auto bad=std::make_unique<Mock>();
    bad->steps.push_back({url,"Range: bytes=0-1048575",{206,"ab","","bytes 2-3/6"}});
    yt::RangeFile corrupt(std::move(bad),url,&received);
    check(corrupt.read(out,1)<0 && corrupt.read(out,1)<0,"reject shifted 206 without looping");
}
void liveQuality() {
    string base = "https://example.com/", header = "#EXTM3U\n";
    string variant = "#EXT-X-STREAM-INF:CODECS=\"avc1,mp4a\",RESOLUTION=";
    string low = variant+"854x480,FRAME-RATE=30\nlow.m3u8\n";
    string fast = variant+"1280x720,FRAME-RATE=59.94\nhigh.m3u8\n";
    for (int ceiling : {480,720}) {
        Mock lists, media;
        std::atomic_bool stop{false};
        string selected = ceiling==480 ? "low.m3u8" : "high.m3u8";
        lists.steps.push_back({base+"master","",{200,header+low+fast}});
        lists.steps.push_back({base+selected,"",{200,header+"seg.ts\n#EXT-X-ENDLIST\n"}});
        media.steps.push_back({base+"seg.ts","",{200,"ts"}});
        yt::LiveFile live(lists,media,stop);
        check(live.open(base+"master",ceiling) && live.height==ceiling,"respect selected quality");
        check(live.frameRate==(ceiling==480 ? 30 : 59.94),"fractional frame rate");
        check(live.refill()==0 && live.refill()==AVERROR_EOF,"consume selected variant");
    }
    Mock lists, media; std::atomic_bool stop{false};
    lists.steps.push_back({base+"master","",{200,header+low}});
    yt::LiveFile live(lists,media,stop);
    check(!live.open(base+"master",720),"respect 720p floor when only 480p exists");
}
void playlists() {
    Mock lists, media; std::atomic_bool stop=false;
    string base="https://example.com/", master="#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aac\",URI=\"a.m3u8\"\n";
    string attrs="#EXT-X-STREAM-INF:CODECS=\"avc1.4d401f,mp4a.40.2\",RESOLUTION=1280x720,AUDIO=\"aac\",FRAME-RATE=";
    master+=attrs+"60\nfast.m3u8\n"+attrs+"30\nv.m3u8\n";
    lists.steps.push_back({base+"master","",{200,master}});
    lists.steps.push_back({base+"v.m3u8","",{200,"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:20\nv20\nv21\nv22\nv23\nv24\n#EXT-X-ENDLIST\n"}});
    lists.steps.push_back({base+"a.m3u8","",{200,"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:19\na19\na20\na21\na22\na23\n"}});
    lists.steps.push_back({base+"a.m3u8","",{200,"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:23\na23\na24\n#EXT-X-ENDLIST\n"}});
    for (int i=21; i<=24; ++i) for (auto track : {"v","a"}) {
        string name=track+std::to_string(i);
        media.steps.push_back({base+name,"",{200,name}});
    }
    yt::LiveFile live{lists,media,stop};
    check(live.open(base+"master",720) && live.frameRate==30,"prefer 720p30");
    for (int i=21; i<=24; ++i)
        check(live.refill()==0 && live.sound=="a"+std::to_string(i) && live.segment=="v"+std::to_string(i),"paired reserve and refresh");
    check(live.refill()==AVERROR_EOF && lists.steps.empty() && media.steps.empty(),"clean end of both playlists");
    yt::PackedAudio packed;
    check(!packed.begin("ID3") && !packed.begin(string(80,0)),"invalid packed audio tag");
}
}
int main() { ranges(); liveQuality(); playlists(); std::cout<<"Range, seek, redirects, live quality floor, paired reserve and packed audio checks passed\n"; }
