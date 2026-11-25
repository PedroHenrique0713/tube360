#pragma once
#include <cstddef>
#include <cstdint>
#ifdef __XSDK__
#include "xsdk/kernel.h"
#endif

namespace video {
inline size_t titleFreeBytes() {
#ifdef __XSDK__
  MM_QUERY_STATISTICS_RESULT memory{}; memory.size = sizeof(memory);
  if (!NT_SUCCESS(MmQueryStatistics(&memory))) return 0;
  return size_t(memory.title.available_pages) * 4096;
#else
  return SIZE_MAX;
#endif
}
struct QueueBudget { int capacity = 10, startup = 6; };
inline QueueBudget decodeQueue(int height, double fps, size_t freeBytes) {
  // Oito superfícies extras de até 4 MiB; retém pelo menos 64 MiB livres.
  if (height >= 1080 && fps > 0 && fps <= 30.1 && freeBytes >= (96u << 20)) return {18,12};
  return {};
}
}
