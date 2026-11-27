#!/usr/bin/env bash
# Executar na pasta do kit do release. FFMPEG_LIB pode apontar às quatro bibliotecas recompiladas.
set -euo pipefail
cd -- "$(dirname -- "$0")"
: "${XSDK_TOOLCHAIN:?set the matching Xbox GCC prefix}"
: "${XSDK_DIR:?set the xsdk checkout containing tools/elf2xex.py}"
av="${FFMPEG_LIB:-ffmpeg-lib}"
"$XSDK_TOOLCHAIN/bin/powerpc-xsdk-elf-g++" -mcpu=cell -fno-pic -static -nostdlib -T support/xsdk.ld \
  -Wl,--wrap=pthread_create objects/main.cpp.obj objects/shaders_bundle.c.obj objects/ffmpeg-libc.c.obj objects/player-fault.c.obj \
  -o tube360.elf -Wl,--start-group support/libvulkan4xenon.a support/libSDL3.a \
  "$av/libavformat.a" "$av/libavcodec.a" "$av/libswresample.a" "$av/libavutil.a" \
  support/libtube360-text.a support/libtube360-update.a support/libtube360-update-core.a support/libtube360-yt.a support/libtube360-cast.a support/libcurl.a support/libmbedtls.a support/libmbedx509.a support/libmbedcrypto.a \
  support/libeverest.a support/libp256m.a support/libquickjs-ng.a -lm -lstdc++ support/libxsdk.a -lc -lm -lgcc -Wl,--end-group
python3 "$XSDK_DIR/tools/elf2xex.py" --title-id 0x58545542 --title-name "${APP_NAME:-Tube360}" \
  --version 1.0.0.0 tube360.elf tube360.xex

"$XSDK_TOOLCHAIN/bin/powerpc-xsdk-elf-g++" -mcpu=cell -fno-pic -static -nostdlib -T support/xsdk.ld \
  objects/launcher.cpp.obj -o tube360-launcher.elf -Wl,--start-group support/libtube360-update-core.a \
  support/libmbedcrypto.a support/libeverest.a support/libp256m.a -lm -lstdc++ support/libxsdk.a -lc -lm -lgcc -Wl,--end-group
python3 "$XSDK_DIR/tools/elf2xex.py" --title-id 0x58545542 --title-name Tube360 \
  --version 1.0.0.0 tube360-launcher.elf default.xex
