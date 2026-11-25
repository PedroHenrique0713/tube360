#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#undef STB_TRUETYPE_IMPLEMENTATION
#include "gfx_video_memory.hpp"
#include <cassert>

int main(int argc,char** argv) {
  assert(argc==3);
  const char* assets=std::getenv("XT_ASSETS");assert(assets);
  boot_open(assets,"mapped-test");tube360_video_policy_init();
  assert(SDL_Init(SDL_INIT_VIDEO));
  gfx::Gfx g;g.readback=true;assert(gfx::init(g,1280,720,"board simulation"));
  gfx::DecodePool pool(g);int alignment[]={32,32,32};
  auto surface=pool.acquire(1280,720,1280,720,alignment);
  bool mapped=surface!=nullptr;
  assert(mapped==(!std::strcmp(argv[1],"mapped")));
  gfx::Video video;g.video=&video;gfx::begin(g);
  if(surface) {
    for(int i=0;i<3;++i)for(int y=0;y<(i?360:720);++y)
      std::memset(surface->data[i]+y*surface->stride[i],i?128:100,i?640:1280);
    surface->publish();gfx::videoUse(g,video,surface,0);
  } else {
    std::vector<uint8_t> pixels(1280*720*3/2,128);
    std::fill(pixels.begin(),pixels.begin()+1280*720,100);
    gfx::videoCreate(g,video,1280,720);gfx::videoUpload(g,video,pixels.data(),pixels.size(),0);
  }
  video.bt709=true;gfx::videoQuad(g,0,0,1280,720);gfx::end(g,argv[2]);
  // O failpoint não pode envenenar o próximo pedido, nem publicar uma superfície parcial.
  auto retry=pool.acquire(1280,720,1280,720,alignment);
  std::printf("PASS board mapped enabled=%d first=%d retry=%d bytes=%zu slots=%zu staging=%d\n",pool.enabled,mapped,retry!=nullptr,pool.bytes,pool.slots.size(),!mapped);
  boot_close();
}
