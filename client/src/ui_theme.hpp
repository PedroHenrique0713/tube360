// Tema compartilhado por todas as telas: cores, índices de fonte, escala e colunas.
#pragma once

// ---------------------------------------------------------------- tema
namespace ui {
const Color BG = rgb(0x0b0b0e), SURFACE = rgb(0x18181d), SURFACE2 = rgb(0x24242b), TEXT = rgb(0xf5f5f7),
            MUTED = rgb(0xa1a1aa), FAINT = rgb(0x6b6b74), A1 = rgb(0xff0f3c), A2 = rgb(0xff5a52), INK = rgb(0x0b0b0e);
enum { F_BODY, F_SMALL, F_CARD, F_H2, F_LOGO, F_BIG, F_KEY, F_TINY, F_HERO, F_CHAN, F_LOGOL, F_CODE, F_VIDEO_TITLE, F_META, F_DETAIL, F_CLOCK, F_BADGE };
inline float W = 1280;
constexpr float H = 720, RAIL = 96, X0 = 136;
inline bool large = false, narrow = false;
inline int columns(int preferred) { return narrow ? 2 : large ? std::min(3,preferred) : preferred; }
inline Color alpha(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }
inline Color mix(Color a, Color b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t}; }
}  // namespace ui
