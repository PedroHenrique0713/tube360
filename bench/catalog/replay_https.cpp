#include "yt/catalog_replay.hpp"

// O link de prova substitui também as conexões internas do player de vídeo.
std::unique_ptr<yt::Transport> replayHttps(const std::atomic_bool*) asm("__wrap__ZN2yt5httpsEPKSt6atomicIbE");
std::unique_ptr<yt::Transport> replayHttps(const std::atomic_bool*) {
  const char* directory = std::getenv("XT_CATALOG_REPLAY");
  return std::make_unique<yt::catalog::Replay>(directory ? directory : "");
}
