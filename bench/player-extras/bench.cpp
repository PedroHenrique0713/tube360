// CPU de decodificação AAC/H.264 + WSOLA, sem rede/render/pacing escondendo custo.
#include "h264.hpp"
#include "audio_tempo.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sys/resource.h>

static int readFile(void* p,uint8_t* dst,int size) { size_t n=std::fread(dst,1,size,(FILE*)p); return n ? int(n) : AVERROR_EOF; }
static int64_t seekFile(void* p,int64_t n,int whence) {
  auto* f=(FILE*)p;
  if (whence==AVSEEK_SIZE) { auto old=ftello(f); fseeko(f,0,SEEK_END); auto size=ftello(f); fseeko(f,old,SEEK_SET); return size; }
  return fseeko(f,n,whence & ~AVSEEK_FORCE) ? AVERROR(EIO) : ftello(f);
}
double cpu() { rusage r{}; getrusage(RUSAGE_SELF,&r); return r.ru_utime.tv_sec+r.ru_stime.tv_sec+(r.ru_utime.tv_usec+r.ru_stime.tv_usec)/1e6; }
int main(int argc,char** argv) {
  if (argc!=4) return 2;
  double speed=std::atof(argv[3]); if (speed<.25 || speed>2) return 2;
  double began=cpu(); auto wall=std::chrono::steady_clock::now();
  int pictures=0,decoded=0; double last=0; uint64_t output=0; volatile float checksum=0;
  FILE* file=std::fopen(argv[1],"rb"); if (!file) return 2;
  h264::Stream video; int r=video.open(file,readFile,4,false,true,seekFile); if (r<0) return 3;
  auto receive=[&](AVCodecContext* codec,AVPacket* packet) {
    int r=avcodec_send_packet(codec,packet); if (r<0) return r;
    while ((r=avcodec_receive_frame(codec,video.frame))>=0) {
      ++decoded; last=video.pts(video.vi);
      if (video.frame->width!=1280 || video.frame->height!=720 || video.frame->decode_error_flags ||
          video.frame->format!=AV_PIX_FMT_YUV420P) return AVERROR_INVALIDDATA;
      if (!h264::dropAtRate(std::llround(last*60),60,speed==1 ? 60 : 30/speed)) ++pictures;
    }
    return r==AVERROR(EAGAIN) || r==AVERROR_EOF ? 0 : r;
  };
  while ((r=av_read_frame(video.fmt,video.pkt))>=0) {
    if (video.pkt->stream_index==video.vi) {
      double pts=video.pkt->pts*av_q2d(video.fmt->streams[video.vi]->time_base);
      video.video->skip_frame=h264::dropAtRate(std::llround(pts*60),60,speed==1 ? 60 : 30/speed) ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
      r=receive(video.video,video.pkt);
    }
    av_packet_unref(video.pkt); if (r<0) break;
  }
  if (r!=AVERROR_EOF || receive(video.video,nullptr)<0 || !pictures) return 4;
  video.close(); std::fclose(file);
  double videoCpu=cpu()-began, audioBegin=cpu();
  file=std::fopen(argv[2],"rb"); if (!file) return 2;
  h264::Stream audio; r=audio.open(file,readFile,1,true,false,seekFile); if (r<0) return 3;
  audio::Tempo tempo; tempo.reset(speed);
  auto sink=[&](const float* pcm,int count) { output+=count; if (count) checksum=checksum+pcm[0]; return true; };
  auto decode=[&](AVPacket* packet) {
    int r=avcodec_send_packet(audio.audio,packet); if (r<0) return r;
    while ((r=avcodec_receive_frame(audio.audio,audio.frame))>=0) {
      int n=audio.convert(audio.frame); if (n<0) return n;
      if (!(speed==1 ? sink(audio.pcm.data(),n) : tempo.push(audio.pcm.data(),n,sink))) return AVERROR_INVALIDDATA;
    }
    return r==AVERROR(EAGAIN) || r==AVERROR_EOF ? 0 : r;
  };
  while ((r=av_read_frame(audio.fmt,audio.pkt))>=0) {
    if (audio.pkt->stream_index==audio.ai) r=decode(audio.pkt);
    av_packet_unref(audio.pkt); if (r<0) break;
  }
  if (r!=AVERROR_EOF || decode(nullptr)<0) return 4;
  if (speed!=1 && !tempo.push(nullptr,0,sink,true)) return 5;
  audio.close(); std::fclose(file);
  double audioCpu=cpu()-audioBegin, duration=last+1./60;
  double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count();
  std::printf("speed,source_s,decoded,presented,wall_s,video_cpu_s,audio_cpu_s,cpu_percent_one_core,pcm_s,checksum\n%.2f,%.3f,%d,%d,%.4f,%.4f,%.4f,%.2f,%.4f,%.6f\n",
    speed,duration,decoded,pictures,elapsed,videoCpu,audioCpu,(videoCpu+audioCpu)/(duration/speed)*100,output/44100.,double(checksum));
}
