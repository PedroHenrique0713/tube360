#include "video_policy.h"
#include "boot_log.h"
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef __XSDK__
#include <xsdk/kernel.h>
#endif

static int initialized,native,flags_known;
static uint32_t flags;
static const char* board="unknown";
static const char* request="auto";
static const char* reason="board not validated; empty tested-board allowlist";
static const char* names[]={"Xenon","Zephyr","Falcon","Jasper","Trinity","Corona","Winchester"};

int tube360_native_video(void){return native;}
void tube360_video_policy_log(void) {
    boot_event("detect","video.board","source=kernel.XboxHardwareInfo ordinal=156 available=%d flags=%08x model=%s; no SMC/MMIO probe",flags_known,flags,board);
    boot_event("detect","video.policy","requested=%s selected=%s tested_board=0 reason=%s; safe=upstream720+staging native=SD/1080+mapped-R8",request,native?"native":"safe",reason);
}
void tube360_video_policy_init(void) {
    if(initialized)return;
    initialized=1;
#ifdef __XSDK__
    boot_event("begin","video.board","optional kernel data lookup, before video hardware");
    HANDLE module=0;void* address=NULL;
    uint32_t module_status=XexGetModuleHandle("xboxkrnl.exe",&module);
    NTSTATUS status=module_status? -1:XexGetProcedureAddress(module,0x156,&address);
    if(NT_SUCCESS(status) && address) {
        memcpy(&flags,address,sizeof(flags));flags_known=1;
        unsigned type=flags>>28;if(type<sizeof(names)/sizeof(names[0]))board=names[type];
    }
    boot_event("end","video.board","module_status=%08x procedure_status=%08x available=%d",module_status,(unsigned)status,flags_known);
    char path[1024];const char* current=boot_log_path();
    const char* slash=strrchr(current,'\\');
    size_t parent=slash?(size_t)(slash-current+1):0;
    if(parent+sizeof("video-driver.txt")<=sizeof(path)) {
        memcpy(path,current,parent);strcpy(path+parent,"video-driver.txt");
        boot_event("begin","video.policy-file","path=%s; read once before any GPU initialization",path);
        FILE* file=fopen(path,"r");int error=file?0:errno;
        if(file) {
            char value[64];size_t size=fread(value,1,sizeof(value)-1,file);
            int more=fgetc(file);int failed=ferror(file);fclose(file);value[size]=0;
            size_t first=0,last=size;
            while(first<last && isspace((unsigned char)value[first]))++first;
            while(last>first && isspace((unsigned char)value[last-1]))--last;
            value[last]=0;
            if(failed || more!=EOF || memchr(value,0,last)!=NULL){request="invalid";reason="invalid driver file; fail safe";}
            else if(!strcmp(value+first,"native")){request="native";native=1;reason="explicit user override; board compatibility unverified";}
            else if(!strcmp(value+first,"safe")){request="safe";reason="explicit user override";}
            else if(strcmp(value+first,"auto")){request="invalid";reason="invalid driver file; fail safe";}
        } else if(error!=ENOENT){request="unreadable";reason="driver file unreadable; fail safe";}
        boot_event("end","video.policy-file","present=%d errno=%d parsed=%s",file!=NULL,error,request);
    } else {request="unreadable";reason="driver file path too long; fail safe";}
#else
    // O desktop não emula a placa nem muda seu driver Vulkan pelo arquivo do Xbox.
    (void)names;reason="desktop Vulkan unaffected; console policy defaults safe";
#endif
    tube360_video_policy_log();
}
