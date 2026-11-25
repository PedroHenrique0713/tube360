#include <stdint.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavcodec/packet.h>
#include <libswresample/swresample.h>
volatile int64_t results[10];
static void ck(int i,int64_t got,int64_t expected){results[0]++;if(got!=expected){results[1]|=INT64_C(1)<<i;results[2]=got;results[3]=expected;}}
int main(void){
 int64_t sizes[]={INT64_C(0x7fffffff),INT64_C(0x80000000),INT64_C(0x8c89588f),INT64_C(0xdddddddd),INT64_C(0xffffffff),INT64_C(0x100000000),INT64_C(0x180000001)};
 for(int i=0;i<7;i++){
  ck(0,av_rescale_rnd(sizes[i],1,1,AV_ROUND_NEAR_INF),sizes[i]);
  ck(1,av_rescale_rnd(-sizes[i],1,1,AV_ROUND_NEAR_INF),-sizes[i]);
  ck(2,av_rescale_rnd(sizes[i],1000,1000,AV_ROUND_NEAR_INF),sizes[i]);
  ck(7,av_compare_ts(sizes[i],(AVRational){1,1000},sizes[i]+1,(AVRational){1,1000}),-1);
  ck(8,av_compare_ts(-sizes[i],(AVRational){1,1000},-sizes[i]-1,(AVRational){1,1000}),1);
  ck(9,av_add_stable((AVRational){1,48000},sizes[i],(AVRational){1,48000},1),sizes[i]+1);
  int64_t last=INT64_MIN;
  ck(10,av_rescale_delta((AVRational){1,48000},sizes[i],(AVRational){1,48000},1024,&last,(AVRational){1,48000}),sizes[i]);
  ck(11,last,sizes[i]+1024);
  AVPacket p={0};p.pts=sizes[i];p.dts=-sizes[i];p.duration=sizes[i];p.pos=sizes[i];
  av_packet_rescale_ts(&p,(AVRational){1,1000},(AVRational){1,1000});
  ck(3,p.pts,sizes[i]);ck(4,p.dts,-sizes[i]);ck(5,p.duration,sizes[i]);ck(6,p.pos,sizes[i]);
 }
 SwrContext* swr=0;AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
 ck(12,swr_alloc_set_opts2(&swr,&stereo,AV_SAMPLE_FMT_S16,48000,&stereo,AV_SAMPLE_FMT_S16,48000,0,0),0);
 ck(13,swr_init(swr),0);
 for(int i=0;i<7;i++){ck(14,swr_next_pts(swr,sizes[i]),sizes[i]);ck(15,swr_get_delay(swr,sizes[i]),0);}
 swr_free(&swr);
 results[9]=0x463436;for(;;)__asm__ volatile("nop");
}
