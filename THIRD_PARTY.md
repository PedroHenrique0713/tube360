# Third-party software

Tube360 is MIT licensed (see `LICENSE`). It ships or depends on:

| Component | Where | License |
|---|---|---|
| [pl_mpeg](https://github.com/phoboslab/pl_mpeg) by Dominic Szablewski | `client/third_party/pl_mpeg.h` (with Tube360 patches: slice-parallel decoding, B-frame skipping) | MIT |
| [stb_image, stb_truetype](https://github.com/nothings/stb) by Sean Barrett | `client/third_party/` | MIT / Public Domain |
| [GNU FriBidi 1.0.16](https://github.com/fribidi/fribidi/releases/tag/v1.0.16) | `client/third_party/fribidi/`, static bidi/Arabic shaping core, Unicode 16 | LGPL-2.1-or-later (`COPYING`); corresponding source is vendored |
| [Noto Sans Arabic 2.012](https://github.com/notofonts/arabic/releases/tag/NotoSansArabic-v2.012) | `client/assets/noto-sans-arabic-*.ttf`, unmodified Light/Regular/SemiBold/Bold | SIL OFL 1.1 (`client/assets/OFL-NotoSansArabic.txt`) |
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

The Tube360 title and the standalone check both use these components.

The Xenon H.264 chroma/qpel/weight additions in `tools/ffmpeg-x360/h264-vmx128.h` and
`h264-tube360.c`, and their FFmpeg integration, are LGPL-2.1-or-later. Keep these sources and
the matching build recipe with the FFmpeg source/relink materials; they are not MIT application code.

| Component | Where | License or provenance |
|---|---|---|
| [picojson 1.3.0](https://github.com/kazuho/picojson/tree/v1.3.0) | `client/third_party/picojson.h`, unmodified | BSD-2-Clause (notice in the header) |
| [Mbed TLS 3.6.8](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.8) | Pinned source archive, built locally for each target | Apache-2.0 OR GPL-2.0-or-later, with upstream third-party notices |
| [curl-xenon, a6b70bd](https://github.com/xenosworks/curl-xenon/tree/a6b70bd06820e1c625f47fe0e592e08c5daaa6b4) | Static libcurl for HTTP with Mbed TLS | curl license (`COPYING` in the archive) |
| [Google Trust Services root certificates](https://pki.goog/roots.pem) | `client/src/yt/gts.pem`, four public certificates R1–R4, embedded at build time | Retrieved 2026-11-23. SHA-256 of the PEM file: `6ba8f2f19e161f295a52b49e627796d7dbbeab3c7e60a46b07529e98e18536d7` |

YouTube is a trademark of Google LLC. Tube360 is an independent project, not affiliated with or endorsed by Google or Microsoft.

## SponsorBlock data

Segments: [SponsorBlock, sponsor.ajay.app](https://sponsor.ajay.app/), contributed by the SponsorBlock
community, under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/)
([API/database license](https://github.com/ajayyy/SponsorBlock/wiki/Database-and-API-License)).
Tube360 is a noncommercial project. This data license applies separately from the MIT application code.
The unmodified recorded API response in `bench/sponsorblock/fixtures/public-prefix.json` is covered by the
same license. See `bench/sponsorblock/README.md` for its origin and collection date.

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
and run `tools/build-ffmpeg-x360.sh xbox` followed by `tools/build-xbox.sh`. To substitute a modified FFmpeg,
install ABI-compatible static libraries and headers into `.work/ffmpeg-xbox/`, remove the build's old linked
executable `client/build-x360/tube360`, and rerun `tools/build-xbox.sh`. CMake relinks and packages the XEX
with the same title ID. The release also includes the unlinked application objects for relinking without
recompiling the application. [FFmpeg licensing](https://ffmpeg.org/legal.html) · [LGPL 2.1, section 6](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html)

The object kit places `main.cpp.obj`, `shaders_bundle.c.obj`, `ffmpeg-libc.c.obj` and `player-fault.c.obj` in `objects/`,
the matching `xsdk.ld`, `libxsdk.a`, `libSDL3.a` and `libvulkan4xenon.a` in `support/`, along with the matching libtube360-yt, libtube360-cast, curl, mbedtls, mbedx509, mbedcrypto, everest and p256m static archives, and the four FFmpeg
archives in `ffmpeg-lib/`. Include the support libraries' licenses. Place `tools/ffmpeg-x360/relink.sh` at the
kit root. With the matching xsdk and GCC installed, run `FFMPEG_LIB=/path/to/modified/lib ./relink.sh`.

See [release preparation](docs/RELEASE.md) for the current object/archive inventory and host-path
sanitization. The app ZIP alone is not the complete source/relink distribution described above.

## Signed updates

- **Monocypher 4.0.3** (`client/third_party/monocypher/`), by Loup Vaillant et al., BSD-2-Clause OR CC0-1.0; upstream sources unmodified, license included. Only Ed25519 verification is called by Tube360. [Source and manual](https://monocypher.org/manual/ed25519), [Cure53 audit](https://monocypher.org/quality-assurance/audit). Archive SHA-512: `40904ada5c7ee4f7741733e38b69a30a4b0561cbffba5ffe7c2dce16136d540251ec0d9056ff606510d3b5b708fb8a40db7e0870d4a0b2dc17ba2bfb880f8965` (`https://monocypher.org/download/monocypher-4.0.3.tar.gz`, checked against upstream `.sha512`).
- **Mozilla CA certificate bundle**, extracted by curl (`client/src/update/roots.pem`), downloaded from [curl CA Extract](https://curl.se/docs/caextract.html) on 2026-11-24; source/license headers preserved. Used only for signed update downloads; YouTube retains its existing GTS roots.

The desktop startup emergency screen includes a Latin subset of X11 misc-fixed 10x20,
by Markus Kuhn and contributors (public domain), from xsdk's `assets/fonts/10x20.bdf`.
The Xbox screen uses xsdk's built-in copy of that font.

The title also statically links GNU FriBidi 1.0.16 (LGPL-2.1-or-later). Include its vendored corresponding source/configuration, `client/text.cmake`, COPYING and the matching `libtube360-text.a` in the release source/relink materials. Replace that archive in `support/` to relink a modified FriBidi with the supplied application objects. The same modification/reverse-engineering permissions above apply.
