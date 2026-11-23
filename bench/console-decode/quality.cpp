// Mesmos quatro trabalhos por fatias do player, sem pular B. Carrega o MPEG-PS antes de cronometrar.
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <vector>

#ifdef __XSDK__
#include "xsdk/log.h"
#define LOG(...) xsdk_log_write(__VA_ARGS__)
#else
#define LOG(...) (std::printf(__VA_ARGS__), std::putchar('\n'))
#endif

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#undef PL_MPEG_IMPLEMENTATION
#include "player.hpp"

static bool measure(const char* path) {
  FILE* file = std::fopen(path, "rb");
  if (!file) { LOG("FAIL: %s", path); return false; }
  std::fseek(file, 0, SEEK_END);
  long size = std::ftell(file);
  std::rewind(file);
  // Os trechos de 20 s a 15 Mbit/s ocupam até 38 MB. Cabe um de cada vez nos 512 MB do console.
  if (size <= 0 || size > 64L * 1024 * 1024) { std::fclose(file); LOG("FAIL: tamanho %ld", size); return false; }
  std::vector<uint8_t> data(size);
  size_t read = std::fread(data.data(), 1, data.size(), file);
  std::fclose(file);
  if (read != data.size()) { LOG("FAIL: leitura de %s", path); return false; }

  plm_t* video = plm_create_with_memory(data.data(), data.size(), 0);
  if (!video) { LOG("FAIL: pl_mpeg %s", path); return false; }
  plm_set_audio_enabled(video, 0);
  std::vector<double> times;
  times.reserve(640);
  double total = 0;
#ifndef __XSDK__
  // No PC compartilhado, o tempo de CPU das quatro threads também permite comparar rodadas sob carga.
  auto cpuStart = std::clock();
#endif
  while (true) {
    Uint64 start = SDL_GetTicksNS();
    if (!plm_decode_video(video)) break;
    double ms = (SDL_GetTicksNS() - start) / 1e6;
    times.push_back(ms);
    total += ms;
  }
  plm_destroy(video);
  if (times.empty()) { LOG("FAIL: nenhum quadro em %s", path); return false; }
#ifndef __XSDK__
  LOG("cpu %s cpu_ms=%.4f", path, 1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC / times.size());
#endif
  std::sort(times.begin(), times.end());
  LOG("decode %s frames=%u mean_ms=%.4f p95_ms=%.4f max_ms=%.4f", path, (unsigned)times.size(),
      total / times.size(), times[(times.size() * 95 - 1) / 100], times.back());
  return true;
}

int main(int argc, char** argv) {
  if (!SDL_Init(0)) { LOG("FAIL: SDL: %s", SDL_GetError()); return 1; }
  player::moveToHardwareThread(2);
  player::SlicePool::get().start();
  plm_video_run_parallel = player::SlicePool::run;
  plm_video_parallel_jobs = player::SlicePool::N + 1;
  if (argc > 1) {
    for (int i = 1; i < argc; i++) if (!measure(argv[i])) return 1;
    return 0;
  }
#ifdef __XSDK__
  return measure("game:\\before.mpg") && measure("game:\\after.mpg") ? 0 : 1;
#else
  std::fputs("usage: decode-quality FILE.mpg [FILE.mpg ...]\n", stderr);
  return 1;
#endif
}
