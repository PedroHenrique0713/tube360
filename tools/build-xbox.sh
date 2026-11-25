#!/usr/bin/env bash
# Compila o Tube360 para o Xbox 360. Precisa do xsdk, do vulkan4xenon e do SDL-xenon já compilados (docs/BUILD.md).
# Caminhos por variável de ambiente ou em ~/.config/tube360/build.env:
#   XSDK_DIR        checkout do xsdk (github.com/xenosworks/xsdk), compilado
#   V4X_DIR         checkout do vulkan4xenon, compilado (build/libvulkan4xenon.a)
#   SDL3_XENON_DIR  pasta de build do SDL-xenon com o SDL3Config.cmake
#   XSDK_TOOLCHAIN  prefixo do GCC do xsdk (padrão ~/xsdk-gcc)
#   APP_NAME        nome que o Aurora mostra (padrão Tube360)
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
env_file=${TUBE360_BUILD_ENV:-$HOME/.config/tube360/build.env}
[ -f "$env_file" ] && . "$env_file"
: "${XSDK_DIR:?defina XSDK_DIR (veja docs/BUILD.md)}"
: "${V4X_DIR:?defina V4X_DIR (veja docs/BUILD.md)}"
: "${SDL3_XENON_DIR:?defina SDL3_XENON_DIR (veja docs/BUILD.md)}"
cd "$root/client"
python3 "$root/tools/prepare-native-display.py" "$XSDK_DIR" "$V4X_DIR"
XSDK_DIR="$root/.work/native-display/xsdk"
V4X_DIR="$root/.work/native-display/vulkan4xenon"
python3 "$root/tools/prepare-video-memory.py" "$V4X_DIR"
python3 "$root/tools/prepare-boot-diagnostics.py" "$XSDK_DIR" "$V4X_DIR"
python3 "$root/tools/prepare-video-policy.py" "$XSDK_DIR" "$V4X_DIR"
export XSDK_DIR V4X_DIR
make -C "$XSDK_DIR" -j3 "TOOLCHAIN=${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}/bin/powerpc-xsdk-elf-" build/libxsdk.a build/host/libxsc.so
make -C "$V4X_DIR" -j3 "TOOLCHAIN=${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}/bin/powerpc-xsdk-elf-" "XSDK=$XSDK_DIR" build/libvulkan4xenon.a
python3 "$V4X_DIR/tools/build_shaders.py" \
  --xost "$XSDK_DIR/xenos-toolchain" --glslang "$(command -v glslangValidator)" \
  --optimize --output x360/shaders_bundle shaders/*.vert shaders/*.frag
fresh=()
if [[ -f build-x360/CMakeCache.txt ]] && ! grep -Fq "CMAKE_TOOLCHAIN_FILE:FILEPATH=$XSDK_DIR/cmake/xsdk.cmake" build-x360/CMakeCache.txt; then fresh+=(--fresh); fi
cmake "${fresh[@]}" -S x360 -B build-x360 -DV4X_DIR="$V4X_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$XSDK_DIR/cmake/xsdk.cmake" -DXSDK_TOOLCHAIN="${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}" \
  -DSDL3_DIR="$SDL3_XENON_DIR" -DAPP_NAME="${APP_NAME:-Tube360}" -DCMAKE_BUILD_TYPE=Release -DTUBE360_BUILD_ID="${TUBE360_BUILD_ID:-1}" -DTUBE360_VERSION="${TUBE360_VERSION:-0.3.0-dev}" \
  -DTUBE360_F33_BENCH="${TUBE360_F33_BENCH:-OFF}" -DTUBE360_F40_BENCH="${TUBE360_F40_BENCH:-OFF}"
cmake --build build-x360 -j"${JOBS:-3}"
sha256sum build-x360/tube360.xex build-x360/tube360-launcher.xex
