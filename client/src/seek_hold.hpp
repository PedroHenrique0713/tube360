#pragma once
#include <algorithm>
#include <cstdint>

namespace playback {
struct SeekHold {
  bool active=false, wasPaused=false,blocked=false;
  double target=0;
  int direction=0;
  uint64_t began=0,previous=0;
  void cancel(bool untilRelease=false){active=false;direction=0;blocked=untilRelease;}
  // Um único seek na soltura. O preview avança por tempo, independente do fps da interface.
  bool update(uint64_t now,int input,double position,double duration,bool paused) {
    if(blocked){if(!input)blocked=false;return false;}
    double end=duration>0?std::max(0.0,duration-3):1e9;
    if(!input){bool commit=active;cancel();return commit;}
    if(!active){active=true;wasPaused=paused;target=position;direction=0;}
    if(input!=direction){direction=input;began=previous=now;target=std::clamp(target+input*10,0.0,end);}
    // Integra exatamente as faixas de aceleração, inclusive com frames espaçados.
    auto distance=[](uint64_t ms){double t=ms/1000.0;return std::max(0.0,std::min(t,2.0)-.3)*10+std::clamp(t-2,0.0,3.0)*30+std::max(0.0,t-5)*120;};
    target=std::clamp(target+input*(distance(now-began)-distance(previous-began)),0.0,end);
    previous=now;return false;
  }
};
}
