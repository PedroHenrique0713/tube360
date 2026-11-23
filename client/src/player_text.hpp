#pragma once
#include "gfx.hpp"

// Os rótulos ficam muitos quadros iguais, sobretudo com o foco preso na fileira.
struct PlayerText {
  std::string source, text;
  float width = 0;
  bool ready = false;
  void update(gfx::Gfx& g, int font, const char* value, float limit) {
    if (ready && source == value) return;
    source = value; text = gfx::ellipsize(g, font, source, limit);
    width = gfx::textWidth(g, font, text); ready = true;
  }
};
