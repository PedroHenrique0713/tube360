#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include "gpu_private.h"
#include "xsdk/display.h"
#include "xsdk/kernel.h"
#include "xsdk/tube360_video.h"

NTSTATUS reference_xsdk_display_open(XsdkDisplayMode,uint32_t*,uint32_t*);
NTSTATUS reference_xsdk_display_show(uint32_t,uint32_t);
void reference_xsdk_display_close(void);
static int native,events,stopped;static uint64_t ticks;
static VIDEO_MODE mode;
int tube360_native_video(void){return native;}
void boot_event(const char* phase,const char* step,const char* fmt,...){
    ++events;printf("%s %s ",phase,step);va_list a;va_start(a,fmt);vprintf(fmt,a);va_end(a);puts("");
}
void xsdk_log_write(const char* fmt,...){(void)fmt;}
void VdQueryVideoMode(VIDEO_MODE* out){*out=mode;}
NTSTATUS xsdk_thread_sleep(uint32_t ms){
    ticks+=(uint64_t)ms*1000000;gpu_registers[0x1928]=2;gpu_registers[0x183b]=0;return 0;
}
int fixture_clock_gettime(clockid_t clock,struct timespec* out){
    (void)clock;ticks+=1000000;out->tv_sec=ticks/1000000000;out->tv_nsec=ticks%1000000000;return 0;
}
static void reset(void){
    memset((void*)gpu_registers,0,0x20000);
    gpu_registers[0x1961]=640u<<16|480;gpu_registers[0x1844]=0x100000;gpu_registers[0x1848]=640;
    gpu_registers[0x184d]=640;gpu_registers[0x184e]=480;gpu_registers[0x194c]=stopped?0:2000;
}
int main(int argc,char** argv){
    assert(argc==2);void* mapped=mmap((void*)0x7fc80000,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(mapped==(void*)0x7fc80000);stopped=strstr(argv[1],"counter-stopped")!=NULL;mode=(VIDEO_MODE){.display_width=1920,.display_height=1080,.is_widescreen=1,.refresh_rate=60};
    if(strstr(argv[1],"sd")){mode.display_width=640;mode.display_height=480;mode.is_widescreen=0;}
    if(strstr(argv[1],"interlaced"))mode.is_interlaced=1;
    native=!strncmp(argv[1],"native",6);uint32_t width=0,height=0;
    uint32_t reference[0x20000/4];
    if(!native){
        reset();assert(NT_SUCCESS(reference_xsdk_display_open(XSDK_DISPLAY_720P,&width,&height)));
        assert(NT_SUCCESS(reference_xsdk_display_show(0x200000,width)));
        memcpy(reference,(void*)gpu_registers,sizeof(reference));reference_xsdk_display_close();
    }
    reset();
    XsdkDisplayMode requested=strstr(argv[1],"sd")?XSDK_DISPLAY_480P:XSDK_DISPLAY_1080P;
    assert(NT_SUCCESS(xsdk_display_open(requested,&width,&height)));
    assert(NT_SUCCESS(xsdk_display_show(0x200000,width)));
    if(!native)assert(!memcmp(reference,(void*)gpu_registers,sizeof(reference)));
    else assert(height==(strstr(argv[1],"sd")?480:1080));
    tube360_display_boot_done();int count=events;
    if(!mode.is_interlaced){DisplaySize size;assert(xsdk_display_plan(width,height,width,&size));size.scaler[0]^=1;xsdk_display_flip_sized(0x300000,&size);}
    else xsdk_display_flip(0x300000);
    assert(events==count); // O caminho chamado na IRQ não grava arquivo.
    xsdk_display_close();assert(!munmap(mapped,0x20000));
    printf("PASS display %s extent=%ux%u safe_registers_match_original=%d IRQ_logging=0\n",argv[1],width,height,!native);
}
