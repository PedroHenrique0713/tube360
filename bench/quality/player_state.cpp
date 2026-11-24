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

bool castSetupKeepsPicture(bool playPending) {
    player::Player p;
    p.mu=SDL_CreateMutex(); p.cv=SDL_CreateCondition(); p.nmu=SDL_CreateMutex();
    p.state=player::PLAYING; p.speakerRequested=true; p.output=player::Player::PRIMING;
    p.firstPts=2; p.screenSec=2; p.tickMs=SDL_GetTicksNS()/1e6-40; p.wallStart=SDL_GetTicks()-40;
    p.audible=!playPending;
    p.cueMs=1000; p.armed=playPending; p.mutedForCast=playPending;
    player::Frame frame; frame.pts=2.033; frame.w=1280; frame.h=720;
    p.frames.push_back(&frame);
    auto* shown=p.take();
    bool advanced=shown==&frame && p.lastPos>=2.033 && p.state==player::PLAYING;
    SDL_DestroyMutex(p.mu); SDL_DestroyMutex(p.nmu); SDL_DestroyCondition(p.cv);
    return advanced;
}

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
    SDL_DestroyMutex(p.mu); SDL_DestroyMutex(p.nmu); SDL_DestroyCondition(p.cv);
    bool setupMoves=castSetupKeepsPicture(false), playMoves=castSetupKeepsPicture(true);
    SDL_Quit();
    if (!setupMoves || !playMoves) {
        std::fprintf(stderr,"FAIL Cast waiting held picture: preload=%d play=%d\n",setupMoves,playMoves); return 1;
    }
    if (!ok || !frozen) {
        std::fprintf(stderr,"FAIL paused Cast: frame=%d state=%d pause=%d nest=%d tracking=%d audible=%d shown=%llu pos=%.3f first=%.3f ready=%d\n",
            first==&frames[0],int(p.state.load()),int(p.paused.load()),int(p.nest),p.tracking,int(p.audible.load()),
            (unsigned long long)p.shownFrames,p.lastPos,p.firstPts,int(p.pictureReady.load())); return 1;
    }
    std::puts("PASS paused Cast displays its first frame, keeps Nest, position and silence");
    std::puts("PASS picture advances while Cast preload or PLAY confirmation is pending");
}
