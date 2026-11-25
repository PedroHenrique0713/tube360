// Sonda de rede opt-in; não lê conta, cookies ou cache persistido.
#include "yt/media.hpp"
#include "h264.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
struct Recording final : yt::Transport {
    std::unique_ptr<yt::Transport> net;
    std::string prefix;
    explicit Recording(const std::atomic_bool* stop,std::string path) : net(yt::https(stop)),prefix(std::move(path)) {}
    bool cancelled() const override { return net->cancelled(); }
    void cancel() override { net->cancel(); }
    yt::Response request(const yt::string&,const yt::string&,bool) override { return {}; }
    yt::Response send(const yt::Request& req) override {
        auto r=net->send(req);
        std::fprintf(stderr,"range %s -> %ld %s bytes=%zu error=%s\n",req.headers.empty()?"none":req.headers[0].c_str(),r.status,r.contentRange.c_str(),r.body.size(),r.error.c_str());
        if(!prefix.empty() && !req.headers.empty() && req.headers[0].starts_with("Range: bytes=0-")) {
            std::ofstream f(prefix+".bin",std::ios::binary);f.write(r.body.data(),r.body.size());
        }
        return r;
    }
};
static int64_t seek(void* p,int64_t off,int whence) {
    auto result=yt::RangeFile::seek(p,off,whence);
    std::fprintf(stderr,"seek off=%lld whence=%x -> %lld\n",(long long)off,whence,(long long)result);return result;
}
int main(int argc,char** argv) {
    if(argc<3)return 2;
    SDL_Init(0);std::atomic_bool stop{false};
    auto* timer=SDL_CreateThread([](void* p){auto& stop=*static_cast<std::atomic_bool*>(p);for(int i=0;i<1200 && !stop;++i)SDL_Delay(50);stop=true;return 0;},"deadline",&stop);
    auto net=yt::https(&stop);yt::Client client(*net);
    auto began=SDL_GetTicks();auto media=client.player(argv[1],720,nullptr,playback::DETAIL);
    std::printf("player status=%s duration=%d video=%d %dp%.3f audio=%d resolve_ms=%llu\n",media.status.c_str(),media.duration,media.video.itag,media.video.height,media.video.fps,media.audio.itag,(unsigned long long)(SDL_GetTicks()-began));std::fflush(stdout);
    int r=-1;
    if(media.status=="OK") {
        yt::RangeFile file(std::make_unique<Recording>(&stop,argc>3?argv[3]:""),media.video.url,nullptr);
        file.aheadEnabled=false;h264::Stream stream;
        r=stream.open(&file,yt::RangeFile::pull,1,false,true,seek);
        std::printf("header result=%d elapsed_ms=%llu bytes=%llu requests=%u length=%lld pos=%lld\n",r,(unsigned long long)(SDL_GetTicks()-began),(unsigned long long)file.bytes.load(),file.requests.load(),(long long)file.length,(long long)file.pos);std::fflush(stdout);
        if(r>=0)r=stream.seekTime(std::atof(argv[2]));
        if(r>=0)r=av_read_frame(stream.fmt,stream.pkt);
        std::printf("packet result=%d pts=%.3f elapsed_ms=%llu bytes=%llu requests=%u\n",r,r>=0?stream.pkt->pts*av_q2d(stream.fmt->streams[stream.pkt->stream_index]->time_base):-1,(unsigned long long)(SDL_GetTicks()-began),(unsigned long long)file.bytes.load(),file.requests.load());
    }
    if(r>=0 && !media.audio.url.empty()) {
        yt::RangeFile file(std::make_unique<Recording>(&stop,argc>3?std::string(argv[3])+"-audio":""),media.audio.url,nullptr);
        h264::Stream stream;r=stream.open(&file,yt::RangeFile::pull,1,true,false,seek);
        std::printf("audio header result=%d elapsed_ms=%llu bytes=%llu requests=%u length=%lld pos=%lld\n",r,(unsigned long long)(SDL_GetTicks()-began),(unsigned long long)file.bytes.load(),file.requests.load(),(long long)file.length,(long long)file.pos);std::fflush(stdout);
        if(r>=0)r=stream.seekTime(std::atof(argv[2]));
        if(r>=0)r=av_read_frame(stream.fmt,stream.pkt);
        std::printf("audio packet result=%d elapsed_ms=%llu bytes=%llu requests=%u\n",r,(unsigned long long)(SDL_GetTicks()-began),(unsigned long long)file.bytes.load(),file.requests.load());
    }
    stop=true;SDL_WaitThread(timer,nullptr);return r<0?1:0;
}
