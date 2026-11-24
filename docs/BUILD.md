# Building Tube360

Run builds from the repository root. Development and Xbox build directories are separate. Do not copy a
working console's configuration or account files into build outputs.

## Xbox prerequisites

- A built [xsdk](https://github.com/xenosworks/xsdk), its PowerPC GCC/newlib toolchain,
  [vulkan4xenon](https://github.com/xenosworks/vulkan4xenon) and
  [SDL-xenon](https://github.com/xenosworks/SDL-xenon).
- CMake 3.25+, Make, Python 3, curl, tar, patch, `flock` and `glslangValidator` on the host.
- Internet access on the first build for the pinned FFmpeg, Mbed TLS and curl-xenon archives. The scripts
  verify their SHA-256 digests; sources and build products are under `.work/`.

Set the paths in your environment or in `~/.config/tube360/build.env` (never commit the machine-specific file):

```bash
export XSDK_DIR=~/src/xsdk
export V4X_DIR=~/src/vulkan4xenon
export SDL3_XENON_DIR=~/src/SDL-xenon/build-xsdk
export XSDK_TOOLCHAIN=~/xsdk-gcc
tools/build-xbox.sh
```

`TUBE360_BUILD_ENV` selects another environment file. `APP_NAME` changes the displayed title, default Tube360.
The build prepares private xsdk/vulkan4xenon copies in `.work/native-display`, including the native display
and video-memory changes. Keep those copies with the matching build for source/relink preparation.
The release title ID is `0x58545542`; the XEX metadata version is currently `1.0.0.0`, separate from the
distribution version in the ZIP name. Do not infer a public release number from that metadata.

The result is `client/build-x360/tube360.xex`; the packager installs it as `default.xex`. The experimental `tube360-launcher.xex` is not shipped in v0.3.0.
Set `TUBE360_BUILD_ID=1 TUBE360_VERSION=0.3.0` for this initial signed release. See [signed updates](UPDATES.md).

```bash
python3 tools/package-xbox.py --version 0.3.0
```

For concurrent worktrees on a shared host, serialize the build and cap memory. The shared development lock is
`~/.cache/tube360-frota-xbox.lock`:

```bash
flock ~/.cache/tube360-frota-xbox.lock systemd-run --user --scope -q \
  -p MemoryMax=6G -p MemorySwapMax=1G tools/build-xbox.sh
```

This starts a transient scope only; it does not restart any running Tube360 service or deploy to a console.

## Desktop development

Install SDL3 development files, Vulkan headers/loader, a Vulkan driver, `glslangValidator`, CMake, a C++20
compiler and Python 3. FFmpeg 7 development libraries are used when available; otherwise the build creates
the pinned minimal static FFmpeg under `.work/ffmpeg-pc`. YouTube TLS dependencies are fetched by the same
source-preparation script as the Xbox build. The local x86 FFmpeg build also needs NASM/Yasm; install NASM
before configuring if no suitable system FFmpeg development package is available.

```bash
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j3
XT_ASSETS="$PWD/client/assets" ./client/build/tube360
```

For tests, copy fonts to a dedicated writable assets folder and use an isolated HOME/config, account fixtures
and loopback receivers. `XT_CAST_DISABLE=1` disables phone discovery in unrelated desktop tests.
See [quality checks](../bench/quality/README.md) and [native architecture](STANDALONE.md).
