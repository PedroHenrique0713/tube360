// Mesmos quatro trabalhos por fatias do player, sem pular B. A leitura dos arquivos fica fora da medição.
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

struct Clip {
  const char* path;
  std::vector<uint8_t> data;
  std::vector<double> times;
  plm_t* video = nullptr;
  double total = 0;
#ifndef __XSDK__
  std::clock_t cpu = 0;
#endif
  bool ended = false;

  explicit Clip(const char* path) : path(path) {}
  ~Clip() { if (video) plm_destroy(video); }

  bool open() {
    FILE* file = std::fopen(path, "rb");
    if (!file) { LOG("FAIL: %s", path); return false; }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::rewind(file);
    // Cada trecho de 20 s a 15 Mbit/s ocupa até 38 MB. O par cabe nos 512 MB do console.
    if (size <= 0 || size > 64L * 1024 * 1024) { std::fclose(file); LOG("FAIL: tamanho %ld", size); return false; }
    data.resize(size);
    size_t read = std::fread(data.data(), 1, data.size(), file);
    std::fclose(file);
    if (read != data.size()) { LOG("FAIL: leitura de %s", path); return false; }
    video = plm_create_with_memory(data.data(), data.size(), 0);
    if (!video) { LOG("FAIL: pl_mpeg %s", path); return false; }
    plm_set_audio_enabled(video, 0);
    times.reserve(640);
    return true;
  }

  void block() {
#ifndef __XSDK__
    auto cpuStart = std::clock();
#endif
    for (int i = 0; i < 15 && !ended; i++) {
      Uint64 start = SDL_GetTicksNS();
      if (!plm_decode_video(video)) { ended = true; break; }
      double ms = (SDL_GetTicksNS() - start) / 1e6;
      times.push_back(ms);
      total += ms;
    }
#ifndef __XSDK__
    cpu += std::clock() - cpuStart;
#endif
  }

  bool report() {
    if (times.empty()) { LOG("FAIL: nenhum quadro em %s", path); return false; }
    std::sort(times.begin(), times.end());
#ifndef __XSDK__
    LOG("cpu %s cpu_ms=%.4f", path, 1000.0 * cpu / CLOCKS_PER_SEC / times.size());
#endif
    LOG("decode %s frames=%u mean_ms=%.4f p95_ms=%.4f max_ms=%.4f", path, (unsigned)times.size(),
        total / times.size(), times[(times.size() * 95 - 1) / 100], times.back());
    return true;
  }
};

static bool compare(const char* first, const char* second) {
  Clip a(first), b(second);
  if (!a.open() || !b.open()) return false;
  // ABBA em blocos de meio segundo de vídeo para distribuir variações de carga e frequência entre os dois.
  while (!a.ended || !b.ended) {
    a.block(); b.block();
    b.block(); a.block();
  }
  if (!a.report() || !b.report()) return false;
  // Conforme a reordenação de B, o último quadro do PS pode ficar retido no pl_mpeg.
  if (a.times.size() + 1 < b.times.size() || b.times.size() + 1 < a.times.size()) {
    LOG("FAIL: contagem de quadros diferente"); return false;
  }
  return true;
}

int main(int argc, char** argv) {
  if (!SDL_Init(0)) { LOG("FAIL: SDL: %s", SDL_GetError()); return 1; }
  player::moveToHardwareThread(2);
  player::SlicePool::get().start();
  plm_video_run_parallel = player::SlicePool::run;
  plm_video_parallel_jobs = player::SlicePool::N + 1;
  if (argc == 4 && !std::strcmp(argv[1], "--pair")) return compare(argv[2], argv[3]) ? 0 : 1;
  if (argc > 1) {
    for (int i = 1; i < argc; i++) {
      Clip clip(argv[i]);
      if (!clip.open()) return 1;
      while (!clip.ended) clip.block();
      if (!clip.report()) return 1;
    }
    return 0;
  }
#ifdef __XSDK__
  return compare("game:\\before.mpg", "game:\\after.mpg") ? 0 : 1;
#else
  std::fputs("usage: decode-quality FILE.mpg [...] | --pair BEFORE.mpg AFTER.mpg\n", stderr);
  return 1;
#endif
}
