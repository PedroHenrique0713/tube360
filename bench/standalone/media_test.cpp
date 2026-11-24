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
    bool backoff(unsigned) override { return true; }
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
    m.steps.push_back({url,"Range: bytes=0-524287",{302,"","","",relocated}});
    m.steps.push_back({relocated,"Range: bytes=0-524287",{206,"abcdef","","bytes 0-5/6"}});
    SDL_AtomicInt received{}; yt::RangeFile f(std::move(net),url,&received);
    uint8_t out[8]{};
    check(f.read(out,3)==3 && !std::memcmp(out,"abc",3),"first range");
    check(yt::RangeFile::seek(&f,0,AVSEEK_SIZE)==6,"content length");
    check(yt::RangeFile::seek(&f,-2,SEEK_END)==4 && f.read(out,4)==2 && !std::memcmp(out,"ef",2),"seek within window");
    check(f.read(out,1)==AVERROR_EOF && m.steps.empty(),"EOF without transfer");

    auto bad=std::make_unique<Mock>();
    bad->steps.push_back({url,"Range: bytes=0-524287",{206,"ab","","bytes 2-3/6"}});
    yt::RangeFile corrupt(std::move(bad),url,&received);
    check(corrupt.read(out,1)<0 && corrupt.read(out,1)<0,"reject shifted 206 without looping");
}
void resumeRange() {
    auto net=std::make_unique<Mock>(); auto& m=*net;
    string url="https://a.googlevideo.com/v";
    m.steps.push_back({url,"Range: bytes=0-524287",{206,"abc","Timeout was reached","bytes 0-5/6"}});
    m.steps.push_back({url,"Range: bytes=3-5",{206,"def","","bytes 3-5/6"}});
    SDL_AtomicInt received{}; yt::RangeFile f(std::move(net),url,&received);
    uint8_t out[8]{};
    check(f.read(out,6)==6 && !std::memcmp(out,"abcdef",6),"resume timeout from first missing byte without corrupting MP4");
    check(SDL_GetAtomicInt(&received)==6 && m.steps.empty(),"count the retained prefix exactly once");
}
void rangeFailures() {
    const string url="https://a.googlevideo.com/v", full="Range: bytes=0-524287";
    SDL_AtomicInt received{}; uint8_t out[8]{};
    auto net=std::make_unique<Mock>(); auto& m=*net;
    m.steps.push_back({url,full,{503}});
    m.steps.push_back({url,full,{206,"abcdef","","bytes 0-5/6"}});
    yt::RangeFile transient(std::move(net),url,&received);
    check(transient.read(out,6)==6 && transient.resumes==1 && m.steps.empty(),"HTTP 503 retries without decoding an empty window");
    net=std::make_unique<Mock>(); auto* broken=net.get();
    for (int i=0;i<4;++i) broken->steps.push_back({url,full,{0,"","Timeout was reached"}});
    yt::RangeFile exhausted(std::move(net),url,&received);
    check(exhausted.read(out,1)<0 && exhausted.read(out,1)<0 && broken->steps.empty() && exhausted.requests==4,"bounded retries, never loop on a permanently dead socket");
    net=std::make_unique<Mock>(); auto* forbidden=net.get();
    forbidden->steps.push_back({url,full,{403}});
    yt::RangeFile denied(std::move(net),url,&received);
    check(denied.read(out,1)<0 && denied.status==403 && denied.requests==1,"403 stays visible for URL renewal, no range retry storm");
    net=std::make_unique<Mock>();
    net->steps.push_back({url,full,{206,"abc","Timeout was reached","bytes 0-5/6"}});
    net->steps.push_back({url,"Range: bytes=3-5",{206,"def","","bytes 3-5/7"}});
    yt::RangeFile changed(std::move(net),url,&received);
    check(changed.read(out,6)<0,"reject content length change across resumed requests");
}
void parallelRanges() {
    struct Shared { std::atomic<int> active{0},peak{0}; std::atomic_bool stop{false}; } shared;
    struct Slow : yt::Transport {
        Shared& shared;
        explicit Slow(Shared& s) : shared(s) {}
        bool cancelled() const override { return shared.stop; }
        yt::Response request(const string&,const string&,bool) override { return {0,"","unexpected"}; }
        yt::Response send(const yt::Request& r) override {
            long long first=0,last=0,total=4LL*yt::RangeFile::chunk+19;
            check(r.media && std::sscanf(r.headers[0].c_str(),"Range: bytes=%lld-%lld",&first,&last)==2,"parallel range request");
            int active=++shared.active, peak=shared.peak;
            while (peak<active && !shared.peak.compare_exchange_weak(peak,active)) {}
            SDL_Delay(30); --shared.active; last=std::min(last,total-1);
            string bytes(size_t(last-first+1),'\0');
            for (long long i=first;i<=last;++i) bytes[size_t(i-first)]=char(i%251);
            return {206,std::move(bytes),"","bytes "+std::to_string(first)+"-"+std::to_string(last)+"/"+std::to_string(total)};
        }
    };
    SDL_AtomicInt received{};
    yt::RangeFile f(std::make_unique<Slow>(shared),"https://a.googlevideo.com/v",&received,std::make_unique<Slow>(shared));
    uint8_t out[16384]; int n; int64_t offset=0;
    while ((n=f.read(out,sizeof(out)))>0) {
        for (int i=0;i<n;++i) check(out[i]==uint8_t((offset+i)%251),"parallel windows preserve byte order");
        offset+=n;
    }
    check(n==AVERROR_EOF && offset==4LL*yt::RangeFile::chunk+19 && shared.peak==2,"two actual simultaneous video transfers through EOF");
    check(SDL_GetAtomicInt(&received)==offset,"read-ahead never downloads a window twice");
    shared.stop=true;
    check(f.read(out,1)==AVERROR_EXIT,"cancel beats EOF and never becomes a decode error");
}
void closeReadAhead() {
    struct Waiting : yt::Transport {
        std::atomic<int>& cancellations;
        std::atomic_bool aborted{false};
        explicit Waiting(std::atomic<int>& n) : cancellations(n) {}
        bool cancelled() const override { return aborted; }
        void cancel() override { aborted=true; cancellations++; }
        yt::Response request(const string&,const string&,bool) override { return {0,"","unexpected"}; }
        yt::Response send(const yt::Request& r) override {
            if (r.headers[0]=="Range: bytes=0-524287") return {206,string(524288,'a'),"","bytes 0-524287/2097152"};
            while (!aborted) SDL_Delay(5);
            return {0,"","cancelled"};
        }
    };
    SDL_AtomicInt received{}; std::atomic<int> cancellations{0};
    uint64_t began=SDL_GetTicks();
    {
        yt::RangeFile f(std::make_unique<Waiting>(cancellations),"https://a.googlevideo.com/v",&received,std::make_unique<Waiting>(cancellations));
        uint8_t out[8]; check(f.read(out,8)==8,"prime independently cancellable read-ahead");
        SDL_Delay(20);
    }
    check(cancellations==2 && SDL_GetTicks()-began<500,"source handoff cancels old ranges without stopping the shared AAC");
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
void profilesAndAudio() {
    for (bool fast : {false,true}) for (bool dub : {false,true}) {
        Mock lists,media; std::atomic_bool stop{false}; string base="https://example.com/";
        string master="#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",LANGUAGE=\"pt\",NAME=\"Portuguese (auto-dubbed)\",DEFAULT=YES,URI=\"pt.m3u8\"\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",LANGUAGE=\"en\",NAME=\"English (original)\",URI=\"en.m3u8\"\n";
        string attrs="#EXT-X-STREAM-INF:CODECS=\"avc1,mp4a\",RESOLUTION=1280x720,AUDIO=\"a\",FRAME-RATE=";
        master+=attrs+"30\nnormal.m3u8\n"+attrs+"60\nfast.m3u8\n";
        string selected=dub ? "pt" : "en", video=fast ? "fast.m3u8" : "normal.m3u8";
        lists.steps.push_back({base+"master","",{200,master}});
        lists.steps.push_back({base+video,"",{200,"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:1\nv1\n#EXT-X-ENDLIST\n"}});
        lists.steps.push_back({base+selected+".m3u8","",{200,"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:1\na1\n#EXT-X-ENDLIST\n"}});
        media.steps.push_back({base+"v1","",{200,"video"}}); media.steps.push_back({base+"a1","",{200,selected}});
        yt::LiveFile live(lists,media,stop);
        check(live.open(base+"master",720,fast,dub ? "pt-auto" : "original"),"HLS profile and language open");
        check(live.frameRate==(fast ? 60 : 30) && live.audioTracks.size()==2,"HLS keeps requested cadence and track catalog");
        check(live.refill()==0 && live.sound==selected && live.refill()==AVERROR_EOF,"HLS original overrides default dub; explicit dub selects its own playlist");
    }
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
void hlsAdaptation() {
    Mock lists,media; std::atomic_bool stop{false};
    string base="https://example.com/";
    auto playlist=[](int first,const string& prefix,const string& stamp) {
        string body="#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:"+std::to_string(first)+"\n";
        if (!stamp.empty()) body+="#EXT-X-PROGRAM-DATE-TIME:"+stamp+"\n";
        for (int i=0;i<3;++i) body+="#EXTINF:2.0,\n"+prefix+std::to_string(i)+"\n";
        return body;
    };
    lists.steps.push_back({base+"fast","",{200,playlist(10,"f","2026-11-23T12:00:00.000Z")}});
    lists.steps.push_back({base+"normal","",{200,playlist(80,"n","2026-11-23T09:00:00-03:00")}});
    lists.steps.push_back({base+"low","",{200,playlist(500,"l","2026-11-23T12:00:00+00:00")}});
    yt::LiveFile f(lists,media,stop); yt::LiveFile::List video;
    f.playlist=base+"fast"; f.audioPlaylist=base+"original";
    f.lowerVariants={{base+"normal","audio",720,30},{base+"low","audio",480,30}};
    f.lowerRemaining=2; f.pending.push_back({"already-buffered-video","already-buffered-audio"});
    check(f.update(f.playlist,video),"HLS timeline parsed");
    int64_t offset=0;
    check(f.switchPlaylist(video,11,offset) && offset==70 && video.first==10 && video.urls[1]==base+"n1","HLS switches by UTC even when media sequences differ");
    check(f.switchPlaylist(video,12,offset) && offset==490 && video.urls[2]==base+"l2" && f.selectedHeight==480 && f.switches==2,"HLS walks both local network sources at the same instant");
    check(f.audioPlaylist==base+"original" && f.pending.front().audio=="already-buffered-audio" && lists.steps.empty(),"HLS keeps original AAC playlist and buffered pairs");
    lists.steps.push_back({base+"unclocked","",{200,playlist(10,"x","")}});
    check(f.update(base+"unclocked",video),"unclocked HLS remains playable");
    f.lowerVariants={{base+"never-requested","",480,30}}; f.lowerRemaining=1;
    check(!f.switchPlaylist(video,11,offset) && f.lowerRemaining==0 && lists.steps.empty(),"no guessed sequence alignment without common UTC");
    check(yt::LiveFile::timestamp("broken")<0 && yt::LiveFile::timestamp("2026-11-23T12:00:00Z")==yt::LiveFile::timestamp("2026-11-23T09:00:00-03:00"),"UTC parser rejects malformed input and honors timezone");
}
}
int main() { resumeRange(); rangeFailures(); parallelRanges(); closeReadAhead(); ranges(); liveQuality(); profilesAndAudio(); playlists(); hlsAdaptation(); std::cout<<"Range resume, bounded retries, parallel byte order, independent cancel, seek, HLS UTC adaptation and audio checks passed\n"; }
