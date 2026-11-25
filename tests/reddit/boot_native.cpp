// A mesma plataforma/arquivo nativos, com exports e NT I/O simulados em loopback de arquivos.
#include "boot_log.h"
#include <xsdk/kernel.h>
#include <xsdk/net.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

static char image[2048];
extern "C" const char* const ExLoadedImageName=image;
static std::string scenario;
static FILE* raw;
static unsigned syncs;
extern "C" void xsdk_log_write(const char*,...){}
extern "C" void tube360_boot_raw(const char*,const char*);
extern "C" void tube360_boot_raw_close();
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" int __real_fsync(int);
static const char* filename(const char* path){const char* p=std::strrchr(path,'\\');return p?p+1:path;}
extern "C" FILE* __wrap_fopen(const char* path,const char* mode){return __real_fopen(path[0]=='\\'||std::strstr(path,"game:")==path?filename(path):path,mode);}
extern "C" int __wrap_fsync(int fd){++syncs;if(scenario=="sync-fail"){errno=EIO;return -1;}return __real_fsync(fd);}
extern "C" NTSTATUS NtCreateFile(HANDLE* out,uint32_t,const OBJECT_ATTRIBUTES* attr,IO_STATUS_BLOCK*,const uint64_t*,uint32_t,uint32_t,uint32_t,uint32_t){
    raw=__real_fopen(filename(attr->name->pointer),"w");*out=raw?77:0;return raw?0:-1;
}
extern "C" NTSTATUS NtWriteFile(HANDLE f,HANDLE,void*,void*,IO_STATUS_BLOCK*,const void* bytes,uint32_t size,const uint64_t*){assert(f==77);return std::fwrite(bytes,1,size,raw)==size?0:-1;}
extern "C" NTSTATUS NtFlushBuffersFile(HANDLE f,IO_STATUS_BLOCK*){assert(f==77);assert(!std::fflush(raw));return __wrap_fsync(fileno(raw));}
extern "C" NTSTATUS NtClose(HANDLE f){assert(f==77);std::fclose(raw);raw=nullptr;return 0;}
extern "C" uint32_t XexGetModuleHandle(const char* name,HANDLE* out){
    if(scenario=="absent" || !std::strcmp(name,"xbdm.xex"))return 126;
    *out=!std::strcmp(name,"xam.xex")?2:1;return 0;
}
static uint16_t version[]={2,0,17559,0};
static void video(VIDEO_MODE* out){out->display_width=1280;out->display_height=720;out->is_widescreen=1;out->refresh_rate=60;}
static NTSTATUS memory(MM_QUERY_STATISTICS_RESULT* out){out->total_physical_pages=131072;out->title.available_pages=90000;return 0;}
static uint32_t av(){return scenario=="component"?3:scenario=="unknown-av"?123:8;}
extern "C" NTSTATUS XexGetProcedureAddress(HANDLE module,uint32_t ordinal,void** out){
    assert(module==1 || module==2);
    *out=nullptr;
    if(module==1 && ordinal==0x158)*out=version;
    if(module==1 && ordinal==0x1ca)*out=reinterpret_cast<void*>(video);
    if(module==1 && ordinal==0xc6)*out=reinterpret_cast<void*>(memory);
    if(module==2 && ordinal==0x3cb)*out=reinterpret_cast<void*>(av);
    return *out?0:-1;
}
extern "C" int xsdk_net_link(uint32_t* value){*value=1;return scenario=="absent"?ENETDOWN:0;}
extern "C" int xsdk_net_address(in_addr*){return EAGAIN;}
int main(int argc,char** argv){
    assert(argc==3);scenario=argv[1];assert(!chdir(argv[2]));
    std::string root=scenario=="hdd"?"\\Device\\Harddisk0\\Partition1\\Tube360\\":"\\Device\\Mass0\\Tube360\\";
    if(scenario=="long")root="\\Device\\Mass0\\"+std::string(235,'x')+"\\";
    std::snprintf(image,sizeof(image),"%stube360.xex",root.c_str());
    tube360_boot_raw("begin","crt.entry");tube360_boot_raw("begin","crt.locks");
    tube360_boot_raw("end","crt.locks");tube360_boot_raw_close();
    boot_open("","app");boot_environment();boot_network();
    errno=E2BIG;
    boot_event("begin","fixture.stop","terminating without boot_close/fclose");
    assert(errno==E2BIG);
    std::printf("syncs=%u path=%s\n",syncs,boot_log_path());std::fflush(stdout);
    // Simula uma interrupção sem destrutores: o último begin já precisa existir no arquivo.
    std::_Exit(0);
}
