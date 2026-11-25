#include "video_policy.h"
#include "board_sim.h"
#include <xsdk/kernel.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
static uint32_t flags;
static int absent;
uint32_t XexGetModuleHandle(const char* n,HANDLE* out){assert(!strcmp(n,"xboxkrnl.exe"));*out=1;return absent?126:0;}
NTSTATUS XexGetProcedureAddress(HANDLE h,uint32_t n,void** out){assert(h==1 && n==0x156);*out=&flags;return 0;}
const char* boot_log_path(void){return "boot-app.log";}
void boot_event(const char* phase,const char* step,const char* fmt,...){
    printf("%s %s ",phase,step);va_list a;va_start(a,fmt);vprintf(fmt,a);va_end(a);puts("");
}
void xsdk_log_write(const char* fmt,...){(void)fmt;}
int main(int argc,char** argv){
    assert(argc==4);flags=(uint32_t)strtoul(argv[1],NULL,16);absent=atoi(argv[2]);
    tube360_video_policy_init();assert(tube360_native_video()==atoi(argv[3]));
    int selected=tube360_native_video();
    FILE* f=fopen("board-sim.txt","w");assert(f);fputs("board=xenon\ndriver=native\n",f);fclose(f);
    tube360_video_policy_init();assert(tube360_native_video()==selected);
    puts("PASS native kernel-data policy and immutable simulation");
}
