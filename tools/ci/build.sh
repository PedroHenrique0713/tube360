#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
# Imagem pública, sem configuração nem diretório de trabalho de outra máquina.
export TUBE360_BUILD_ENV=/dev/null JOBS=2
mkdir -p .work/src
cp -a /opt/devkit/.deps/tube360/.work/src/mbedtls-3.6.8* .work/src/
cp -a /opt/devkit/.deps/tube360/.work/src/curl-xenon* .work/src/
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j2
tools/ci/checks.sh
export FFMPEG_PREFIX=/opt/devkit/.deps/tube360/.work/ffmpeg-xbox
tools/build-xbox.sh
