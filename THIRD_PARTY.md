# Third-party software

Tube360 is MIT licensed (see `LICENSE`). It ships or depends on:

| Component | Where | License |
|---|---|---|
| [pl_mpeg](https://github.com/phoboslab/pl_mpeg) by Dominic Szablewski | `client/third_party/pl_mpeg.h` (with Tube360 patches: slice-parallel decoding, B-frame skipping) | MIT |
| [stb_image, stb_truetype](https://github.com/nothings/stb) by Sean Barrett | `client/third_party/` | MIT / Public Domain |
| [Inter](https://rsms.me/inter/) typeface by Rasmus Andersson | `client/assets/inter-*.ttf` | SIL Open Font License 1.1 (`client/assets/OFL-Inter.txt`) |
| [Phosphor Icons](https://phosphoricons.com) (player controls and ratings) | `client/third_party/phosphor/`, rasterized into `client/src/icons.hpp` by `tools/gen-icons.py` | MIT (`client/third_party/phosphor/LICENSE`) |
| [yt-cast-receiver](https://github.com/patrickkfkan/yt-cast-receiver) | `cast/` (npm) | MIT |
| [YouTube.js](https://github.com/LuanRT/YouTube.js) (`youtubei.js`) | `cast/` (npm), the YouTube account | MIT |
| [yt-dlp](https://github.com/yt-dlp/yt-dlp) | server (pip) | Unlicense |
| [FastAPI](https://github.com/fastapi/fastapi), [Uvicorn](https://github.com/encode/uvicorn) | server (pip) | MIT / BSD-3-Clause |
| [PyChromecast](https://github.com/home-assistant-libs/pychromecast) | server (pip) | MIT |
| [Pillow](https://github.com/python-pillow/Pillow) | server (pip) | MIT-CMU |
| [FFmpeg](https://ffmpeg.org) | server (system binary, invoked as a process) | LGPL/GPL |
| [FFmpeg 7.1.5](https://ffmpeg.org/releases/ffmpeg-7.1.5.tar.xz) | H.264/AAC playback in the Xbox title (static) | LGPL-2.1-or-later |
| [xsdk](https://github.com/xenosworks/xsdk), [vulkan4xenon](https://github.com/xenosworks/vulkan4xenon) | Xbox 360 build toolchain (not vendored) | MIT |
| [SDL-xenon](https://github.com/xenosworks/SDL-xenon) | Xbox SDL port | zlib |

YouTube is a trademark of Google LLC. Tube360 is an independent project, not affiliated with or endorsed by Google or Microsoft.

## Xbox release and relinking

The Xbox title includes libavformat, libavcodec, libswresample and libavutil from FFmpeg 7.1.5 under the LGPL
2.1 or later. The pinned configuration in `tools/build-ffmpeg-x360.sh` disables GPL, nonfree and version-3-only
components. Distribute this notice and `tools/ffmpeg-x360/COPYING.LGPLv2.1` with the title. Permit modification
for personal use and reverse engineering for debugging those modifications.

Alongside each XEX, supply the matching FFmpeg source archive, its local modifications and dated build scripts,
the Tube360 source at that release, and the application objects, SDK support libraries and relink instructions.
Keep upstream notices. The objects include the compiled shader bundle. Keep the source and relink materials
available from the same download location as the binary.

To rebuild, place the pinned archive in `.work/src/`, set the Xbox toolchain paths as described in the README,
and run `tools/build-ffmpeg-x360.sh scalar` followed by `tools/build-xbox.sh`. To substitute a modified FFmpeg,
install ABI-compatible static libraries and headers into `.work/ffmpeg-scalar/`, remove the build's old linked
executable `client/build-x360/tube360`, and rerun `tools/build-xbox.sh`. CMake relinks and packages the XEX
with the same title ID. The release also includes the unlinked application objects for relinking without
recompiling the application. [FFmpeg licensing](https://ffmpeg.org/legal.html) · [LGPL 2.1, section 6](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html)

The object kit places `main.cpp.obj`, `shaders_bundle.c.obj` and `ffmpeg-libc.c.obj` in `objects/`,
the matching `xsdk.ld`, `libxsdk.a`, `libSDL3.a` and `libvulkan4xenon.a` in `support/`, and the four FFmpeg
archives in `ffmpeg-lib/`. Include the support libraries' licenses. Place `tools/ffmpeg-x360/relink.sh` at the
kit root. With the matching xsdk and GCC installed, run `FFMPEG_LIB=/path/to/modified/lib ./relink.sh`.
