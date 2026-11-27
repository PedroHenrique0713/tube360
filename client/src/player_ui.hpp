#pragma once
#include <cstdint>
#include <string>

struct PlayerUI {
  enum Panel { NONE, CAPTIONS, QUALITY, SOUND, TRACKS, COMMENTS_PANEL, SPEED_PANEL } panel = NONE;
  enum Option { TRANSPORT, CC, VIDEO, AUDIO, TRACK, TIMER, RATE, DISLIKE, COMMENTS, SPEED, LOOP, CHANNEL, EDGE, COUNT };
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

// "A seguir" e relacionados: fileira que abre com ↓ na linha de ícones, sem parar o vídeo. volumeUntil: mostra o volume do app.
struct PlayerShelf {
  bool open = false;
  std::string drawnKey;  // última lista desenhada na fileira (log)
  int sel = 0, tab = 0;  // tab: 0 A seguir, 1 Do canal, 2 Relacionados
  std::string video;  // vídeo ao qual a fileira pertence; ao trocar, fecha
  uint64_t volumeUntil = 0;
  int volumeShown = 100;
  bool volumeSpeaker = false;  // o indicador mostra a caixa (Nest), não o volume da TV
  uint64_t shortOpenedAt = 0;  // Shorts: quando o atual foi aberto (fim antes de 1 s espera, para não virar laço)
  int shortsFocus = 0;  // Shorts: 0 vídeo, 1 curtir, 2 não gostei, 3 comentários, 4 canal
};

// Painel de som (TV / caixa / volume), compartilhado entre o player e o Tocando agora da Música.
struct SoundPanel { bool open = false; int sel = 0; };
