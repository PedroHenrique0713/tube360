#include "video_policy.h"
#include <xsdk/kernel.h>
#include <xsdk/log.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t flags;
static int missing,denied;
FILE* __real_fopen(const char*,const char*);
const char* const ExLoadedImageName="\\Device\\Mass0\\Tube360\\default.xex";
static FILE* normal;
void RtlInitializeCriticalSection(RTL_CRITICAL_SECTION* s){(void)s;}
void RtlEnterCriticalSection(RTL_CRITICAL_SECTION* s){(void)s;}
void RtlLeaveCriticalSection(RTL_CRITICAL_SECTION* s){(void)s;}
NTSTATUS NtCreateFile(HANDLE* out,uint32_t access,const OBJECT_ATTRIBUTES* attr,IO_STATUS_BLOCK* io,const uint64_t* size,uint32_t flags,uint32_t share,uint32_t create,uint32_t options){
    (void)access;(void)attr;(void)io;(void)size;(void)flags;(void)share;(void)create;(void)options;
    normal=__real_fopen("sdk-normal.log","w");*out=normal?77:0;return normal?0:-1;
}
NTSTATUS NtWriteFile(HANDLE file,HANDLE event,void* apc,void* context,IO_STATUS_BLOCK* io,const void* bytes,uint32_t size,const uint64_t* offset){
    (void)event;(void)apc;(void)context;(void)io;(void)offset;assert(file==77);return fwrite(bytes,1,size,normal)==size?0:-1;
}
NTSTATUS NtClose(HANDLE file){assert(file==77);fclose(normal);return 0;}
uint32_t XexGetModuleHandle(const char* name,HANDLE* out){assert(!strcmp(name,"xboxkrnl.exe"));*out=1;return missing==2?126:0;}
NTSTATUS XexGetProcedureAddress(HANDLE module,uint32_t ordinal,void** out){assert(module==1 && ordinal==0x156);*out=missing?NULL:&flags;return missing?-1:0;}
const char* boot_log_path(void){return "\\Device\\Mass0\\Tube360\\boot-app.log";}
void boot_event(const char* phase,const char* step,const char* fmt,...){
    printf("%s %s ",phase,step);va_list args;va_start(args,fmt);vprintf(fmt,args);va_end(args);puts("");
}
FILE* __real_fopen(const char*,const char*);
FILE* __wrap_fopen(const char* path,const char* mode){
    if(strstr(path,"board-sim.txt")){errno=ENOENT;return NULL;}
    if(!strstr(path,"video-driver.txt"))return __real_fopen(path,mode);
    assert(!strcmp(path,"\\Device\\Mass0\\Tube360\\video-driver.txt"));
    if(denied){errno=EACCES;return NULL;}
    return __real_fopen("video-driver.txt",mode);
}
int main(int argc,char** argv){
    assert(argc==5);flags=(uint32_t)strtoul(argv[1],NULL,16);missing=atoi(argv[2]);denied=atoi(argv[3]);
    assert(!tube360_native_video());assert(NT_SUCCESS(xsdk_log_open()));tube360_video_policy_init();tube360_video_policy_normal_log();
    assert(tube360_native_video()==atoi(argv[4]));
    // A seleção fica imutável: IRQ e alocações futuras não relêem o arquivo.
    FILE* changed=__real_fopen("video-driver.txt","w");assert(changed);fputs(tube360_native_video()?"safe":"native",changed);fclose(changed);
    tube360_video_policy_init();assert(tube360_native_video()==atoi(argv[4]));
    xsdk_log_close();
}
