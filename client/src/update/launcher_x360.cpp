#include "launcher.hpp"
#include "public_key.hpp"
#include "i18n.hpp"
#include <cerrno>
#include <unistd.h>
#include <xsdk/display.h>
#include <xsdk/gpu.h>
#include <xsdk/log.h>
#include <xsdk/present.h>
#include <xsdk/text.h>
#include <xsdk/thread.h>
#include <xsdk/xam.h>

namespace {
uint32_t deadline(void*) {
    xsdk_thread_sleep(20000);
    XamLoaderTerminateTitle();
    return 0;
}
struct Host:updates::LauncherHost {
    updates::LauncherLog logger{"",[](const updates::string& s){xsdk_log_write("%s",s.c_str());}};
    bool gpu=false,text=false,present=false,watching=false;
    void log(const updates::string& s) override {logger.line(s);}
    bool screen() override {
        auto status=xsdk_gpu_open();gpu=NT_SUCCESS(status);
        log("tela gpu status="+std::to_string(uint32_t(status)));
        if(!gpu)return false;
        uint32_t width=0,height=0;
        status=xsdk_present_open(XSDK_DISPLAY_720P,2,1,&width,&height);present=NT_SUCCESS(status);
        log("tela present status="+std::to_string(uint32_t(status)));
        if(!present)return false;
        status=xsdk_text_open();text=NT_SUCCESS(status);
        log("tela texto status="+std::to_string(uint32_t(status)));
        if(!text)return false;
        uint32_t address=0;
        status=xsdk_present_begin(&address,1000);
        log("tela begin status="+std::to_string(uint32_t(status)));
        if(!NT_SUCCESS(status))return false;
        xsdk_gpu_resolve(address,width,height,0xFF111118);
        xsdk_gpu_set_target(width,height);
        auto lang=i18n::Lang::EN;
        if(FILE* settings=std::fopen("settings.txt","r")) {
            char line[128];
            while(std::fgets(line,sizeof(line),settings)) {
                char value[8];if(std::sscanf(line,"lang=%7s",value)==1)lang=i18n::language(value);
            }
            std::fclose(settings);
        }
        const char* label=i18n::get(lang,i18n::Key::LauncherOpening);
        uint32_t w=0,h=0,scale=width>=1000?2:1;
        xsdk_text_measure(label,scale,&w,&h);
        xsdk_text_draw(int32_t(width-w)/2,int32_t(height-h)/2,scale,0xFFFFFFFF,label);
        xsdk_gpu_resolve(address,width,height,0xFF111118);
        xsdk_present_end();
        status=xsdk_gpu_wait(xsdk_gpu_fence(),1000);
        log("tela quadro status="+std::to_string(uint32_t(status)));
        if(NT_SUCCESS(status))xsdk_thread_sleep(40);
        return NT_SUCCESS(status);
    }
    bool closeScreen() override {
        log("tela recursos fechando");
        if(present){xsdk_present_close();present=false;}
        if(text){xsdk_text_close();text=false;}
        if(gpu){auto status=xsdk_gpu_close();gpu=false;log("tela gpu fechada status="+std::to_string(uint32_t(status)));return NT_SUCCESS(status);}
        return true;
    }
    int launch(const updates::string& path) override {
        if(!watching) {
            // O prazo cobre o handoff, com GPU/callbacks ja devolvidos ao XAM.
            HANDLE watchdog=0;
            auto status=xsdk_thread_create_on(&watchdog,64*1024,deadline,nullptr,5);
            log("watchdog handoff 20s status="+std::to_string(uint32_t(status)));
            if(!NT_SUCCESS(status)){errno=EAGAIN;return -1;}
            watching=true;
        }
        return xsdk_launch(path.c_str(),nullptr,0);
    }
};
}
int main() {
    Host host;
    host.log(updates::string("imagem=")+(ExLoadedImageName?ExLoadedImageName:"indisponivel"));
    char cwd[1024]{};
    host.log(updates::string("cwd=")+(getcwd(cwd,sizeof(cwd))?cwd:"indisponivel"));
    return updates::launchApp("",updates::publicKeyHex,host);
}
