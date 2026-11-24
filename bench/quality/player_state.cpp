// O relógio Cast pausado precisa liberar a primeira imagem sem iniciar PCM.
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdio>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#undef PL_MPEG_IMPLEMENTATION
#ifndef TUBE360_PLAYER_HEADER
#define TUBE360_PLAYER_HEADER "player.hpp"
#endif
#define LOG(...) SDL_Log(__VA_ARGS__)
#include TUBE360_PLAYER_HEADER
#include <cstdio>

int main() {
    SDL_Init(0);
    player::Player p;
    p.mu=SDL_CreateMutex(); p.cv=SDL_CreateCondition(); p.nmu=SDL_CreateMutex();
    p.state=player::BUFFERING; p.direct=p.original=p.speakerRequested=p.paused=true;
    p.audible=false; p.output=player::Player::CAST; p.startAt=8; p.nestState=2; p.nestPos=.5;
    p.nestStampMs=SDL_GetTicksNS()/1e6;
    player::Frame frames[6];
    for (auto& f : frames) { f.pts=.5; f.w=1920; f.h=1080; f.sourceFps=30; p.frames.push_back(&f); }
    auto* first=p.take();
    bool ok=first==&frames[0] && p.state==player::PLAYING && p.paused && p.nest && p.tracking &&
            !p.audible && p.shownFrames==1 && p.lastPos==8.5;
    bool frozen=p.take()==nullptr && p.lastPos==8.5 && p.shownFrames==1;
    p.paused=false; p.retryConverted=true; p.directReserveReady=false;
    net::serverEnabled=true;
    bool unavailable=p.take()==nullptr && p.state==player::FAILED && !p.netThread && !p.decThread;
    p.state=player::BUFFERING; p.retryConverted=false;
    net::serverEnabled=false;
    bool pressure=!p.directFailed(false) && !p.retryConverted && p.state==player::BUFFERING;
    p.directFailed();
    bool disabled=p.retryConverted && !p.directReserveReady && p.take()==nullptr && p.state==player::FAILED;
    bool probes=true;
    for (int status : {503,200}) {
        int listener=::socket(AF_INET,SOCK_STREAM,0);
        sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        socklen_t size=sizeof(address);
        if (listener<0 || ::bind(listener,(sockaddr*)&address,sizeof(address)) || ::listen(listener,1) ||
            ::getsockname(listener,(sockaddr*)&address,&size)) return 2;
        struct Reply { int listener,status; } reply{listener,status};
        auto* thread=SDL_CreateThread([](void* data) {
            auto& reply=*static_cast<Reply*>(data); int fd=::accept(reply.listener,nullptr,nullptr);
            char request[2048]{}; int got=fd<0 ? -1 : int(::recv(fd,request,sizeof(request)-1,0));
            bool correct=got>0 && std::string(request).find("GET /api/remote/state HTTP/")==0;
            if (correct) net::sendAll(fd,"HTTP/1.0 "+std::to_string(reply.status)+" fixture\r\nContent-Length: 14\r\n\r\n{\"playing\":\"\"}");
            if (fd>=0) ::close(fd);
            return correct ? 0 : 1;
        },"reserve-fixture",&reply);
        if (!thread) return 2;
        net::serverEnabled=true; p.srv={"127.0.0.1",ntohs(address.sin_port)};
        p.state=player::BUFFERING; p.retryConverted=false;
        bool ready=p.directFailed(false);
        int result=0; SDL_WaitThread(thread,&result); ::close(listener);
        probes=probes && !result && ready==(status==200) && p.retryConverted==(status==200);
    }
    SDL_DestroyMutex(p.mu); SDL_DestroyMutex(p.nmu); SDL_DestroyCondition(p.cv); SDL_Quit();
    if (!ok || !frozen || !unavailable || !disabled || !pressure || !probes) {
        std::fprintf(stderr,"FAIL paused Cast: frame=%d state=%d pause=%d nest=%d tracking=%d audible=%d shown=%llu pos=%.3f first=%.3f ready=%d\n",
            first==&frames[0],int(p.state.load()),int(p.paused.load()),int(p.nest),p.tracking,int(p.audible.load()),
            (unsigned long long)p.shownFrames,p.lastPos,p.firstPts,int(p.pictureReady.load())); return 1;
    }
    std::puts("PASS paused Cast preserves frame/clock; disabled or unavailable server never starts fallback");
}
