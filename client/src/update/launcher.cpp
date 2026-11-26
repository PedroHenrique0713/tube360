#include "launcher.hpp"
#include "boot_log.h"
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <ctime>

namespace updates {
static uint64_t millis() {
    timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);
    return uint64_t(now.tv_sec)*1000+uint64_t(now.tv_nsec)/1000000;
}
LauncherLog::LauncherLog(const string& directory,Trace sink):file(std::fopen((directory+"launcher.log").c_str(),"a")),network(std::move(sink)),started(millis()) {}
LauncherLog::~LauncherLog(){if(file)std::fclose(file);}
void LauncherLog::line(const string& message) {
    boot_event("info","launcher","%s",message.c_str());
    string line="launcher +"+std::to_string(millis()-started)+"ms: "+message;
    if(file){std::fprintf(file,"%s\n",line.c_str());std::fflush(file);if(fsync(fileno(file)))boot_event("error","launcher.log-sync","errno=%d",errno);}
    if(network)network(line);
}
int launchApp(const string& directory,const string& key,LauncherHost& host) {
    host.log("inicio build="+std::to_string(buildId)+" version="+appVersion);
    host.log("tela inicio: Tube360 – abrindo…");
    if(!host.screen()){host.log("tela erro");host.closeScreen();if(read(directory+"session.txt").rfind("open\n",0)!=0)boot_session_begin(directory.c_str(),"launcher display failed");host.fail(i18n::Key::StartupVideo,"launcher display",i18n::Key::StartupRetry);return 1;}
    host.log("tela pronta");
    auto reportStarted=millis();
    if(reports::enabled(directory)) {
        string pending=reports::sanitize(read(directory+"error-report.json"));
        if(pending.empty()) {
            pending=reports::make(directory);
            if(!pending.empty() && !write(directory+"error-report.json",pending))pending.clear();
        }
        if(!pending.empty()) {
            if(read(directory+"error-report.sent")==sha256(pending)) {
                std::remove((directory+"error-report.json").c_str());
                write(directory+"session.txt","reported\n");
                pending.clear();
            }
        }
        if(!pending.empty()) {
            // 1,5 s de rede + 4 s de código; coleta local limitada e sem tentativas nesta partida.
            yt::Request request;request.url=reports::endpoint;request.agent="Tube360/1.0";
            request.body=pending;request.timeoutMs=1500;request.connectTimeoutMs=1000;request.maxBytes=1024;
            auto code=reports::code(host.postReport(request));
            if(!code.empty()) {
                if(write(directory+"error-report.sent",sha256(pending))) {
                    std::remove((directory+"error-report.json").c_str());
                    // Uma sessão já enviada não volta à fila se o launcher falhar antes do app.
                    write(directory+"session.txt","reported\n");
                }
                unsigned elapsed=unsigned(millis()-reportStarted);
                host.reportCode(code,elapsed<5800?std::min(4000u,5800-elapsed):0);
                host.log("relatorio enviado: "+code);
            } else host.log("relatorio pendente; proxima partida");
        }
    }
    boot_session_begin(directory.c_str(),("launcher=1\nversion="+string(appVersion)+" build="+std::to_string(buildId)).c_str());
    auto missing=host.preflight(directory);
    if(!missing.empty()){host.closeScreen();host.fail(i18n::Key::StartupFonts,missing,i18n::Key::StartupCopy);return 1;}
    AppUpdate app(directory,key,[&](const string& s){host.log(s);});
    string error,path=app.prepare(error);
    host.log("prepare fim caminho="+path+" erro="+(error.empty()?"nenhum":error));
    host.log("tela fechando antes do launch");
    if(!host.closeScreen()){host.log("tela fechamento falhou");host.fail(i18n::Key::StartupVideo,"launcher GPU handback",i18n::Key::StartupRetry);return 1;}
    if(path.empty()){host.log("sem app");host.fail(i18n::Key::StartupLaunch,error,i18n::Key::StartupCopy);return 1;}
    auto attempt=[&](const string& target) {
        // game: explicita a pasta do titulo; o xsdk tambem resolve caminhos relativos para ela.
        string resolved=directory.empty()?"game:\\"+target:target;
        host.log("caminho resolvido="+resolved);
        host.log("launch chamada: "+resolved);
        boot_session_begin(directory.c_str(),("launcher=1\nversion="+string(appVersion)+" build="+std::to_string(buildId)).c_str());
        errno=0;int result=host.launch(resolved);int saved=errno;
        host.log("launch retorno="+std::to_string(result)+" errno="+std::to_string(saved)+" ("+std::strerror(saved)+")");
    };
    attempt(path);
    host.log("launch falhou; verificando rollback");
    if(app.failed(error))attempt(app.executable());
    host.log("fim erro="+(error.empty()?string("launch"):error)+"; saindo para Aurora/dashboard");
    host.fail(i18n::Key::StartupLaunch,error.empty()?"tube360.xex (launch failed)":error,i18n::Key::StartupCopy);
    return 1;
}
}
