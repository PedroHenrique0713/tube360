#!/usr/bin/env python3
"""Rasteriza os SVG do Phosphor no tamanho de exibição. As duas últimas linhas do atlas formam pares regular/fill."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "client/third_party/phosphor"
RATINGS = ["thumbs-up-fill", "thumbs-up-bold", "thumbs-down-fill", "thumbs-down-bold", "translate-bold"]
SIZES = [48, 24, 72, 36]
OPTIONS = ["closed-captioning", "gear", "speaker-high", "translate", "moon", "thumbs-up", "thumbs-down", "user-circle", "broadcast", "play", "pause"]
sizes = [22] * 9 + [26, 26]
width, height = max(72 * len(RATINGS), sum(sizes) * 3 // 2), sum(SIZES) + 2 * 26 + 2 * 39
atlas = bytearray(width * height)


def raster(name, size, x, y):
    raw = subprocess.run(["magick", "-background", "none", "-density", "600", str(SRC / f"{name}.svg"), "-resize",
                          f"{size}x{size}", "-gravity", "center", "-extent", f"{size}x{size}", "-alpha", "extract",
                          "-depth", "8", "gray:-"], check=True, capture_output=True).stdout
    assert len(raw) == size * size, (name, size, len(raw))
    for row in range(size):
        start = (y + row) * width + x
        atlas[start:start + size] = raw[row * size:(row + 1) * size]


y = 0
for size in SIZES:
    for i, name in enumerate(RATINGS):
        raster(name, size, i * 72, y)
    y += size

sprites = [[], []]
regular_y, fill_y = [], []
for level, scale in enumerate([1, 1.5]):
    regular_y.append(y)
    fill_y.append(y + int(26 * scale))
    x = 0
    for name, base_size in zip(OPTIONS, sizes):
        size = int(base_size * scale)
        raster(name, size, x, regular_y[-1])
        raster(name + "-fill", size, x, fill_y[-1])
        sprites[level].append(f"{{{x}, {size}}}")
        x += size
    y += 2 * int(26 * scale)

rows = ",\n".join(", ".join(map(str, atlas[y:y+width])) for y in range(0, len(atlas), width))
(ROOT / "client/src/icons.hpp").write_text(f"""// Gerado por tools/gen-icons.py a partir de client/third_party/phosphor (Phosphor Icons, MIT). Não editar à mão.
#pragma once
namespace icons {{
enum {{ THUMB_UP_FILL, THUMB_UP, THUMB_DOWN_FILL, THUMB_DOWN, TRANSLATE }};
constexpr int W = {width}, H = {height}, CELL = 72;
constexpr int SIZES[] = {{{", ".join(map(str, SIZES))}}};
enum PlayerIcon {{ {", ".join(("TRANSLATE_PLAYER" if name == "translate" else name.replace("-", "_").upper()) for name in OPTIONS)} }};
struct Sprite {{ int x, size; }};
constexpr Sprite PLAYER[2][{len(OPTIONS)}] = {{{{{", ".join(sprites[0])}}}, {{{", ".join(sprites[1])}}}}};
constexpr int REGULAR_Y[] = {{{", ".join(map(str, regular_y))}}}, FILL_Y[] = {{{", ".join(map(str, fill_y))}}};
static const unsigned char ALPHA[W * H] = {{
{rows}
}};
}}  // namespace icons
""", encoding="utf-8")
print(f"icons.hpp: {width}x{height}")
