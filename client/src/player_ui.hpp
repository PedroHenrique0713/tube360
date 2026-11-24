#pragma once
#include <cstdint>

struct PlayerUI {
  enum Panel { NONE, CAPTIONS, QUALITY, SOUND, TRACKS } panel = NONE;
  enum Option { TRANSPORT, CC, VIDEO, AUDIO, TRACK, TIMER, RATE, DISLIKE, CHANNEL, EDGE, COUNT };
  bool tracks = false;
  uint64_t until = 0;
  bool options = false;
  int selected = TIMER, panelSel = 0, sleepRow = 0;
  static constexpr uint64_t timeout = 5000;

  bool visible(uint64_t now) const { return options || panel != NONE || now < until; }
  void touch(uint64_t now) { until = now + timeout; }
  void reset(uint64_t now) { options = false; panel = NONE; sleepRow = 0; if (!tracks && selected==TRACK) selected=VIDEO; touch(now); }
  // Retorna o salto somente quando a trilha já tinha o foco antes do toque.
  int direction(uint64_t now, int horizontal, int vertical, int optionCount = EDGE) {
    if (!visible(now)) { options = false; touch(now); return 0; }
    touch(now);
    if (options) {
      selected = (selected + horizontal + optionCount) % optionCount;
      if (!tracks && selected == TRACK) selected = (selected + (horizontal < 0 ? -1 : 1) + optionCount) % optionCount;
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
