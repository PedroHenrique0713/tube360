// Duas aberturas do leitor HLS do produto, com janelas sintéticas do mesmo broadcast.
#include "yt/media.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

struct Fixture : yt::Transport {
    std::string directory;
    int first;
    Fixture(std::string dir,int sequence) : directory(std::move(dir)),first(sequence) {}
    yt::Response request(const std::string& url,const std::string&,bool) override {
        std::string body;
        if (url.ends_with("/master.m3u8")) body="#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=500000,RESOLUTION=320x180,FRAME-RATE=30,CODECS=\"avc1.42e01e,mp4a.40.2\"\nmedia.m3u8\n";
        else if (url.ends_with("/media.m3u8")) {
            body="#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:"+std::to_string(first)+"\n";
            for (int i=first;i<first+6;++i) {
                char file[64]; std::snprintf(file,sizeof(file),"#EXTINF:2.0,\nsegment%02d.ts\n",i); body+=file;
            }
        } else {
            std::ifstream input(directory+url.substr(url.find_last_of('/')),std::ios::binary);
            if (!input) return {404,{},"missing_fixture"};
            body.assign(std::istreambuf_iterator<char>(input),{});
        }
        return {200,std::move(body),{}};
    }
};
struct Bytes {
    const std::string& bytes;
    size_t offset=0;
    static int read(void* p,uint8_t* out,int count) {
        auto& source=*static_cast<Bytes*>(p);
        auto n=std::min(size_t(count),source.bytes.size()-source.offset);
        if (!n) return AVERROR_EOF;
        std::memcpy(out,source.bytes.data()+source.offset,n); source.offset+=n; return int(n);
    }
};
std::pair<double,double> origin(const std::string& directory,int first) {
    Fixture lists(directory,first),segments(directory,first);
    std::atomic_bool stop{false};
    yt::LiveFile live(lists,segments,stop);
    if (!live.open("https://fixture.invalid/master.m3u8",480) || live.refill()<0) throw std::runtime_error("HLS open/refill");
    Bytes bytes{live.segment};
    auto* io=avio_alloc_context(static_cast<unsigned char*>(av_malloc(32768)),32768,0,&bytes,Bytes::read,nullptr,nullptr);
    auto* format=avformat_alloc_context(); format->pb=io; format->flags|=AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&format,nullptr,nullptr,nullptr)<0 || avformat_find_stream_info(format,nullptr)<0) throw std::runtime_error("demux");
    double video=NAN,audio=NAN;
    auto* packet=av_packet_alloc();
    while (av_read_frame(format,packet)>=0) {
        auto* stream=format->streams[packet->stream_index];
        if (packet->pts!=AV_NOPTS_VALUE) {
            double pts=packet->pts*av_q2d(stream->time_base);
            if (stream->codecpar->codec_type==AVMEDIA_TYPE_VIDEO && !std::isfinite(video)) video=pts;
            if (stream->codecpar->codec_type==AVMEDIA_TYPE_AUDIO && !std::isfinite(audio)) audio=pts;
        }
        av_packet_unref(packet);
        if (std::isfinite(video) && std::isfinite(audio)) break;
    }
    av_packet_free(&packet); avformat_close_input(&format); av_freep(&io->buffer); avio_context_free(&io);
    if (!std::isfinite(video) || !std::isfinite(audio)) throw std::runtime_error("missing PTS");
    return {video,audio};
}
int main(int argc,char** argv) {
    if (argc!=2 || !SDL_Init(0)) return 2;
    try {
        auto base=origin(argv[1],0);
        for (int window : {0,1,3}) {
            auto other=origin(argv[1],window);
            double skew=other.second-base.first;
            std::cout<<"window_shift="<<window<<" video_pts="<<base.first<<" audio_pts="<<other.second<<" skew_ms="<<skew*1000<<std::endl;
            if (std::fabs((other.second-base.second)-window*2)>.03) return 3;
        }
    } catch (const std::exception& e) { std::cerr<<e.what()<<std::endl; return 1; }
    SDL_Quit();
}
