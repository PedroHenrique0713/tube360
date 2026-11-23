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
    SDL_DestroyMutex(p.mu); SDL_DestroyMutex(p.nmu); SDL_DestroyCondition(p.cv); SDL_Quit();
    if (!ok || !frozen) {
        std::fprintf(stderr,"FAIL paused Cast: frame=%d state=%d pause=%d nest=%d tracking=%d audible=%d shown=%llu pos=%.3f first=%.3f ready=%d\n",
            first==&frames[0],int(p.state.load()),int(p.paused.load()),int(p.nest),p.tracking,int(p.audible.load()),
            (unsigned long long)p.shownFrames,p.lastPos,p.firstPts,int(p.pictureReady.load())); return 1;
    }
    std::puts("PASS paused Cast displays its first frame, keeps Nest, position and silence");
}
