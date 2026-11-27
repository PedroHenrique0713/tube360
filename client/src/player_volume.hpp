// Volume do app: passos de 5 em 0..100, sempre caindo numa grade (um valor fora dela, de um settings.txt editado, anda até a vizinha).
#pragma once
#include <algorithm>

namespace volume {
constexpr int step = 5;
constexpr int clampPercent(int percent) { return std::clamp(percent, 0, 100); }
constexpr int stepped(int percent, int direction) {
  percent = clampPercent(percent);
  return clampPercent(direction > 0 ? (percent / step + 1) * step : ((percent + step - 1) / step - 1) * step);
}
}  // namespace volume
