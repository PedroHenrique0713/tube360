// FFmpeg real do Xbox em CPU PowerPC, MSR.SF=0. Rede e kernel são substituídos.
#include <libavformat/avformat.h>
#include <stdint.h>
#include <string.h>
#include "fixture.h"
volatile int64_t results[10];
extern unsigned char _binary_prefix_bin_start[], _binary_fragment_bin_start[];
static int64_t cursor;
static uint8_t byte_at(int64_t position) {
    if(position<PREFIX_SIZE)return _binary_prefix_bin_start[position];
    int64_t relative=position-PREFIX_SIZE;
    uint64_t index=relative/STRIDE,offset=relative%STRIDE;
    if(offset>=TFDT_OFFSET && offset<TFDT_OFFSET+8)
        return (index*2*TIMESCALE) >> ((TFDT_OFFSET+7-offset)*8);
    if(offset>=MFHD_OFFSET && offset<MFHD_OFFSET+4)
        return (index+1) >> ((MFHD_OFFSET+3-offset)*8);
    return offset<FRAGMENT_SIZE ? _binary_fragment_bin_start[offset] : 0;
}
static int pull(void* opaque,uint8_t* out,int size) {
    if(++results[6]>128){results[8]=1;return AVERROR_EOF;}
    if(cursor>=FILE_SIZE)return AVERROR_EOF;
    if(size>FILE_SIZE-cursor)size=FILE_SIZE-cursor;
    for(int i=0;i<size;++i)out[i]=byte_at(cursor+i);
    cursor+=size;if(cursor>results[7])results[7]=cursor;
    return size;
}
static int64_t seek(void* opaque,int64_t offset,int whence) {
    if(whence==AVSEEK_SIZE)return FILE_SIZE;
    if(whence==SEEK_SET)return cursor=offset;
    if(whence==SEEK_END)return cursor=FILE_SIZE+offset;
    return AVERROR(EINVAL);
}
int main(void) {
    AVFormatContext* format=avformat_alloc_context();
    AVIOContext* io=avio_alloc_context(av_malloc(32768),32768,0,0,pull,0,seek);
    format->pb=io;format->flags|=AVFMT_FLAG_CUSTOM_IO;
    results[1]=avio_size(io);cursor=0;
    results[0]=avformat_open_input(&format,0,av_find_input_format("mov"),0);
    if(results[0]>=0) {
        AVPacket* packet=av_packet_alloc();
        for(int i=0;i<2;++i) {
            int seconds=i?10000:120;
            results[2+2*i]=av_seek_frame(format,0,(int64_t)seconds*TIMESCALE,AVSEEK_FLAG_BACKWARD);
            if(results[2+2*i]>=0)results[2+2*i]=av_read_frame(format,packet);
            results[3+2*i]=packet->pts==AV_NOPTS_VALUE ? -1 : packet->pts*1000/TIMESCALE;
            av_packet_unref(packet);
        }
    }
    results[9]=0x463436;
    return 0;
}
