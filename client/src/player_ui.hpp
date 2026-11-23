#pragma once
#include <cstdint>

struct PlayerUI {
  enum Panel { NONE, CAPTIONS, QUALITY, SOUND } panel = NONE;
  enum Option { CC, VIDEO, AUDIO, TIMER, RATE, CHANNEL, COUNT };
  uint64_t until = 0;
  bool options = false;
  int selected = TIMER, panelSel = 0, sleepRow = 0;
  static constexpr uint64_t timeout = 5000;

  bool visible(uint64_t now) const { return options || panel != NONE || now < until; }
  void touch(uint64_t now) { until = now + timeout; }
  void reset(uint64_t now) { options = false; panel = NONE; sleepRow = 0; touch(now); }
  // Retorna o salto somente quando a trilha já tinha o foco antes do toque.
  int direction(uint64_t now, int horizontal, int vertical) {
    if (!visible(now)) { options = false; touch(now); return 0; }
    touch(now);
    if (options) {
      selected = (selected + horizontal + COUNT) % COUNT;
      if (vertical < 0) options = false;
      return 0;
    }
    if (vertical > 0) options = true;
    return horizontal;
  }
  // false: a barra já estava escondida, então B pode sair do vídeo.
  bool back(uint64_t now) {
    if (panel != NONE) { panel = NONE; touch(now); return true; }
    if (options) { options = false; touch(now); return true; }
    if (visible(now)) { until = 0; return true; }
    return false;
  }
};
