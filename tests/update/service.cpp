#include "update/service.hpp"
#include "update/public_key.hpp"
#include <cstdlib>
#include <iostream>
using namespace updates;
static void check(bool ok,const char* why){if(!ok){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    string dir=string(argv[1])+"/",mode=argv[2];
    SDL_Init(SDL_INIT_EVENTS);
    {
        Service service(dir);Status status;unsigned rev=0;
        auto wait=[&](auto predicate,unsigned timeout=12000) {
            auto until=SDL_GetTicks()+timeout;
            do {service.tick(true,true);rev=service.poll(status,rev);if(predicate())return true;SDL_Delay(10);}while(SDL_GetTicks()<until);
            std::cerr<<"state="<<int(status.state)<<" error="<<status.error<<'\n';return false;
        };
        auto idle=[&]{return (status.state==State::IDLE || status.state==State::CHECK_FAILED) && rev>1;};
        if(mode=="reject") {check(wait(idle),"untrusted manifest not offered");return 0;}
        check(wait([&]{return status.state==State::AVAILABLE;}),"signed newer build offered");
        check(status.app.build==6000 && status.app.size>1024*1024,"signed version and byte size exposed");
        if(mode=="cancel") {
            service.requestDownload();
            check(wait([&]{return status.state==State::DOWNLOADING;}),"worker starts download");
            SDL_Delay(150);auto start=SDL_GetTicks();service.cancelDownload();
            check(wait([&]{return status.state==State::AVAILABLE;},2000),"B cancels in-flight transfer");
            check(!exists(dir+"tube360.next.xex.part") && !exists(dir+"update-app-state.json"),"cancel removes partial file without journal");
            std::cout<<"cancel latency "<<SDL_GetTicks()-start<<" ms\n";
            return 0;
        }
        service.requestDownload();
        if(mode=="error" || mode=="legacy" || mode=="corrupt") {
            check(wait([&]{return status.state==State::ERROR;}),"bad transfer/state refused");
            if(mode=="legacy")check(status.error=="setup","legacy installation gets migration hint");
            if(mode=="corrupt")check(status.error=="state","corrupt journal blocks staging");
            check(read(dir+"tube360.xex")=="XEX2original" || mode=="legacy","installed app untouched");
            check(!exists(dir+"tube360.next.xex") && !exists(dir+"tube360.next.xex.part"),"bad payload not staged");
            return 0;
        }
        check(wait([&]{return status.state==State::READY;}),"real Range HTTP, SHA256 and signature stage candidate");
        check(read(dir+"tube360.xex")=="XEX2original","staging never replaces running app");
        service.requestCheck();
        SDL_Delay(250);rev=service.poll(status,rev);
        check(status.state==State::READY,"checking again keeps staged candidate ready");
    }
    {
        Service restarted(dir);Status status;unsigned rev=0;auto until=SDL_GetTicks()+5000;
        do{restarted.tick(true,true);rev=restarted.poll(status,rev);if(status.state==State::READY)break;SDL_Delay(10);}while(SDL_GetTicks()<until);
        check(status.state==State::READY,"staged update survives app restart");
    }
    AppUpdate launcher(dir,publicKeyHex);string error;
    check(launcher.prepare(error)==launcher.executable(),"launcher promotes downloaded candidate");
    check(read(dir+"tube360.previous.xex")=="XEX2original","launcher keeps original backup");
    AppUpdate unconfirmed(dir,publicKeyHex);
    check(unconfirmed.prepare(error)==launcher.executable() && read(launcher.executable())=="XEX2original","next boot rolls unconfirmed candidate back");
    check(unconfirmed.highWater()==6000,"rollback persists rejected build");
    SDL_Quit();std::cout<<"Service HTTP -> staged -> launcher -> rollback passed\n";
}
