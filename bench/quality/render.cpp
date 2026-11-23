// Renderiza um quadro YUV pelo shader real para comparar a redução com o Lanczos do ffmpeg.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#undef STB_TRUETYPE_IMPLEMENTATION
#include "gfx.hpp"
#include <fstream>

int main(int argc,char** argv) {
    if (argc!=6) return 2;
    int w=std::atoi(argv[2]),h=std::atoi(argv[3]);
    if (w<=0 || h<=0 || w>1920 || h>1080 || (w&1) || (h&1)) return 2;
    std::vector<uint8_t> data(size_t(w)*h*3/2);
    std::ifstream in(argv[1],std::ios::binary);
    if (!in.read((char*)data.data(),data.size())) return 2;
    SDL_Init(SDL_INIT_VIDEO);
    gfx::Gfx g; g.readback=true;
    if (const char* level=std::getenv("XT_QUALITY_GPU")) g.videoMetrics=std::atoi(level);
    int output=std::getenv("XT_OUTPUT_HEIGHT") ? std::atoi(std::getenv("XT_OUTPUT_HEIGHT")) : 720;
    if (!gfx::init(g,output*16/9,output,"quality measurement")) return 2;
    VkPhysicalDeviceProperties device{}; vkGetPhysicalDeviceProperties(g.phys,&device);
    std::printf("measurement device: %s\n",device.deviceName);
    gfx::Video video; g.video=&video; gfx::videoCreate(g,video,w,h); video.bt709=true;
    int count=std::getenv("XT_QUALITY_FRAMES") ? std::max(4,std::atoi(std::getenv("XT_QUALITY_FRAMES"))) : 4;
    for (int i=0;i<count;++i) {
        gfx::begin(g); gfx::videoUpload(g,video,data.data(),data.size(),0);
        gfx::videoQuad(g,0,0,1280,720);
        if (std::atoi(argv[4])==0) g.videoFilter[0]=g.videoFilter[1]=0; // Controle: bilinear, mesma GPU e matriz.
        gfx::end(g,i==count-1 ? argv[5] : nullptr);
    }
    gfx::videoDestroy(g,video); SDL_Quit();
}
