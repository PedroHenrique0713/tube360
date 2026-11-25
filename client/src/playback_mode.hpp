#pragma once
#include <array>
#include <cstring>

namespace playback {
enum Mode { AUTO, FAST, DETAIL, FIXED480, FIXED360, FIXED240, FIXED144, COUNT };
inline bool manual(int mode){return mode>=FIXED480 && mode<COUNT;}
inline int fixedHeight(int mode){return mode==FIXED480?480:mode==FIXED360?360:mode==FIXED240?240:mode==FIXED144?144:0;}
inline int ceiling(int mode) { return manual(mode)?fixedHeight(mode):mode == FAST ? 720 : 1080; }
inline int rate(int mode) { return mode == DETAIL || manual(mode) ? 30 : 60; }
inline const char* modeName(int mode){static const char* names[]={"auto","720p60","1080p30","480p","360p","240p","144p"};return mode>=0 && mode<COUNT?names[mode]:names[AUTO];}
inline int modeFromName(const char* name){for(int i=0;i<COUNT;++i)if(!std::strcmp(name,modeName(i)))return i;return AUTO;}
inline unsigned offeredMode(int height,double fps){
  unsigned out=1u<<AUTO;
  if(height==720 && fps>30.1)out|=1u<<FAST;
  if(height==1080 && fps>0 && fps<=30.1)out|=1u<<DETAIL;
  for(int i=FIXED480;i<COUNT;++i)if(fixedHeight(i)==height)out|=1u<<i;
  return out;
}
struct QualityChoices {
  std::array<int,COUNT> modes{};int count=0;
  explicit QualityChoices(unsigned mask=(1u<<COUNT)-1){assign(mask);}
  void assign(unsigned mask){count=0;mask|=1u<<AUTO;for(int i=0;i<COUNT;++i)if(mask&(1u<<i))modes[count++]=i;}
  int index(int mode)const{for(int i=0;i<count;++i)if(modes[i]==mode)return i;return 0;}
  int cycle(int mode,int direction)const{return modes[(index(mode)+direction+count)%count];}
};
}
