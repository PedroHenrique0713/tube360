#!/usr/bin/env python3
"""Gera client/src/icons.hpp: os ícones SVG de client/third_party/phosphor rasterizados (ImageMagick) em um atlas só de
alfa, nos tamanhos exatos em que a tela os desenha (sem reduzir na GPU, que serrilha). Rodar de novo ao trocar um SVG."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "client/third_party/phosphor"
ICONS = ["thumbs-up-fill", "thumbs-up-bold", "thumbs-down-fill", "thumbs-down-bold", "translate-bold"]
SIZES = [48, 24, 72, 36]

width, height = max(SIZES) * len(ICONS), sum(SIZES)
atlas = bytearray(width * height)
y = 0
for size in SIZES:
    for i, name in enumerate(ICONS):
        raw = subprocess.run(["magick", "-background", "none", "-density", "600", str(SRC / f"{name}.svg"), "-resize",
                              f"{size}x{size}", "-gravity", "center", "-extent", f"{size}x{size}", "-alpha", "extract",
                              "-depth", "8", "gray:-"], check=True, capture_output=True).stdout
        assert len(raw) == size * size, (name, size, len(raw))
        for row in range(size):
            start = (y + row) * width + i * max(SIZES)
            atlas[start:start + size] = raw[row * size:(row + 1) * size]
    y += size

lines = ", ".join(str(b) for b in atlas)
(ROOT / "client/src/icons.hpp").write_text(f"""// Gerado por tools/gen-icons.py a partir de client/third_party/phosphor (Phosphor Icons, MIT). Não editar à mão.
#pragma once
namespace icons {{
// Ordem das colunas: {", ".join(ICONS)}. Linhas: {" e ".join(f"{s} px" for s in SIZES)}.
enum {{ THUMB_UP_FILL, THUMB_UP, THUMB_DOWN_FILL, THUMB_DOWN, TRANSLATE }};
constexpr int W = {width}, H = {height}, CELL = {max(SIZES)};
constexpr int SIZES[] = {{{", ".join(map(str, SIZES))}}};
static const unsigned char ALPHA[W * H] = {{{lines}}};
}}  // namespace icons
""", encoding="utf-8")
print(f"icons.hpp: {width}x{height}")
