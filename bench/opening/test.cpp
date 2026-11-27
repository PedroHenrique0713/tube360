#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#define LOG(...) SDL_Log(__VA_ARGS__)
#include "player.hpp"
#include "yt/media.hpp"
#include "h264.hpp"
#include <fstream>
#include <iostream>

static void check(bool value,const char* message) {
    if(!value){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}
}
struct FileTransport final : yt::Transport {
    std::string path;
    const std::atomic_bool* stop=nullptr;
    std::atomic_bool aborted{false};
    bool cancelled() const override { return aborted || (stop && *stop); }
    void cancel() override { aborted=true; }
    explicit FileTransport(std::string p) : path(std::move(p)) {}
    yt::Response request(const yt::string&,const yt::string&,bool) override { return {0,"","unexpected request"}; }
    yt::Response send(const yt::Request& request) override {
        if(cancelled())return {0,"","cancelled"};
        if(request.url.ends_with("/broken"))return {206,std::string(64,'\0'),"","bytes 0-63/64"};
        std::ifstream file(path,std::ios::binary);check(bool(file),"fixture exists");
        file.seekg(0,std::ios::end);auto length=int64_t(file.tellg());
        long long begin=0,end=0;
        check(request.headers.size()==1 && std::sscanf(request.headers[0].c_str(),"Range: bytes=%lld-%lld",&begin,&end)==2,"bounded Range request");
        if(begin>=length)return {416};
        end=std::min(end,(long long)length-1);file.seekg(begin);
        std::string bytes(size_t(end-begin+1),'\0');file.read(bytes.data(),bytes.size());
        check(size_t(file.gcount())==bytes.size(),"sparse fixture read complete");
        return {206,std::move(bytes),"","bytes "+std::to_string(begin)+"-"+std::to_string(end)+"/"+std::to_string(length)};
    }
};
static std::string playerFixture;
namespace yt {
std::unique_ptr<Transport> https(const std::atomic_bool* stop) {
    auto result=std::make_unique<FileTransport>(playerFixture);result->stop=stop;return result;
}
}
static void fallbackDoesNotPunishNextVideo() {
    player::Player p;
    p.mu=SDL_CreateMutex();p.cv=SDL_CreateCondition();p.nmu=SDL_CreateMutex();
    auto play=[&](const char* id,bool broken) {
        p.halt();
        p.media={};p.media.id=id;p.media.status="OK";p.media.duration=10800;p.media.ttl=3600;
        p.media.video={broken?"https://fixture.googlevideo.com/broken":"https://fixture.googlevideo.com/video",137,1080,25};
        if(broken)p.media.lowerVideo.push_back({"https://fixture.googlevideo.com/video",136,720,25});
        p.media.videoLadder={p.media.video};
        p.media.videoLadder.insert(p.media.videoLadder.end(),p.media.lowerVideo.begin(),p.media.lowerVideo.end());
        p.cachedQuality=720;p.cachedVideoMode=playback::DETAIL;p.cachedAudio="original";p.validUntil=SDL_GetTicks()+60000;
        p.start(id,120,"fixture",10800,720,false,false,playback::DETAIL);
        uint64_t deadline=SDL_GetTicks()+5000;
        while(SDL_GetTicks()<deadline && p.state!=player::PLAYING && p.state!=player::FAILED) {
            if(auto* frame=p.take())p.recycle(frame);
            SDL_Delay(2);
        }
        check(p.state==player::PLAYING && p.position()>=120 && p.position()<121,"actual player opens fallback at correct initial seek");
    };
    play("first000001",true);
    check(p.activeVideoItag==136 && p.adapted,"malformed header actually exercises lower source");
    check(p.videoMode==playback::DETAIL && p.quality==720 && p.qualityRecovery.failed.empty(),"opening failure does not become a measured performance penalty");
    play("second00002",false);
    check(p.activeVideoItag==137 && !p.adapted && p.qualityRecovery.failed.empty(),"next video retains original profile and first source");
    p.halt();SDL_DestroyCondition(p.cv);SDL_DestroyMutex(p.mu);SDL_DestroyMutex(p.nmu);
    std::cout<<"PASS opening failure -> lower source; next video -> original source, no performance penalty\n";
}
int main(int argc,char** argv) {
    check(argc==2,"generated fixture directory required");SDL_Init(0);
    for(const char* name : {"long25","hdr60"}) {
        std::string path=std::string(argv[1])+"/"+name+"/source.mp4";
        yt::RangeFile file(std::make_unique<FileTransport>(path),"https://fixture.googlevideo.com/video",nullptr);
        file.aheadEnabled=false;h264::Stream stream;
        int r=stream.open(&file,yt::RangeFile::pull,1,false,true,yt::RangeFile::seek);
        check(r>=0,"open three-hour MP4");
        check(file.length>INT64_C(2147483648) && avio_size(stream.io)==file.length,"64-bit AVIO size is exact above 2 GiB");
        check(file.requests<=2 && file.bytes<=2*yt::RangeFile::chunk,"header does not scan media fragments");
        auto* track=stream.fmt->streams[stream.vi];
        check(std::abs(track->duration*av_q2d(track->time_base)-10800)<.01,"full sidx duration");
        for(double seconds : {120.0,10000.0}) {
            check(stream.seekTime(seconds)>=0,"initial and >2 GiB seek");
            check(av_read_frame(stream.fmt,stream.pkt)>=0,"packet after seek");
            double pts=stream.pkt->pts*av_q2d(track->time_base);
            check(std::abs(pts-seconds)<.01,"seek selects correct fragment timestamp");
            avcodec_flush_buffers(stream.video);
            check(avcodec_send_packet(stream.video,stream.pkt)>=0,"submit real AVC packet");
            av_packet_unref(stream.pkt);
            bool decoded=false;
            for(int i=0;i<20 && !decoded;++i) {
                int result=avcodec_receive_frame(stream.video,stream.frame);
                if(result>=0){decoded=true;break;}
                check(result==AVERROR(EAGAIN),"decode after sparse seek");
                check(av_read_frame(stream.fmt,stream.pkt)>=0,"continue fragment");
                check(avcodec_send_packet(stream.video,stream.pkt)>=0,"submit next packet");av_packet_unref(stream.pkt);
            }
            check(decoded && stream.frame->width==1280 && stream.frame->height==720,"real decoded frame after seek");
            if(std::string(name)=="hdr60")check(stream.frame->color_trc==AVCOL_TRC_SMPTE2084,"PQ metadata survives 60 fps opening");
            av_frame_unref(stream.frame);
        }
        check(file.requests<=8,"bounded transfers for header and both seeks");
        std::cout<<"PASS "<<name<<" length="<<file.length<<" requests="<<file.requests<<" bytes="<<file.bytes<<" seek=120,10000 decoded=1\n";
    }
    playerFixture=std::string(argv[1])+"/long25/source.mp4";
    fallbackDoesNotPunishNextVideo();
}
