#include "video_policy.h"
#include <xsdk/kernel.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t flags;
static int missing,denied;
uint32_t XexGetModuleHandle(const char* name,HANDLE* out){assert(!strcmp(name,"xboxkrnl.exe"));*out=1;return missing==2?126:0;}
NTSTATUS XexGetProcedureAddress(HANDLE module,uint32_t ordinal,void** out){assert(module==1 && ordinal==0x156);*out=missing?NULL:&flags;return missing?-1:0;}
const char* boot_log_path(void){return "\\Device\\Mass0\\Tube360\\boot-app.log";}
void boot_event(const char* phase,const char* step,const char* fmt,...){
    printf("%s %s ",phase,step);va_list args;va_start(args,fmt);vprintf(fmt,args);va_end(args);puts("");
}
FILE* __real_fopen(const char*,const char*);
FILE* __wrap_fopen(const char* path,const char* mode){
    assert(!strcmp(path,"\\Device\\Mass0\\Tube360\\video-driver.txt"));
    if(denied){errno=EACCES;return NULL;}
    return __real_fopen("video-driver.txt",mode);
}
int main(int argc,char** argv){
    assert(argc==5);flags=(uint32_t)strtoul(argv[1],NULL,16);missing=atoi(argv[2]);denied=atoi(argv[3]);
    assert(!tube360_native_video());tube360_video_policy_init();
    assert(tube360_native_video()==atoi(argv[4]));
    // A seleção fica imutável: IRQ e alocações futuras não relêem o arquivo.
    FILE* changed=__real_fopen("video-driver.txt","w");assert(changed);fputs(tube360_native_video()?"safe":"native",changed);fclose(changed);
    tube360_video_policy_init();assert(tube360_native_video()==atoi(argv[4]));
}
