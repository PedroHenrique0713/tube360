#include "h264.hpp"
#include "quality_recovery.hpp"
#include "settings.hpp"
#include "yt/thread.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void check(bool value,const char* reason) { if(!value){std::fprintf(stderr,"FAIL %s\n",reason);std::exit(1);} }
static int readFile(void* p,uint8_t* out,int size) {int n=std::fread(out,1,size,static_cast<FILE*>(p));return n?n:AVERROR_EOF;}
static int64_t seekFile(void* p,int64_t offset,int whence) {
  auto* f=static_cast<FILE*>(p);
  if(whence==AVSEEK_SIZE){auto pos=ftello(f);fseeko(f,0,SEEK_END);auto size=ftello(f);fseeko(f,pos,SEEK_SET);return size;}
  return fseeko(f,offset,whence & ~AVSEEK_FORCE) ? AVERROR(EIO) : ftello(f);
}
static void decode(const char* file,int height,bool full,int workers,int loop) {
  auto* f=std::fopen(file,"rb");check(f,"open fixture");
  h264::Stream stream;stream.source(height,30000.0/1001,full);
  check(stream.open(f,readFile,4,false,true,seekFile)>=0,"open real FFmpeg with profile");
  check(stream.video->thread_count==workers && stream.video->active_thread_type==FF_THREAD_FRAME,"actual FRAME worker count");
  check(int(stream.video->skip_loop_filter)==loop,"filter before first packet");
  int frames=0;
  auto packet=[&](AVPacket* p) {
    check(avcodec_send_packet(stream.video,p)>=0,"send packet");
    int r=0;
    while((r=avcodec_receive_frame(stream.video,stream.frame))>=0) {
      check(!stream.frame->decode_error_flags,"frame decoded without error");
      check(int(stream.video->skip_loop_filter)==loop,"filter stable through GOPs");++frames;
    }
    check(r==AVERROR(EAGAIN) || r==AVERROR_EOF,"receive frame");
  };
  int r;
  while((r=av_read_frame(stream.fmt,stream.pkt))>=0) {
    if(stream.pkt->stream_index==stream.vi) packet(stream.pkt);
    av_packet_unref(stream.pkt);
  }
  check(r==AVERROR_EOF,"fixture ends cleanly");packet(nullptr);check(frames>=60,"multiple GOPs decoded");
  stream.close();std::fclose(f);
  std::printf("PASS actual decode height=%d full=%d workers=%d loop=%d frames=%d\n",height,int(full),workers,loop,frames);
}
int main(int argc,char** argv) {
  check(argc==4,"usage: policy-test clip1080 clip720 disposable-settings-dir/");
  for(double fps:{0.,-1.,60.,double(NAN),double(INFINITY)})check(!playback::h264Policy(1080,fps,false).skipLoop,"unknown/invalid/high cadence keeps full filter");
  for(int height:{144,240,360,480,720,1088})for(bool full:{false,true}) {
    auto p=playback::h264Policy(height,30,full);check(p.threads==4 && !p.spread && !p.skipLoop,"only 1080p30 profile");
  }
  auto profile=playback::h264Policy(1080,29.97,false);
  check(profile.threads==6 && profile.spread && profile.skipLoop,"1080p30 policy");
  check(!playback::h264Policy(1080,30,true).skipLoop,"full filter opt-out");
  double current=.019*30,scale=2.25;
  double projected=playback::projectedDecodeFraction(current,scale,true,30);
  check(projected<.7 && projected>.67,"F39 measured policy allows 1080 despite old pixel projection >100%");
  check(playback::projectedDecodeFraction(current,scale,true,30,.1)==projected,"paced asynchronous calls cannot erase the measured Xenon cost");
  check(playback::projectedDecodeFraction(current,scale,false,30)>1,"full-filter projection keeps old conservative cost");
  check(playback::projectedDecodeFraction(current,scale,true,30,44)>1.3,"session CPU failure overrides F39 prediction");
  playback::QualityRecovery recovery;
  recovery.rejected(40000,137,30,true);recovery.changed(40000,false);
  for(uint64_t now=41000;now<160000;now+=1000)
    check(!recovery.sample(now,1000,true,18,false,0,current,projected/current,100,5,true,recovery.retryAfter(137,30)),"prediction never bypasses rejected rung");
  check(!recovery.sample(170000,1000,true,18,false,0,current,projected/current,2,5,true),"network gate remains in effect");
  for(auto name:{"range","hls","playlists"}){auto role=yt::threadRole(name);check(role.hw==3 && role.priority==1,"video TLS separated from AAC/HW1");}
  check(yt::threadRole("youtube").hw==1 && yt::threadRole("cast-v2").hw==5,"feeder/Nest map");
  check(yt::threadRole("atualizações").priority<0 && yt::threadRole("miniaturas").hw==5,"background work avoids presentation");
  auto sponsor=yt::threadRole("SponsorBlock");
  check(sponsor.hw==5 && sponsor.priority==-1,"SponsorBlock restores background role after TLS boost");
  Settings settings;settings.sponsorBlock=sponsorblock::AUTO;settings.lang=i18n::Lang::AR;
  settings.h264FullFilter=true;check(settings.save(argv[3]),"save full filter option");
  Settings restored;restored.load(argv[3]);check(restored.h264FullFilter,"reload full filter option");
  check(restored.sponsorBlock==sponsorblock::AUTO && restored.lang==i18n::Lang::AR,"filter saving preserves SponsorBlock and Arabic");
  restored.h264FullFilter=false;check(restored.save(argv[3]),"save auto filter");settings.load(argv[3]);check(!settings.h264FullFilter,"reload auto filter");
  check(settings.sponsorBlock==sponsorblock::AUTO && settings.lang==i18n::Lang::AR,"other settings survive filter toggle");
  decode(argv[1],1080,false,6,48);decode(argv[1],1080,true,6,0);decode(argv[2],720,false,4,0);
  std::puts("PASS policy, recovery/backoff/bandwidth, thread roles and persistent settings");
}
