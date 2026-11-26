#include "update/launcher.hpp"
#include "boot_log.h"
#include <cstdlib>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
using namespace updates;
namespace {
int checks=0;
void check(bool ok,const char* why){++checks;if(!ok){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
struct Host:LauncherHost {
    int posts=0,launches=0;unsigned duration=0;string shown,body;
    yt::Response response{200,"{\"code\":\"T360-7K2Q\"}",{}};
    bool randomBytes(unsigned char* bytes,size_t n) override {static unsigned seed=1;for(size_t i=0;i<n;++i)bytes[i]=static_cast<unsigned char>(seed+i);++seed;return true;}
    void log(const string&) override {}
    bool screen() override {return true;}
    bool closeScreen() override {return true;}
    int launch(const string&) override {++launches;return -1;}
    yt::Response postReport(const yt::Request& r) override {
        ++posts;body=r.body;
        J payload;check(parse(body,payload) && reports::installationValid(text(payload,"installation_id")) && !text(payload,"category").empty(),"wire report has persistent anonymous ID and category");
        check(r.url=="https://tube360.hypermind.space/api/report" && r.agent=="Tube360/1.0","exact destination and user agent");
        check(r.timeoutMs<=1500 && r.connectTimeoutMs<=1000 && r.maxBytes==1024,"bounded single attempt and response");
        check(body.size()<=65536,"wire body respects cap");return response;
    }
    void reportCode(const string& code,unsigned ms) override {shown=code;duration=ms;}
};
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    if(string(argv[1])=="--scrub"){std::cout<<reports::scrub(read(argv[2],4*1024*1024));return 0;}
    string base=string(argv[1])+"/";int n=0;
    auto scenario=[&](){string d=base+std::to_string(n++)+"/";check(!mkdir(d.c_str(),0700),"isolated directory");write(d+"tube360.xex","XEX2fixture");return d;};
    auto installDir=scenario();unsigned entropyCalls=0;
    auto entropy=[&](unsigned char* bytes,size_t size){++entropyCalls;for(size_t i=0;i<size;++i)bytes[i]=static_cast<unsigned char>(entropyCalls+i);return true;};
    auto installation=reports::installation(installDir,entropy);
    check(reports::installationValid(installation) && reports::installation(installDir,entropy)==installation && entropyCalls==1,"installation random ID is persisted and reused without reading hardware");
    auto otherDir=scenario();check(reports::installation(otherDir,entropy)!=installation,"another installation receives fresh random bytes");
    write(installDir+"blocker","file");check(reports::installation(installDir+"blocker/",entropy).empty(),"unpersisted ID is never used for upload");
    auto raw=J(O{{"version",J(string("0.3.3"))},{"build",J(string("33"))},{"hardware",J(string("Corona"))},{"screen",J(string("Player"))},{"error",J(string("player LOGIN_REQUIRED"))},{"log",J(string("Authorization: Bearer secret"))}}).serialize();
    J upgraded;check(parse(reports::sanitize(raw,installation),upgraded) && text(upgraded,"category")=="youtube" && text(upgraded,"error").find("youtube.auth_required")!=string::npos && text(upgraded,"installation_id")==installation,"legacy pending report gains category and ID while credentials are removed");
    check(reports::sanitize(reports::sanitize(raw,installation),installation)==reports::sanitize(raw,installation),"enriched queued body remains idempotent");
    for(auto item:std::vector<std::pair<string,string>>{{"DNS failed","network"},{"TLS certificate failed","network"},{"timeout","network"},{"sem rede","network"},{"player LOGIN_REQUIRED","youtube"},{"player UNPLAYABLE","youtube"},{"403 from video link","youtube"},{"startup.fail Missing fonts","startup"},{"unclean session","crash"},{"unrecognized diagnostic","other"}})
        check(reports::category(item.first,"")==item.second,"launcher supplies category from known cause");
    check(reports::cause("TLS failed","DNS diagnostic","network")=="network.tls","primary error outranks incidental network log");
    string dirty="open\n";
    string privateLog="peer 192.168.1.50 203.0.113.25 video dQw4w9WgXcQ x_y-Z012345\nNest: Sala da Maria\ncast name=Bedroom\nAuthorization: Bearer ABC.secret\nya29.secret\n{\"refresh_token\":\"private space\"}\naccess_token=secret\naccount: Example Name\nquery=private search\n";
    auto clean=reports::scrub(privateLog);
    check(reports::scrub(string("bad ")+char(0xc3)).find("bad ?")!=string::npos,"partial UTF-8 replaced before JSON");
    check(reports::screenName("Busca")=="Search","screen enum survives search-data scrubbing");
    check(reports::scrub(clean)==clean,"scrubbing is idempotent across retries");
    check(reports::scrub("peerX192.168.1.1z\n").find("192.168.1.1")==string::npos,"IPv4 adjacent to letters removed");
    for(auto [line,secret]:std::vector<std::pair<string,string>>{
        {"peer 2001:db8:abcd::42","2001:db8:abcd::42"}, {"peer [2001:db8::43]:8443","2001:db8::43"},
        {"peer fe80::42%eth0","fe80::42"}, {"peer livingroom.local:8009","livingroom.local"},
        {"SSID=Casa Secreta","Casa Secreta"}, {"wifi connected Minha Rede","Minha Rede"},
        {"SAPISID=short.private.value","short.private.value"}, {"__Secure-1PSID=short.private.value","short.private.value"},
        {"github_pat_short.private.value","short.private.value"}, {"path /home/Alice/.config/tube360","Alice"},
        {"path /Users/Alice/tube360","Alice"}, {"path C:\\Users\\Alice\\tube360","Alice"},
        {"https://youtube.com/watch?v=dQw4w9WgXcQ","dQw4w9WgXcQ"}, {"https://youtu.be/dQw4w9WgXcQ","dQw4w9WgXcQ"},
        {"https://youtube.com/shorts/dQw4w9WgXcQ","dQw4w9WgXcQ"}, {"id=dQw4w9WgXcQ","dQw4w9WgXcQ"},
        {"{\"id\":\"dQw4w9WgXcQ\"}","dQw4w9WgXcQ"}}) {
        clean=reports::scrub(line);
        if(clean.find(secret)!=string::npos)std::cerr<<"CANARY leaked: "<<line<<'\n';
        check(clean.find(secret)==string::npos,"adversarial privacy canary removed");
        check(reports::scrub(clean)==clean,"adversarial scrubbing remains idempotent");
    }
    for(auto secret:{"192.168.1.50","203.0.113.25","dQw4w9WgXcQ","x_y-Z012345","Sala da Maria","Bedroom","ABC.secret","ya29.secret","private space","access_token","Example Name","private search"})check(clean.find(secret)==string::npos,"secret absent from scrubbed text");
    string actual=read(argv[2],4*1024*1024);
    check(actual.size()>1000,"real anonymizable diagnostic fixture loaded");
    clean=reports::scrub(actual+privateLog);
    check(clean.find("dQw4w9WgXcQ")==string::npos && clean.find("Sala da Maria")==string::npos,"real log mixed with seeded privacy canaries");
    auto d=scenario();check(reports::make(d).empty(),"no session on fresh install means no upload");
    write(d+"session.txt","clean\n");check(reports::make(d).empty(),"normal exit means no upload");
    write(d+"session.txt",dirty);write(d+"tube360.log",privateLog+"navegacao: Home -> Player\n"+string(10000,'x')+"\n");
    write(d+"boot-app.log","boot app #0001 +1ms info main C runtime reached main; build=33 version=0.3.3\nboot app #0002 +2ms detect video.board model=Corona flags=50000000\nboot app #0003 +3ms error gpu.fence status=dead\n");
    J doc;auto bytes=reports::make(d);check(parse(bytes,doc),"dirty session creates JSON");
    check(text(doc,"version").find("0.3.3")==0 && text(doc,"build").find("33")==0,"previous running version/build from boot logs");
    check(text(doc,"hardware").find("Corona")!=string::npos && text(doc,"screen").find("Player")!=string::npos,"known hardware and last screen included");
    check(text(doc,"error").find("gpu.fence")!=string::npos && bytes.find("Sala da Maria")==string::npos,"error included and scrubbed before JSON");
    for(int i=0;i<3;++i) {
        auto dir=scenario();write(dir+"session.txt",dirty);
        if(i==0)write(dir+"no-error-reports.txt","");
        if(i==1)write(dir+"settings.txt","lang=pt\n error_reports = off\n");
        if(i==2){write(dir+"settings.txt","error_reports=on\n");write(dir+"no-error-reports.txt","");}
        write(dir+"error-report.json",bytes);
        Host host;launchApp(dir,"",host);check(host.posts==0 && host.launches==1 && !exists(dir+"error-report-installation.txt"),"opt-out overrides dirty and pending report without generating ID");
    }
    auto dir=scenario();write(dir+"session.txt",dirty);
    Host offline;offline.response={0,{},"offline"};launchApp(dir,"",offline);
    check(offline.posts==1 && offline.launches==1 && exists(dir+"error-report.json"),"offline never blocks launch; sanitized queue persists");
    auto pending=read(dir+"error-report.json");
    for(int i=0;i<24;++i) {
        write(dir+"session.txt",dirty);write(dir+"boot-app.log","new failure "+std::to_string(i));
        Host retry;retry.response={0,{},"offline"};launchApp(dir,"",retry);
        check(retry.posts==1 && retry.launches==1 && read(dir+"error-report.json")==pending,"repeated offline starts retain one bounded pending report");
    }
    for(auto rejected:std::vector<yt::Response>{{200,"{\"code\":\"T360-"+string(5000,'A')+"\"}",{}},
        {200,"{\"code\":\"T360-ABCD\\n\"}",{}},{200,"{\"code\":\"T360-ABCD\\u001b\"}",{}},
        {200,"{\"code\":\"T360-ABCDé\"}",{}},{200,"{\"code\":\"T360-@ABCD\"}",{}},
        {200,"{\"code\":\"T360-ABCD\"}","timeout"},{200,"garbage",{}},{503,"{\"code\":\"T360-ABCD\"}",{}}}) {
        Host badReply;badReply.response=rejected;launchApp(dir,"",badReply);
        check(badReply.shown.empty() && badReply.launches==1 && read(dir+"error-report.json")==pending,"hostile response never reaches display or loses offline queue");
    }
    write(dir+"session.txt","clean\n");
    Host online;launchApp(dir,"",online);
    check(online.posts==1 && online.body==pending && !exists(dir+"error-report.json"),"offline report survives later clean session and is removed on receipt");
    check(online.shown=="T360-7K2Q" && online.duration<=4000 && online.duration>=3500,"valid receipt shown for about four seconds");
    write(dir+"session.txt","clean\n");Host again;launchApp(dir,"",again);check(again.posts==0,"accepted report not repeated");
    write(dir+"error-report.json",pending);write(dir+"error-report.sent",sha256(pending));
    Host torn;launchApp(dir,"",torn);check(torn.posts==0 && !exists(dir+"error-report.json"),"receipt hash recovers interrupted cleanup without resending");
    check(reports::code({200,"{\"code\":\"T360-<script>\"}",{}}).empty() && reports::code({500,"{\"code\":\"T360-7K2Q\"}",{}}).empty(),"invalid/error response never displayed");
    dir=scenario();write(dir+"session.txt",dirty);
    Host bad;bad.response={200,"not json",{}};launchApp(dir,"",bad);check(exists(dir+"error-report.json") && bad.shown.empty(),"malformed receipt stays pending");
    dir=scenario();write(dir+"tube360.log","old crash should not appear\n");boot_session_begin(dir.c_str(),"fixture");
    FILE* f=fopen((dir+"tube360.log").c_str(),"a");fputs("new checkpoint\n",f);fclose(f);
    bytes=reports::make(dir);check(bytes.find("old crash")==string::npos && bytes.find("new checkpoint")!=string::npos,"session offsets exclude earlier sessions");
    write(dir+"tube360.log",string(70*1024,'x')+"Bearer tail-secret\nlast checkpoint\n");
    bytes=reports::make(dir);check(bytes.size()<=65536 && bytes.find("tail-secret")==string::npos && bytes.find("last checkpoint")!=string::npos,"bounded tail drops partial secret lines");
    dir=scenario();write(dir+"tube360.log","old player data\n");
    boot_session_begin(dir.c_str(),"launcher=1\nversion=0.3.2 build=32");
    check(reports::make(dir).find("old player data")==string::npos,"handoff failure excludes previous app log");
    boot_session_version(33,"0.3.3");
    write(dir+"tube360.log","navegacao: Home -> Player\nnew log longer than the previous session\n");
    check(parse(reports::make(dir),doc) && text(doc,"screen").find("Player")!=string::npos && text(doc,"version").find("0.3.3")==0,"SDK overwrite resets log offset and running app replaces launcher version");
    dir=scenario();write(dir+"session.txt",dirty);write(dir+"startup.log","startup: Tube360 could not start\nMissing fonts\ninter-400.ttf\nCopy the folder\n");
    check(parse(reports::make(dir),doc) && text(doc,"error").find("Missing fonts")!=string::npos,"launcher startup failure supplies a useful error signature");
    dir=scenario();write(dir+"session.txt","");check(!reports::make(dir).empty(),"torn empty marker is dirty");
    for(int mode=0;mode<2;++mode) {
        dir=scenario();pid_t pid=fork();check(pid>=0,"fork child for abrupt exit");
        if(!pid){boot_open(dir.c_str(),"app");boot_event("begin","fixture.stop","checkpoint durable");if(!mode)_Exit(23);for(;;)pause();}
        if(mode){for(int i=0;i<200 && read(dir+"boot-app.log").find("fixture.stop")==string::npos;++i)usleep(10000);kill(pid,SIGKILL);}
        int status=0;waitpid(pid,&status,0);
        check(read(dir+"session.txt").rfind("open\n",0)==0 && !reports::make(dir).empty(),"dirty marker survives _Exit and SIGKILL");
    }
    dir=scenario();boot_open(dir.c_str(),"app");boot_close();boot_session_clean();check(read(dir+"session.txt").rfind("clean\n",0)==0,"normal shutdown closes marker");
    std::cout<<checks<<" error-report checks passed (real log + privacy canaries, launcher policy, durable sessions)\n";
}
