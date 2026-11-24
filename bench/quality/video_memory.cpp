// Retém dez quadros fora do DPB e compara a saída do shader com o controle staging.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#undef STB_TRUETYPE_IMPLEMENTATION
#include "gfx_video_memory.hpp"
#include "h264.hpp"
#include <deque>

int main(int argc,char** argv) {
  if (argc != 4) return 2;
  int mode = std::atoi(argv[2]);
  bool direct = mode == 1;
  if (!SDL_Init(SDL_INIT_VIDEO)) return 2;
  gfx::Gfx g; g.readback = true;
  if (!gfx::init(g,1920,1080,"video memory")) return 2;
  gfx::DecodePool pool(g);
  if (direct && !pool.enabled) return 3;
  gfx::Video v; g.video = &v;
  struct Reject : video::Allocator {
    std::shared_ptr<video::Surface> acquire(int,int,int,int,const int*) override { return {}; }
  } reject;
  h264::Stream s; s.videoBuffers = mode == 2 ? static_cast<video::Allocator*>(&reject) : direct ? &pool : nullptr;
  FILE* file = std::fopen(argv[1],"rb"); if (!file) return 2;
  auto read = [](void* p,uint8_t* bytes,int n) { size_t got = std::fread(bytes,1,n,(FILE*)p); return got ? int(got) : AVERROR_EOF; };
  auto seek = [](void* p,int64_t offset,int whence) -> int64_t {
    FILE* f = (FILE*)p;
    if (whence == AVSEEK_SIZE) { auto at = ftello(f); fseeko(f,0,SEEK_END); auto size = ftello(f); fseeko(f,at,SEEK_SET); return size; }
    return fseeko(f,offset,whence & ~AVSEEK_FORCE) ? AVERROR(EIO) : ftello(f);
  };
  int result = s.open(file,read,4,false,true,seek);
  if (result < 0) return 3;
  struct Pending { std::shared_ptr<video::Surface> surface; std::vector<uint8_t> yuv; int w,h,number; uint64_t hash; };
  std::deque<Pending> queue;
  uint64_t combined = 1469598103934665603ULL;
  int count = 0, shown = 0;
  auto hash = [](const uint8_t* const* data,const int* strides,int w,int h) {
    uint64_t sum = 1469598103934665603ULL;
    for (int i = 0; i < 3; ++i)
      for (int y = 0; y < (i ? (h+1)/2 : h); ++y)
        for (int x = 0; x < (i ? (w+1)/2 : w); ++x) { sum ^= data[i][y*strides[i]+x]; sum *= 1099511628211ULL; }
    return sum;
  };
  auto show = [&] {
    Pending f = std::move(queue.front()); queue.pop_front();
    gfx::begin(g);
    if (f.surface) {
      const uint8_t* data[] = {f.surface->data[0],f.surface->data[1],f.surface->data[2]};
      if (hash(data,f.surface->stride,f.w,f.h) != f.hash) return false;
      gfx::videoUse(g,v,f.surface,f.number/30.0);
    } else {
      if (v.w != f.w || v.h != f.h) { gfx::videoDestroy(g,v); gfx::videoCreate(g,v,f.w,f.h); }
      gfx::videoUpload(g,v,f.yuv.data(),f.yuv.size(),f.number/30.0);
    }
    v.bt709 = true; gfx::videoQuad(g,0,0,1280,720);
    char path[1024]; std::snprintf(path,sizeof(path),"%s-%03d.ppm",argv[3],f.number);
    gfx::end(g,(f.number == 31 || f.number == 91 || f.number == 151) ? path : nullptr);
    combined ^= f.hash; combined *= 1099511628211ULL; ++shown;
    return true;
  };
  auto decode = [&](AVPacket* packet) {
    int r = avcodec_send_packet(s.video,packet); if (r < 0) return r;
    while ((r = avcodec_receive_frame(s.video,s.frame)) >= 0) {
      auto* f = s.frame; if (f->decode_error_flags) return AVERROR_INVALIDDATA;
      Pending pending{}; pending.w = f->width; pending.h = f->height; pending.number = ++count;
      if (mode == 2 && h264::mappedSurface(f)) return AVERROR_INVALIDDATA;
      pending.hash = hash(f->data,f->linesize,f->width,f->height);
      if (direct) { pending.surface = h264::mappedSurface(f); if (!pending.surface) return AVERROR_INVALIDDATA; pending.surface->publish(); }
      else for (int i = 0; i < 3; ++i)
        for (int y = 0; y < (i ? (f->height+1)/2 : f->height); ++y)
          pending.yuv.insert(pending.yuv.end(),f->data[i]+y*f->linesize[i],f->data[i]+y*f->linesize[i]+(i ? (f->width+1)/2 : f->width));
      queue.push_back(std::move(pending));
      if (queue.size() >= 10 && !show()) return AVERROR_INVALIDDATA;
    }
    return r == AVERROR(EAGAIN) || r == AVERROR_EOF ? 0 : r;
  };
  while ((result = av_read_frame(s.fmt,s.pkt)) >= 0) {
    if (s.pkt->stream_index == s.vi) result = decode(s.pkt);
    av_packet_unref(s.pkt); if (result < 0) break;
  }
  if (result == AVERROR_EOF) result = decode(nullptr);
  while (result >= 0 && !queue.empty()) if (!show()) result = AVERROR_INVALIDDATA;
  s.close(); std::fclose(file); gfx::videoDestroy(g,v);
  LOG("video-memory-test: direto %d, decodificados %d apresentados %d, hash %016llx, pool %u, resultado %d",
      direct,count,shown,(unsigned long long)combined,(unsigned)pool.slots.size(),result);
  SDL_Quit();
  return result < 0 || count == 0 || count != shown ? 4 : 0;
}
