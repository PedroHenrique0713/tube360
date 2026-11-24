#include "update/launcher.hpp"
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace updates;
namespace {
int checks=0;
void check(bool ok,const char* why) {++checks;if(!ok){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
struct Host:LauncherHost {
    string dir;
    std::vector<string> network,paths;
    LauncherLog logger;
    bool opened=false,closed=false,brokenScreen=false,brokenClose=false;
    explicit Host(string directory):dir(std::move(directory)),logger(dir,[&](const string& s){network.push_back(s);}) {}
    void log(const string& s) override {
        logger.line(s);
        check(read(dir+"launcher.log").find(s)!=string::npos,"every line flushed while launcher remains alive");
    }
    bool screen() override {opened=true;return !brokenScreen;}
    bool closeScreen() override {closed=true;return !brokenClose;}
    int launch(const string& path) override {
        check(opened && closed,"display released before every launch");
        check(read(dir+"launcher.log").find("launch chamada: "+path)!=string::npos,"launch call persisted before handoff");
        paths.push_back(path);errno=ENOEXEC;return -1;
    }
    bool saw(const string& s) const {for(const auto& line:network)if(line.find(s)!=string::npos)return true;return false;}
    int count(const string& s) const {int n=0;for(const auto& line:network)if(line.find(s)!=string::npos)++n;return n;}
};
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    string base=string(argv[1])+"/",key=read(base+"public.txt"),error;
    auto bytes=read(base+"app.json"),sig=read(base+"app.json.sig"),payload=read(base+"download.xex",64*1024*1024);
    auto scenario=[&](const char* name){string dir=base+name+"/";check(::mkdir(dir.c_str(),0700)==0,"isolated scenario directory");check(write(dir+"tube360.xex","XEX2old-good"),"installed known good executable");return dir;};
    auto dir=scenario("launcher-initial");
    write(dir+"default.log","app log sentinel");
    {
        Host host(dir);
        check(launchApp(dir,key,host)==1 && host.paths==std::vector<string>{dir+"tube360.xex"},"first boot needs no journal and returns after one failed launch");
        check(host.saw("journal ausente") && host.saw("errno="+std::to_string(ENOEXEC)),"first boot and exact launch error logged over network sink");
        check(!host.saw("hash inicio"),"first boot never hashes a 10 MB app");
        check(read(dir+"default.log")=="app log sentinel","launcher never writes app log");
    }
    auto before=read(dir+"launcher.log");
    {Host host(dir);launchApp(dir,key,host);}
    check(read(dir+"launcher.log").size()>before.size() && read(dir+"launcher.log").find(before)==0,"launcher log survives a second boot");
    dir=scenario("launcher-missing");std::remove((dir+"tube360.xex").c_str());
    {Host host(dir);check(launchApp(dir,key,host)==1 && host.closed && host.paths.empty() && host.saw("missing_app"),"missing app exits without launch or waiting");}
    dir=scenario("launcher-screen-error");
    {Host host(dir);host.brokenScreen=true;check(launchApp(dir,key,host)==1 && host.closed && host.paths.empty(),"screen error closes resources and exits");}
    dir=scenario("launcher-close-error");
    {Host host(dir);host.brokenClose=true;check(launchApp(dir,key,host)==1 && host.closed && host.paths.empty(),"failed GPU handback exits without launching next title");}
    dir=scenario("launcher-journal");write(dir+"update-app-state.json","invalid");write(dir+"tube360.previous.xex","XEX2backup");write(dir+"tube360.next.xex",payload);
    {Host host(dir);launchApp(dir,key,host);check(host.paths==std::vector<string>{dir+"tube360.previous.xex"} && read(dir+"tube360.next.xex",64*1024*1024)==payload,"invalid journal launches backup and never promotes candidate");}
    dir=scenario("launcher-staged");
    AppUpdate staged(dir,key);check(write(staged.pending(),payload) && staged.stage(bytes,sig,error),"signed 10 MiB candidate staged");
    {
        Host host(dir);launchApp(dir,key,host);
        check(host.paths==std::vector<string>{dir+"tube360.xex",dir+"tube360.xex"},"failed new launch restores backup and attempts it once");
        check(read(dir+"tube360.xex")=="XEX2old-good" && read(dir+"tube360.failed.xex",64*1024*1024)==payload,"launch failure preserves both original and failed candidate");
        check(host.saw("rollback fim") && host.count("hash inicio")==2,"candidate and renamed app verified exactly once each");
        check(host.count("hash bytes=")>=20,"large hash progress reaches persistent and network logs");
        std::cout<<"10 MiB prepare + failed launch + rollback: "<<read(dir+"launcher.log").size()<<" log bytes\n";
    }
    dir=scenario("launcher-path");
    check(::chdir(dir.c_str())==0,"console-like working directory");
    {Host host("");launchApp("",key,host);check(host.paths==std::vector<string>{"game:\\tube360.xex"},"console launch uses explicit game drive");}
    std::cout<<checks<<" launcher checks passed\n";
}
