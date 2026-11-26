#include "launcher.hpp"
#include "public_key.hpp"
#include "i18n.hpp"
#include "startup.hpp"
#include "boot_log.h"
#include <atomic>
#include <cerrno>
#include <ctime>
#include <pthread.h>
#include <unistd.h>
#include <xsdk/display.h>
#include <xsdk/gpu.h>
#include <xsdk/log.h>
#include <xsdk/present.h>
#include <xsdk/text.h>
#include <xsdk/thread.h>
#include <xsdk/xam.h>

namespace {
struct ReportJob {
    yt::Request request;
    yt::Response response;
    std::atomic_bool done{false},cancel{false};
};
void* submitReport(void* value) {
    std::unique_ptr<std::shared_ptr<ReportJob>> owner(static_cast<std::shared_ptr<ReportJob>*>(value));
    auto job=*owner;
    try {
        auto transport=yt::https(&job->cancel);
        if(transport)job->response=transport->send(job->request);
    } catch(...) {job->response.error="report transport failed";}
    job->done.store(true);
    return nullptr;
}
uint64_t monotonicMs() {
    timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);
    return uint64_t(now.tv_sec)*1000+uint64_t(now.tv_nsec)/1000000;
}
uint32_t deadline(void* data) {
    xsdk_thread_sleep(20000);
    if(static_cast<std::atomic_bool*>(data)->load())startup::fail(i18n::Key::StartupLaunch,i18n::get(startup::language,i18n::Key::StartupTimeout));
    return 0;
}
struct Host:updates::LauncherHost {
    updates::LauncherLog logger{"",[](const updates::string& s){xsdk_log_write("%s",s.c_str());}};
    std::atomic_bool handoff{false};
    bool gpu=false,text=false,present=false,watching=false;
    uint32_t width=0,height=0;
    void log(const updates::string& s) override {logger.line(s);}
    bool screen() override {
        boot_event("begin","launcher.gpu","xsdk_gpu_open");
        auto status=xsdk_gpu_open();gpu=NT_SUCCESS(status);
        log("tela gpu status="+std::to_string(uint32_t(status)));
        if(!gpu)return false;
        boot_event("end","launcher.gpu","status=%08x",unsigned(status));
        boot_event("begin","launcher.swapchain","xsdk_present_open 720p preference");
        status=xsdk_present_open(XSDK_DISPLAY_720P,2,1,&width,&height);present=NT_SUCCESS(status);
        log("tela present status="+std::to_string(uint32_t(status)));
        if(!present)return false;
        boot_event("end","launcher.swapchain","width=%u height=%u",width,height);
        boot_event("begin","launcher.text","embedded font");
        status=xsdk_text_open();text=NT_SUCCESS(status);
        log("tela texto status="+std::to_string(uint32_t(status)));
        if(!text)return false;
        boot_event("end","launcher.text","ready");
        boot_event("begin","launcher.first-frame","begin, dark clear, text, present and fence");
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
        // O bootstrap usa a fonte bitmap ASCII do xsdk; a interface árabe começa no app.
        const char* label=lang==i18n::Lang::AR ? "Tube360" : i18n::get(lang,i18n::Key::LauncherOpening);
        uint32_t w=0,h=0,scale=width>=1000?2:1;
        xsdk_text_measure(label,scale,&w,&h);
        xsdk_text_draw(int32_t(width-w)/2,int32_t(height-h)/2,scale,0xFFFFFFFF,label);
        xsdk_gpu_resolve(address,width,height,0xFF111118);
        xsdk_present_end();
        status=xsdk_gpu_wait(xsdk_gpu_fence(),1000);
        log("tela quadro status="+std::to_string(uint32_t(status)));
        boot_event(NT_SUCCESS(status)?"end":"error","launcher.first-frame","fence=%08x",unsigned(status));
        if(NT_SUCCESS(status))xsdk_thread_sleep(40);
        return NT_SUCCESS(status);
    }
    bool closeScreen() override {
        boot_event("begin","launcher.release-gpu","return GPU before launching app");
        log("tela recursos fechando");
        if(present){xsdk_present_close();present=false;}
        if(text){xsdk_text_close();text=false;}
        if(gpu){auto status=xsdk_gpu_close();gpu=false;log("tela gpu fechada status="+std::to_string(uint32_t(status)));boot_event(NT_SUCCESS(status)?"end":"error","launcher.release-gpu","status=%08x",unsigned(status));return NT_SUCCESS(status);}
        return true;
    }
    updates::string preflight(const updates::string& dir) override {return startup::missingFonts(dir);}
    yt::Response postReport(const yt::Request& request) override {
        // getaddrinfo do xsdk pode bloquear 30 s; o prazo do curl sozinho não limita DNS.
        // O worker possui seus dados e pode terminar após o handoff; nunca referencia Host/pilha.
        auto job=std::make_shared<ReportJob>();job->request=request;
        auto* owner=new std::shared_ptr<ReportJob>(job);
        pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,512*1024);
        pthread_t thread;int status=pthread_create(&thread,&attr,submitReport,owner);pthread_attr_destroy(&attr);
        if(status){delete owner;return {};}
        pthread_detach(thread);
        uint64_t until=monotonicMs()+1500;
        while(!job->done.load() && monotonicMs()<until){timespec pause{0,10000000};nanosleep(&pause,nullptr);}
        if(job->done.load())return job->response;
        job->cancel=true;
        return {};
    }
    void reportCode(const updates::string& code,unsigned duration) override {
        if(!duration)return;
        uint32_t address=0;
        if(!NT_SUCCESS(xsdk_present_begin(&address,100)))return;
        xsdk_gpu_resolve(address,width,height,0xFF111118);
        xsdk_gpu_set_target(width,height);
        uint32_t w=0,h=0,scale=width>=1000?2:1;
        xsdk_text_measure(code.c_str(),scale,&w,&h);
        xsdk_text_draw(int32_t(width-w)/2,int32_t(height-h)/2,scale,0xFFFFFFFF,code.c_str());
        xsdk_gpu_resolve(address,width,height,0xFF111118);
        xsdk_present_end();
        if(NT_SUCCESS(xsdk_gpu_wait(xsdk_gpu_fence(),100)))xsdk_thread_sleep(duration>200?duration-200:0);
    }
    void fail(i18n::Key reason,const updates::string& detail,i18n::Key action) override {
        handoff=false;log("falha visivel: "+detail);startup::configure("");startup::fail(reason,detail,action);
    }
    int launch(const updates::string& path) override {
        if(!watching) {
            // O prazo cobre o handoff, com GPU/callbacks ja devolvidos ao XAM.
            HANDLE watchdog=0;
            handoff=true;
            auto status=xsdk_thread_create_on(&watchdog,64*1024,deadline,&handoff,5);
            log("watchdog handoff 20s status="+std::to_string(uint32_t(status)));
            if(!NT_SUCCESS(status)){errno=EAGAIN;return -1;}
            watching=true;
        }
        boot_event("begin","launcher.handoff","target=%s; game: follows current title on HDD or USB",path.c_str());
        int result=xsdk_launch(path.c_str(),nullptr,0);
        boot_event("error","launcher.handoff","returned=%d errno=%d",result,errno);
        return result;
    }
};
}
int main() {
    boot_open("","launcher");
    boot_environment();
    boot_event("begin","launcher.legacy-log","open launcher.log");
    startup::configure("");
    Host host;
    boot_event("end","launcher.legacy-log","ready");
    host.log(updates::string("imagem=")+(ExLoadedImageName?ExLoadedImageName:"indisponivel"));
    char cwd[1024]{};
    host.log(updates::string("cwd=")+(getcwd(cwd,sizeof(cwd))?cwd:"indisponivel"));
    return updates::launchApp("",updates::publicKeyHex,host);
}
