// Decode real sem pacing: hash/export só no passe visual, pois dariam folga escondida aos workers.
#include "h264.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int readFile(void* p,uint8_t* dst,int size) {
  size_t n = std::fread(dst,1,size,(FILE*)p);
  return n ? int(n) : AVERROR_EOF;
}
static int64_t seekFile(void* p,int64_t offset,int whence) {
  FILE* file = (FILE*)p;
  if (whence == AVSEEK_SIZE) {
    auto position = ftello(file); fseeko(file,0,SEEK_END); auto size = ftello(file); fseeko(file,position,SEEK_SET); return size;
  }
  return fseeko(file,offset,whence & ~AVSEEK_FORCE) ? AVERROR(EIO) : ftello(file);
}
int main(int argc,char** argv) {
  if (argc < 5 || argc > 6) return 2;
  FILE* file = std::fopen(argv[1],"rb"); if (!file) return 2;
  int threads = std::atoi(argv[2]), type = std::atoi(argv[3]), filter = std::atoi(argv[4]);
  h264::Stream stream; stream.threadType = type;
  int result = stream.open(file,readFile,threads,false,true,seekFile);
  if (result < 0) return 3;
  stream.video->skip_loop_filter = filter ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
  using Clock = std::chrono::steady_clock;
  auto ticks = [] { return Clock::now(); };
  uint64_t nanoseconds = 0, hash = 1469598103934665603ULL;
  int frames = 0, width = 0, height = 0, packets = 0;
  double first = -1, last = -1;
  auto decode = [&](AVPacket* packet) {
    auto began = ticks(); int r = avcodec_send_packet(stream.video,packet);
    nanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(ticks()-began).count();
    if (r < 0) return r;
    for (;;) {
      began = ticks(); r = avcodec_receive_frame(stream.video,stream.frame);
      nanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(ticks()-began).count();
      if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return 0;
      if (r < 0) return r;
      AVFrame* f = stream.frame;
      if (f->decode_error_flags || f->format != AV_PIX_FMT_YUV420P) return AVERROR_INVALIDDATA;
      ++frames; width = f->width; height = f->height;
      last = stream.pts(stream.vi); if (first < 0) first = last;
      if (argc == 6) for (int plane = 0; plane < 3; ++plane)
        for (int y = 0; y < (plane ? (height+1)/2 : height); ++y)
          for (int x = 0; x < (plane ? (width+1)/2 : width); ++x) {
            hash ^= f->data[plane][y*f->linesize[plane]+x]; hash *= 1099511628211ULL;
          }
      if (argc == 6 && (frames == 31 || frames == 91 || frames == 151)) {
        char name[1024]; std::snprintf(name,sizeof(name),"%s-%03d.yuv",argv[5],frames);
        FILE* out = std::fopen(name,"wb"); if (!out) return AVERROR(EIO);
        for (int plane = 0; plane < 3; ++plane)
          for (int y = 0; y < (plane ? (height+1)/2 : height); ++y)
            std::fwrite(f->data[plane]+y*f->linesize[plane],1,plane ? (width+1)/2 : width,out);
        std::fclose(out);
      }
    }
  };
  auto totalAt = ticks();
  while ((result = av_read_frame(stream.fmt,stream.pkt)) >= 0) {
    if (stream.pkt->stream_index == stream.vi) { ++packets; result = decode(stream.pkt); }
    av_packet_unref(stream.pkt); if (result < 0) break;
  }
  if (result == AVERROR_EOF) result = decode(nullptr);
  nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(ticks()-totalAt).count();
  double fps = av_q2d(stream.fmt->streams[stream.vi]->avg_frame_rate);
  double duration = last-first + (fps > 0 ? 1/fps : 0);
  std::printf("%s,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%016llx,%d\n",argv[1],width,height,threads,type,
              stream.video->active_thread_type,filter,packets,frames,nanoseconds/1e6,
              frames ? nanoseconds/1e6/frames : 0,duration > 0 ? nanoseconds/1e6/duration : 0,
              (unsigned long long)hash,result);
  stream.close(); std::fclose(file);
  return result < 0 || frames == 0 || packets == 0 ? 4 : 0;
}
