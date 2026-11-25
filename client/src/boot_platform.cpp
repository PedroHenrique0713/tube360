#include "boot_log.h"
#include "video_policy.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <unistd.h>
#ifdef __XSDK__
#include <xsdk/kernel.h>
#include <xsdk/net.h>
#endif

namespace {
#ifdef __XSDK__
void* procedure(const char* module,unsigned ordinal,const char* label) {
    HANDLE handle=0;void* result=nullptr;
    boot_event("begin","environment.export","%s module=%s ordinal=%x optional=1",label,module,ordinal);
    uint32_t moduleStatus=XexGetModuleHandle(module,&handle);
    NTSTATUS status=moduleStatus==0?XexGetProcedureAddress(handle,ordinal,&result):-1;
    boot_event("end","environment.export","%s module=%s ordinal=%x module_status=%08x procedure_status=%08x present=%d optional=1",
               label,module,ordinal,unsigned(moduleStatus),unsigned(status),result!=nullptr);
    return status>=0?result:nullptr;
}
#endif
const char* drive(const char* image) {
    if(std::strstr(image,"Mass0") || std::strstr(image,"Usb0") || std::strstr(image,"usb0"))return "USB0";
    if(std::strstr(image,"Mass1") || std::strstr(image,"Usb1"))return "USB1";
    if(std::strstr(image,"Harddisk") || std::strstr(image,"HDD") || std::strstr(image,"Hdd"))return "HDD";
    return "unknown";
}
}
extern "C" void boot_environment() {
    tube360_video_policy_init();
    tube360_video_policy_log();
    tube360_video_policy_normal_log();
    char cwd[1024]{};const char* current=getcwd(cwd,sizeof(cwd))?cwd:"unavailable";
#ifdef __XSDK__
    const char* image=ExLoadedImageName?ExLoadedImageName:"unknown";
#else
    const char* image="desktop";
#endif
    boot_event("info","storage.paths","image=%s drive=%s cwd=%s log=%s; game: is title folder, not a fixed HDD drive",image,drive(image),current,boot_log_path());
    boot_event("begin","storage.volume","filesystem=unknown; no disk scan or format assumptions");
    struct statvfs volume{};int result=statvfs(current,&volume);
    boot_event("end","storage.volume","result=%d errno=%d block_bytes=%lu available_blocks=%llu name_max=%lu; FAT32 file limit not inferred",result,result?errno:0,(unsigned long)volume.f_frsize,(unsigned long long)volume.f_bavail,(unsigned long)volume.f_namemax);
#ifdef __XSDK__
    boot_event("info","environment.policy","exploit=unknown; no BadUpdate detection or kernel/HV patch; XBDM and dbgprint not required");
    boot_event("begin","environment.xbdm","optional module lookup, no XBDM calls");
    HANDLE debugger=0;int debuggerStatus=XexGetModuleHandle("xbdm.xex",&debugger);
    boot_event("end","environment.xbdm","module_status=%08x present=%d required=0",unsigned(debuggerStatus),debuggerStatus==0);
    if(auto* address=procedure("xboxkrnl.exe",0x158,"XboxKrnlVersion")) {
        uint16_t version[4];std::memcpy(version,address,sizeof(version));
        boot_event("info","environment.kernel","version=%u.%u.%u.%u; identity does not identify exploit",version[0],version[1],version[2],version[3]);
    }
    auto video=reinterpret_cast<void(*)(VIDEO_MODE*)>(procedure("xboxkrnl.exe",0x1ca,"VdQueryVideoMode"));
    if(video){VIDEO_MODE mode{};boot_event("begin","video.mode","query only; HDMI timings unchanged");video(&mode);
        boot_event("end","video.mode","dashboard=%ux%u interlaced=%u wide=%u hi_def=%u hz=%.3f standard=%u",mode.display_width,mode.display_height,mode.is_interlaced,mode.is_widescreen,mode.is_hi_def,double(mode.refresh_rate),mode.video_standard);}
    auto av=reinterpret_cast<uint32_t(*)()>(procedure("xam.xex",0x3cb,"XGetAVPack"));
    if(av){boot_event("begin","video.connector","API enum; no SMC write or display-mode change");unsigned pack=av();
        // Enum documentado pelo Xenia Canary; preservar o valor bruto para revisões desconhecidas.
        const char* label=pack==4 || pack==8?"HDMI":pack==0 || pack==3?"component":pack==5?"composite/S-Video":pack==2?"SCART":pack==7?"TV":pack==6?"VGA":"other/unknown";
        boot_event("end","video.connector","avpack=%u connector=%s (unrecognized codes remain unknown)",pack,label);}
    auto memory=reinterpret_cast<NTSTATUS(*)(MM_QUERY_STATISTICS_RESULT*)>(procedure("xboxkrnl.exe",0xc6,"MmQueryStatistics"));
    if(memory){boot_event("begin","memory.available","query title pages");MM_QUERY_STATISTICS_RESULT stats{};stats.size=sizeof(stats);auto status=memory(&stats);
        boot_event("end","memory.available","status=%08x total_pages=%u title_available_pages=%u",unsigned(status),stats.total_physical_pages,stats.title.available_pages);}
#else
    boot_event("info","environment.policy","desktop; console exports/video connector not probed; exploit=unknown");
#endif
    boot_event("end","environment","optional diagnostics complete; no exploit compatibility claim");
}
extern "C" void boot_network() {
    boot_event("begin","network.init","BSD socket probe; no remote connection or Xbox Live login");
    int fd=socket(AF_INET,SOCK_STREAM,0),error=fd<0?errno:0;
    if(fd>=0)close(fd);
    boot_event("end","network.init","socket=%s errno=%d; TLS verification remains required",fd>=0?"available":"unavailable",error);
#ifdef __XSDK__
    boot_event("begin","network.link","nonblocking XAM link/address status");
    uint32_t flags=0;struct in_addr address{};
    int link=xsdk_net_link(&flags),ip=xsdk_net_address(&address);
    boot_event("end","network.link","link_result=%d link_flags=%08x address_result=%d (EAGAIN is DHCP pending)",link,flags,ip);
#endif
}
