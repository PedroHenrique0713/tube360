#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu_private.h"
#include "xsdk/display.h"
#include "xsdk/present.h"
#include "xsdk/kernel.h"
#include "xsdk/splash.h"
static int count,freed,installed,closed,trace_done,letterbox;
static NTSTATUS shown;
static int simulation_failure,simulation_interlaced;
int tube360_board_sim_step(unsigned stage){assert(stage==0);return simulation_failure;}
int tube360_board_sim_display_ok(int native){(void)native;return !simulation_interlaced;}
int tube360_native_video(void){return 0;}
void boot_event(const char* phase,const char* step,const char* fmt,...){printf("%s %s ",phase,step);va_list a;va_start(a,fmt);vprintf(fmt,a);va_end(a);puts("");}
void fixture_barrier(void){}
void tube360_display_boot_done(void){trace_done=1;}
void xsdk_splash_end(void){}
void xsdk_splash_free(void){}
bool xsdk_splash_hand_over(uint32_t* w,uint32_t* h){(void)w;(void)h;return false;}
NTSTATUS xsdk_display_open(XsdkDisplayMode mode,uint32_t* w,uint32_t* h){assert(mode==XSDK_DISPLAY_720P);*w=1280;*h=720;return 0;}
bool xsdk_display_letterboxed(void){return letterbox;}
bool xsdk_display_plan(uint32_t w,uint32_t h,uint32_t p,DisplaySize* out){(void)w;(void)h;(void)p;memset(out,0,sizeof(*out));return letterbox;}
void xsdk_display_close(void){++closed;}
uint32_t xsdk_gpu_resolve_bytes(uint32_t w,uint32_t h){return w*h*4;}
volatile uint32_t* xsdk_gpu_alloc(uint32_t bytes){++count;return malloc(bytes);}
void xsdk_gpu_free(volatile uint32_t* p){++freed;free((void*)p);}
uint32_t xsdk_gpu_address(const volatile uint32_t* p){(void)p;return count*0x1000000;}
NTSTATUS xsdk_display_show(uint32_t a,uint32_t p){(void)a;(void)p;return shown;}
NTSTATUS xsdk_display_show_sized(uint32_t a,const DisplaySize* s){(void)a;(void)s;return shown;}
void VdSetGraphicsInterruptCallback(VdGraphicsInterruptCallback callback,void* context){(void)callback;(void)context;assert(trace_done);++installed;}
int main(int argc,char** argv){
    assert(argc==2);letterbox=strstr(argv[1],"letterbox")!=NULL;
    simulation_failure=!strcmp(argv[1],"sim-fail");simulation_interlaced=!strcmp(argv[1],"sim-interlaced");
    shown=strstr(argv[1],"fail")?STATUS_IO_TIMEOUT:0;
    uint32_t w=0,h=0;NTSTATUS status=xsdk_present_open(XSDK_DISPLAY_1080P,2,1,&w,&h);
    if(simulation_failure || simulation_interlaced) {
        assert(status==(simulation_failure?STATUS_IO_TIMEOUT:STATUS_NOT_SUPPORTED));
        assert(!count && !freed && !installed && !closed);
        printf("PASS present %s status=%08x buffers=0 callbacks=0\n",argv[1],(unsigned)status);return 0;
    }
    assert(status==shown && count==2);
    if(!NT_SUCCESS(shown))assert(!installed && freed==2 && closed==1);
    else assert(installed==1 && !freed && !closed);
    printf("PASS present %s status=%08x buffers=%d freed=%d callbacks=%d\n",argv[1],(unsigned)status,count,freed,installed);
}
