#include "update/service.hpp"
#include "update/public_key.hpp"
#include <iostream>
using namespace updates;
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    string dir=string(argv[1])+"/",mode=argv[2];
    if(!SDL_Init(SDL_INIT_EVENTS))return 3;
    if(mode=="transport") {
        auto net=yt::https();yt::Request r;
        r.url="https://raw.githubusercontent.com/PedroHenrique0713/tube360/rules/rules.json";
        r.update=true;r.maxBytes=maxDocument;
        auto res=net->send(r);
        std::cout<<J(O{{"status",J(double(res.status))},{"error",J(res.error)}}).serialize()<<'\n';
        return 0;
    }
    Status status;unsigned revision=0;
    {
        Service service(dir);
        auto begin=SDL_GetTicks();bool requested=false;
        do {
            service.tick(true,true);revision=service.poll(status,revision);
            if(mode=="download" && status.state==State::AVAILABLE && !requested) {
                requested=true;service.requestDownload();
            }
            if(status.state==State::READY || status.state==State::ERROR)break;
            if(mode=="check" && status.state==State::AVAILABLE)break;
            SDL_Delay(10);
        }while(SDL_GetTicks()-begin<(mode=="reject"?3000:8000));
    }
    std::cout<<J(O{{"rules",J(double(rules()->version))},
        {"available",J(status.state==State::AVAILABLE)},{"ready",J(status.state==State::READY)},
        {"error",J(status.error)},{"partial",J(exists(dir+"tube360.next.xex.part"))},
        {"installed",J(read(dir+"tube360.xex"))}}).serialize()<<'\n';
    SDL_Quit();
}
