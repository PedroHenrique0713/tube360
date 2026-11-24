#include "cast/discovery.hpp"
#include "nest.hpp"
#include "picojson.h"
#include <arpa/inet.h>
#include <cassert>
#include <iostream>
#include <thread>

using J=picojson::value;
using O=picojson::object;
J json(const nest::Config& c) { return J(O{{"host",J(c.host)},{"name",J(c.name)},{"uuid",J(c.uuid)},{"model",J(c.model)}}); }
int main(int argc,char** argv) {
    SDL_Init(0);
    if (argc<2) return 2;
    std::string command=argv[1]; std::atomic_bool cancel{false};
    uint64_t start=SDL_GetTicks();
    if (command=="plan" && argc==4) {
        in_addr address{}, mask{};
        if (inet_pton(AF_INET,argv[2],&address)!=1 || inet_pton(AF_INET,argv[3],&mask)!=1) return 2;
        picojson::array hosts;
        for (const auto& host:nest::addresses({ntohl(address.s_addr),ntohl(mask.s_addr)})) hosts.push_back(J(host));
        std::cout<<J(O{{"hosts",J(hosts)}}).serialize()<<std::endl;
    } else if (command=="scan" || command=="cancel") {
        nest::Network network; if (!nest::localNetwork(network)) return 3;
        std::thread timer;
        if (command=="cancel") timer=std::thread([&] { SDL_Delay(70); cancel=true; });
        auto result=nest::scan(network,cancel);
        if (timer.joinable()) timer.join();
        picojson::array devices; for (const auto& c:result.devices) devices.push_back(json(c));
        std::cout<<J(O{{"devices",J(devices)},{"state",J(double(result.state))},{"ms",J(double(SDL_GetTicks()-start))}}).serialize()<<std::endl;
    } else if (command=="identify" && argc==3) {
        nest::Config c; c.host=argv[2]; bool ok=nest::identify(c,cancel);
        auto out=json(c).get<O>(); out["ok"]=J(ok); std::cout<<J(out).serialize()<<std::endl;
    } else if (command=="recover" && argc==3) {
        nest::Speaker speaker; speaker.init(std::string(argv[2])+"/"); nest::SearchResult view;
        while (SDL_GetTicks()-start<7000) {
            speaker.finder().poll(view);
            if (view.state!=nest::SearchState::Idle && view.state!=nest::SearchState::Searching) break;
            SDL_Delay(10);
        }
        auto out=json(speaker.settings()).get<O>(); out["state"]=J(double(view.state)); out["ms"]=J(double(SDL_GetTicks()-start));
        speaker.shutdown(); std::cout<<J(out).serialize()<<std::endl;
    } else if (command=="lifecycle" && argc==3) {
        std::string dir=std::string(argv[2])+"/";
        nest::Discovery finder; finder.init(dir); nest::SearchResult view;
        while (SDL_GetTicks()-start<1000) {
            finder.poll(view); if (view.state==nest::SearchState::Searching) break; SDL_Delay(1);
        }
        finder.search();
        while (SDL_GetTicks()-start<6000) {
            finder.poll(view);
            if (view.state==nest::SearchState::Done && view.devices.size()==2) break;
            SDL_Delay(10);
        }
        assert(view.state==nest::SearchState::Done && view.devices.size()==2);
        assert(finder.choose(view.devices.back())); assert(finder.ready());
        nest::Config chosen; assert(chosen.load(dir) && chosen.uuid==view.devices.back().uuid);
        assert(finder.choose(nest::Config{})); assert(!finder.configured() && !finder.ready());
        assert(!chosen.load(dir));
        finder.search(); finder.cancel();
        finder.poll(view); assert(view.state==nest::SearchState::Cancelled);
        finder.search();
        uint64_t retry=SDL_GetTicks();
        while (SDL_GetTicks()-retry<6000) {
            finder.poll(view); if (view.state==nest::SearchState::Done && view.devices.size()==2) break; SDL_Delay(10);
        }
        assert(view.state==nest::SearchState::Done && view.devices.size()==2);
        finder.stop();
        std::cout<<"lifecycle OK"<<std::endl;
    } else if (command=="contracts" && argc==3) {
        in_addr a{},m{}; inet_pton(AF_INET,"192.0.2.21",&a); inet_pton(AF_INET,"255.255.255.240",&m);
        auto list=nest::addresses({ntohl(a.s_addr),ntohl(m.s_addr)});
        assert(list.size()==13 && list.front()=="192.0.2.17" && list.back()=="192.0.2.30");
        inet_pton(AF_INET,"255.255.0.0",&m); list=nest::addresses({ntohl(a.s_addr),ntohl(m.s_addr)});
        assert(list.size()==253 && list.front()=="192.0.2.1" && list.back()=="192.0.2.254");
        assert(nest::addresses({ntohl(a.s_addr),0xff00ff00}).empty());
        nest::Config saved; saved.name="Sala"; saved.uuid="11111111111111111111111111111111";
        nest::Config first=saved,second=saved; second.uuid="22222222222222222222222222222222";
        assert(nest::matching(saved,{second,first})==1);
        assert(nest::matching(saved,{second})==-1);
        saved.uuid.clear(); assert(nest::matching(saved,{second,first})==-1);
        assert(nest::matching(saved,{second})==0);
        std::string dir=std::string(argv[2])+"/";
        saved.host="127.77.24.2"; saved.model="Google Nest Mini";
        assert(saved.save(dir)); nest::Config loaded; assert(loaded.load(dir) && loaded.host==saved.host && loaded.name==saved.name && loaded.model==saved.model);
        assert(!saved.save(dir+"missing/"));
        assert(nest::Config{}.save(dir)); assert(!loaded.load(dir) && loaded.host.empty());
        std::cout<<"contracts OK"<<std::endl;
    } else return 2;
    SDL_Quit();
}
