// Tela de emergência independente de Vulkan, das fontes externas e da inicialização do SDL.
#pragma once
#include "i18n.hpp"
#include "boot_log.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#ifdef __XSDK__
#include <xsdk/display.h>
#include <xsdk/gpu.h>
#include <xsdk/present.h>
#include <xsdk/text.h>
#include <xsdk/thread.h>
#include <xsdk/log.h>
#else
#include <SDL3/SDL.h>
#include "startup_font.hpp"
#endif

namespace startup {
inline std::string directory;
inline i18n::Lang language=i18n::Lang::EN;
inline void (*releaseGraphics)()=nullptr;
inline void configure(const std::string& dir) {
  directory=dir;
  if (FILE* f=std::fopen((dir+"settings.txt").c_str(),"r")) {
    char line[128],value[8];
    while (std::fgets(line,sizeof(line),f)) if (std::sscanf(line,"lang=%7s",value)==1) language=i18n::language(value);
    std::fclose(f);
  }
}
inline void log(const std::string& text) {
  if (FILE* f=std::fopen((directory+"startup.log").c_str(),"a")) {
    std::fprintf(f,"startup: %s\n",text.c_str()); std::fflush(f); std::fclose(f);
  }
#ifdef __XSDK__
  xsdk_log_write("startup: %s",text.c_str());
#else
  std::fprintf(stderr,"startup: %s\n",text.c_str());
#endif
}
inline std::string missingFonts(const std::string& dir) {
  std::string result;
  for (auto name : {"inter-400.ttf","inter-600.ttf","inter-700.ttf","inter-300.ttf"}) {
    boot_event("begin","fonts.header","file=%s",name);
    FILE* f=std::fopen((dir+name).c_str(),"rb");
    unsigned char header[4]{};
    bool valid=f && std::fread(header,1,4,f)==4 &&
        ((!header[0] && header[1]==1 && !header[2] && !header[3]) || !std::memcmp(header,"OTTO",4));
    if(f)std::fclose(f);
    boot_event(valid?"end":"error","fonts.header","file=%s valid=%d errno=%d",name,valid,valid?0:errno);
    if(!valid) {if(!result.empty())result+=", ";result+=name;}
  }
  return result;
}
inline bool writable() {
  FILE* f=std::fopen((directory+".startup-write-test").c_str(),"wb");
  if(!f)return false;
  bool ok=std::fputc('1',f)!=EOF; if(std::fclose(f))ok=false;
  std::remove((directory+".startup-write-test").c_str()); return ok;
}
// Mesma quebra UTF-8 para o texto embutido do xsdk e a prova desktop.
inline std::string wrap(const std::string& text,size_t columns) {
  std::string out;size_t line=0,lastSpace=std::string::npos;
  for(size_t i=0;i<text.size();) {
    unsigned char c=text[i];
    if(c=='\n'){out+='\n';++i;line=0;lastSpace=std::string::npos;continue;}
    if(line>=columns) {
      if(lastSpace!=std::string::npos) {
        out[lastSpace]='\n';line=0;
        for(size_t j=lastSpace+1;j<out.size();++j)if((uint8_t(out[j])&0xc0)!=0x80)++line;
        lastSpace=std::string::npos;
      } else {out+='\n';line=0;}
    }
    if(c==' ')lastSpace=out.size();
    size_t bytes=c<0x80?1:c<0xe0?2:c<0xf0?3:4;
    bytes=std::min(bytes,text.size()-i);out.append(text,i,bytes);i+=bytes;++line;
  }
  return out;
}
[[noreturn]] inline int fail(i18n::Key reason,const std::string& detail={},i18n::Key action=i18n::Key::StartupRetry) {
  auto tr=[](i18n::Key k){return i18n::get(language,k);};
  boot_event("error","startup.fail","%s detail=%s",tr(reason),detail.c_str());
  log(std::string(tr(i18n::Key::StartupTitle))+"\n"+tr(reason)+"\n"+detail+"\n"+tr(action));
  size_t limit=std::min(size_t(80),detail.size());
  while(limit<detail.size() && limit && (uint8_t(detail[limit])&0xc0)==0x80)--limit;
  std::string body=std::string(tr(reason))+"\n"+detail.substr(0,limit)+(limit<detail.size()?"...":"")+"\n\n"+tr(action);
  if(releaseGraphics){auto release=releaseGraphics;releaseGraphics=nullptr;release();}
#ifdef __XSDK__
  // O driver pode ter falhado antes de assumir a GPU. Cada recurso só é fechado se abriu.
  boot_event("begin","error-screen.gpu","xsdk_gpu_open");
  auto gpuStatus=xsdk_gpu_open();
  bool gpu=NT_SUCCESS(gpuStatus),present=false,text=false;
  boot_event(gpu?"end":"error","error-screen.gpu","status=%08x",unsigned(gpuStatus));
  uint32_t w=0,h=0;
  if(gpu)present=NT_SUCCESS(xsdk_present_open(XSDK_DISPLAY_720P,2,1,&w,&h));
  if(present)text=NT_SUCCESS(xsdk_text_open());
  uint32_t scale=w>=1000?2:1;
  auto lines=wrap(body,std::max(30u,(w>80?w-80:560)/(10*scale)));
  bool released=false;
  for(;;) {
    if(text) {
      uint32_t address=0;
      if(NT_SUCCESS(xsdk_present_begin(&address,1000))) {
        xsdk_gpu_resolve(address,w,h,0xFF111118);xsdk_gpu_set_target(w,h);
        xsdk_text_draw(40,40,scale,0xFFFF5A52,tr(i18n::Key::StartupTitle));
        xsdk_text_draw(40,120,scale,0xFFFFFFFF,lines.c_str());
        std::string exit=std::string("[ B ]  ")+tr(i18n::Key::StartupExit);
        xsdk_text_draw(40,h-80,scale,0xFFFF5A52,exit.c_str());
        xsdk_gpu_resolve(address,w,h,0xFF111118);xsdk_present_end();
        xsdk_gpu_wait(xsdk_gpu_fence(),1000);
      }
    }
    bool down=false;
    for(uint32_t user=0;user<4;++user){INPUT_STATE state{};if(!XamInputGetState(user,0,&state))down|=(state.gamepad.buttons&INPUT_GAMEPAD_B)!=0;}
    if(!down)released=true;
    if(down && released)break;
    xsdk_thread_sleep(16);
  }
  if(text)xsdk_text_close();
  if(present)xsdk_present_close();
  if(gpu)xsdk_gpu_close();
#else
  // Software + fonte bitmap embutida: funciona mesmo com Vulkan e todos os TTF ausentes.
  SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_GAMEPAD);
  SDL_Window* win=SDL_CreateWindow("Tube360",1280,720,0);
  SDL_Renderer* renderer=win?SDL_CreateRenderer(win,"software"):nullptr;
  if(!renderer){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,tr(i18n::Key::StartupTitle),body.c_str(),win);std::_Exit(1);}
  auto draw=[&](const std::string& value,int x,int y,int scale){
    int left=x;
    for(size_t i=0;i<value.size();) {
      uint32_t point=uint8_t(value[i++]);
      if(point>=0xc0){unsigned tail=point<0xe0?1:point<0xf0?2:3;point&=tail==1?31:tail==2?15:7;while(tail-- && i<value.size())point=(point<<6)|(uint8_t(value[i++])&63);}
      if(point=='\n'){x=left;y+=24*scale;continue;}
      auto glyph=std::lower_bound(std::begin(font),std::end(font),point,[](const Bitmap& b,uint32_t p){return b.point<p;});
      if(glyph!=std::end(font) && glyph->point==point)for(int row=0;row<20;++row)for(int col=0;col<10;++col)if(glyph->rows[row]&(0x8000>>col)) {
        SDL_FRect pixel{float(x+col*scale),float(y+row*scale),float(scale),float(scale)};SDL_RenderFillRect(renderer,&pixel);
      }
      x+=10*scale;
    }
  };
  SDL_SetRenderDrawColor(renderer,17,17,24,255);SDL_RenderClear(renderer);
  SDL_SetRenderDrawColor(renderer,255,90,82,255);draw(tr(i18n::Key::StartupTitle),40,40,2);
  SDL_SetRenderDrawColor(renderer,255,255,255,255);draw(wrap(body,58),40,120,2);
  SDL_SetRenderDrawColor(renderer,255,90,82,255);draw(std::string("[ B ]  ")+tr(i18n::Key::StartupExit),40,640,2);
  if(const char* path=std::getenv("XT_SHOT")) {
    if(SDL_Surface* surface=SDL_RenderReadPixels(renderer,nullptr)) {
      SDL_Surface* rgb=SDL_ConvertSurface(surface,SDL_PIXELFORMAT_RGB24);
      if(rgb){if(FILE* f=std::fopen(path,"wb")){std::fprintf(f,"P6\n%d %d\n255\n",rgb->w,rgb->h);for(int y=0;y<rgb->h;++y)std::fwrite(static_cast<char*>(rgb->pixels)+y*rgb->pitch,1,rgb->w*3,f);std::fclose(f);}SDL_DestroySurface(rgb);}
      SDL_DestroySurface(surface);
    }
  }
  SDL_RenderPresent(renderer);
  std::vector<SDL_Gamepad*> pads;
  int count=0;SDL_JoystickID* ids=SDL_GetGamepads(&count);for(int i=0;i<count;++i)if(auto* p=SDL_OpenGamepad(ids[i]))pads.push_back(p);SDL_free(ids);
  bool done=false,released=false;
  while(!done) {
    SDL_Event e;while(SDL_PollEvent(&e)) {
      if(e.type==SDL_EVENT_QUIT || (e.type==SDL_EVENT_KEY_DOWN && e.key.key==SDLK_ESCAPE))done=true;
      if(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button==SDL_GAMEPAD_BUTTON_EAST && released)done=true;
      if(e.type==SDL_EVENT_GAMEPAD_ADDED)if(auto* p=SDL_OpenGamepad(e.gdevice.which))pads.push_back(p);
    }
    bool down=false;for(auto* p:pads)down|=SDL_GetGamepadButton(p,SDL_GAMEPAD_BUTTON_EAST);
    if(!down)released=true;
    // Saída explícita da bancada; sem ela, a tela espera pelo controle.
    if(const char* keys=std::getenv("XT_KEYS"))if(std::strchr(keys,'B') && SDL_GetTicks()>300)done=true;
    SDL_Delay(16);
  }
  for(auto* p:pads)SDL_CloseGamepad(p);
  SDL_DestroyRenderer(renderer);SDL_DestroyWindow(win);
#endif
  log("B: exit");
  std::_Exit(1);
}
}
