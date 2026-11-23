# Third-party software

Tube360 is MIT licensed (see `LICENSE`). It ships or depends on:

| Component | Where | License |
|---|---|---|
| [pl_mpeg](https://github.com/phoboslab/pl_mpeg) by Dominic Szablewski | `client/third_party/pl_mpeg.h` (with Tube360 patches: slice-parallel decoding, B-frame skipping) | MIT |
| [stb_image, stb_truetype](https://github.com/nothings/stb) by Sean Barrett | `client/third_party/` | MIT / Public Domain |
| [Inter](https://rsms.me/inter/) typeface by Rasmus Andersson | `client/assets/inter-*.ttf` | SIL Open Font License 1.1 (`client/assets/OFL-Inter.txt`) |
| [Phosphor Icons](https://phosphoricons.com) (thumbs up/down) | `client/third_party/phosphor/`, rasterized into `client/src/icons.hpp` by `tools/gen-icons.py` | MIT (`client/third_party/phosphor/LICENSE`) |
| [yt-cast-receiver](https://github.com/patrickkfkan/yt-cast-receiver) | `cast/` (npm) | MIT |
| [YouTube.js](https://github.com/LuanRT/YouTube.js) (`youtubei.js`) | `cast/` (npm), the YouTube account | MIT |
| [yt-dlp](https://github.com/yt-dlp/yt-dlp) | server (pip) | Unlicense |
| [FastAPI](https://github.com/fastapi/fastapi), [Uvicorn](https://github.com/encode/uvicorn) | server (pip) | MIT / BSD-3-Clause |
| [PyChromecast](https://github.com/home-assistant-libs/pychromecast) | server (pip) | MIT |
| [Pillow](https://github.com/python-pillow/Pillow) | server (pip) | MIT-CMU |
| [FFmpeg](https://ffmpeg.org) | server (system binary, invoked as a process) | LGPL/GPL |
| [xsdk](https://github.com/xenosworks/xsdk), [vulkan4xenon](https://github.com/xenosworks/vulkan4xenon), [SDL-xenon](https://github.com/xenosworks/SDL-xenon) | Xbox 360 build toolchain (not vendored) | MIT |

The standalone check title also uses these components.

| Component | Where | License or provenance |
|---|---|---|
| [picojson 1.3.0](https://github.com/kazuho/picojson/tree/v1.3.0) | `client/third_party/picojson.h`, unmodified | BSD-2-Clause (notice in the header) |
| [Mbed TLS 3.6.8](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.8) | Pinned source archive, built locally for each target | Apache-2.0 OR GPL-2.0-or-later, with upstream third-party notices |
| [curl-xenon, a6b70bd](https://github.com/xenosworks/curl-xenon/tree/a6b70bd06820e1c625f47fe0e592e08c5daaa6b4) | Static libcurl for HTTP with Mbed TLS | curl license (`COPYING` in the archive) |
| [Google Trust Services root certificates](https://pki.goog/roots.pem) | `client/src/yt/gts.pem`, four public certificates R1–R4, embedded at build time | Retrieved 2026-11-23. SHA-256 of the PEM file: `6ba8f2f19e161f295a52b49e627796d7dbbeab3c7e60a46b07529e98e18536d7` |

YouTube is a trademark of Google LLC. Tube360 is an independent project, not affiliated with or endorsed by Google or Microsoft.
