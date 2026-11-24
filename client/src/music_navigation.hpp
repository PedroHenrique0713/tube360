#pragma once
#include "navigation.hpp"

struct MusicNavigation : Navigation {
  bool localQueue = false;
  size_t queueIndex = 0;
  std::string queueStart;
  bool nativePage() const { return !pages.empty() && pages.back().native; }
  void clearLocalQueue() { localQueue = false; ownsQueue = false; queue.clear(); queueStart.clear(); }
};
