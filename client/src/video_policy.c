#include "video_policy.h"
#include "boot_log.h"
#include "board_sim.h"
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef __XSDK__
#include <xsdk/kernel.h>
#include <xsdk/log.h>
#endif

static int initialized,native,flags_known;
static uint32_t flags;
static const char* board="unknown";
static const char* request="auto";
static const char* reason="family not validated for native path";
static const char* names[]={"Xenon","Zephyr","Falcon","Jasper","Trinity","Corona","Winchester"};
#ifndef TUBE360_NATIVE_BOARD_ID
#define TUBE360_NATIVE_BOARD_ID (-1)
#endif
static int policy_family=-1,native_default;

int tube360_native_video(void){return native;}
void tube360_video_policy_log(void) {
    boot_event("detect","video.board","source=kernel.XboxHardwareInfo ordinal=156 available=%d flags=%08x model=%s; no SMC/MMIO probe",flags_known,flags,board);
    boot_event("detect","video.policy","requested=%s selected=%s tested_board=%d reason=%s; policy_family=%d policy_model=%s validated_family=%d simulated=%d safe=upstream720+staging native=SD/1080+mapped-R8",request,native?"native":"safe",policy_family>=0 && policy_family==TUBE360_NATIVE_BOARD_ID,reason,policy_family,policy_family<0?"unknown":names[policy_family],TUBE360_NATIVE_BOARD_ID,tube360_board_sim_active());
    tube360_board_sim_log();
}
void tube360_video_policy_normal_log(void) {
#ifdef __XSDK__
    xsdk_log_write("video: model=%s flags=%08x path=%s requested=%s native_default=%d reason=%s",board,flags,native?"native":"safe",request,native_default,reason);
#else
    printf("video: model=desktop path=desktop; console policy does not change desktop Vulkan\n");
#endif
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
#endif
    tube360_board_sim_init();
    policy_family=tube360_board_sim_active()?tube360_board_sim_family():flags_known && (flags>>28)<7?(int)(flags>>28):-1;
    native_default=TUBE360_NATIVE_BOARD_ID>=0 ? policy_family==TUBE360_NATIVE_BOARD_ID : policy_family==5 || policy_family==6;
    if(native_default){native=1;reason="coordinator native baseline retained";}
#ifndef __XSDK__
    if(!tube360_board_sim_active()) {
        reason="desktop Vulkan unaffected; console policy defaults safe";
        tube360_video_policy_log();return;
    }
#endif
    char path[1024];const char* current=boot_log_path();
    const char* slash=NULL;
    for(const char* p=current;*p;++p)if(*p=='/' || *p=='\\')slash=p;
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
            if(failed || more!=EOF || memchr(value,0,last)!=NULL){request="invalid";native=0;reason="invalid driver file; fail safe";}
            else if(!strcmp(value+first,"native")){request="native";native=1;reason="explicit user override; board compatibility unverified";}
            else if(!strcmp(value+first,"safe")){request="safe";native=0;reason="explicit user override";}
            else if(strcmp(value+first,"auto")){request="invalid";native=0;reason="invalid driver file; fail safe";}
        } else if(error!=ENOENT){request="unreadable";native=0;reason="driver file unreadable; fail safe";}
        boot_event("end","video.policy-file","present=%d errno=%d parsed=%s",file!=NULL,error,request);
    } else {request="unreadable";native=0;reason="driver file path too long; fail safe";}
    int override=tube360_board_sim_driver();
    if(override>=0){native=override;reason="explicit board simulation driver; no hardware compatibility claim";}
    tube360_video_policy_log();
}
