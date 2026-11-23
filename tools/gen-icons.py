#!/usr/bin/env python3
"""Rasteriza os SVG do Phosphor no tamanho de exibição. As duas últimas linhas do atlas formam pares regular/fill."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "client/third_party/phosphor"
RATINGS = ["thumbs-up-fill", "thumbs-up-bold", "thumbs-down-fill", "thumbs-down-bold"]
OPTIONS = ["closed-captioning", "gear", "speaker-high", "moon", "thumbs-up", "user-circle", "broadcast", "play", "pause"]
sizes = [22] * 7 + [26, 26]
width, height = sum(sizes), 48 + 24 + 2 * max(sizes)
atlas = bytearray(width * height)


def raster(name, size, x, y):
    raw = subprocess.run(["magick", "-background", "none", "-density", "600", str(SRC / f"{name}.svg"), "-resize",
                          f"{size}x{size}", "-gravity", "center", "-extent", f"{size}x{size}", "-alpha", "extract",
                          "-depth", "8", "gray:-"], check=True, capture_output=True).stdout
    assert len(raw) == size * size, (name, size, len(raw))
    for row in range(size):
        start = (y + row) * width + x
        atlas[start:start + size] = raw[row * size:(row + 1) * size]


for size, y in [(48, 0), (24, 48)]:
    for i, name in enumerate(RATINGS):
        raster(name, size, i * 48, y)

sprites = []
x = 0
for name, size in zip(OPTIONS, sizes):
    raster(name, size, x, 72)
    raster(name + "-fill", size, x, 98)
    sprites.append(f"{{{x}, {size}}}")
    x += size

rows = ",\n".join(", ".join(map(str, atlas[y:y+width])) for y in range(0, len(atlas), width))
(ROOT / "client/src/icons.hpp").write_text(f"""// Gerado por tools/gen-icons.py a partir de client/third_party/phosphor (Phosphor Icons, MIT). Não editar à mão.
#pragma once
namespace icons {{
enum {{ THUMB_UP_FILL, THUMB_UP, THUMB_DOWN_FILL, THUMB_DOWN }};
constexpr int W = {width}, H = {height}, CELL = 48;
constexpr int SIZES[] = {{48, 24}};
enum PlayerIcon {{ {", ".join(name.replace("-", "_").upper() for name in OPTIONS)} }};
struct Sprite {{ int x, size; }};
constexpr Sprite PLAYER[] = {{{", ".join(sprites)}}};
constexpr int REGULAR_Y = 72, FILL_Y = 98;
static const unsigned char ALPHA[W * H] = {{
{rows}
}};
}}  // namespace icons
""", encoding="utf-8")
print(f"icons.hpp: {width}x{height}")
