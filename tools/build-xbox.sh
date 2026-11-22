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
python3 "$V4X_DIR/tools/build_shaders.py" \
  --xost "$XSDK_DIR/xenos-toolchain" --glslang "$(command -v glslangValidator)" \
  --optimize --output x360/shaders_bundle shaders/*.vert shaders/*.frag
cmake -S x360 -B build-x360 -DV4X_DIR="$V4X_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$XSDK_DIR/cmake/xsdk.cmake" -DXSDK_TOOLCHAIN="${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}" \
  -DSDL3_DIR="$SDL3_XENON_DIR" -DAPP_NAME="${APP_NAME:-Tube360}" -DCMAKE_BUILD_TYPE=Release
cmake --build build-x360 -j"$(nproc)"
sha256sum build-x360/tube360.xex
