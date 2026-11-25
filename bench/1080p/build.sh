#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
target=${1:-pc}
# A instrumentação F30 continua privada; nunca entra nas bibliotecas do app.
bash "$root/bench/cpu-floor/build.sh" "$target"
stage="$root/bench/cpu-floor/.work/app-build/.work"
flags=()
src="$stage/src/ffmpeg-7.1.5"
if [[ $target == xbox ]]; then
    src+="-xbox"
    flags+=(-DCMAKE_TOOLCHAIN_FILE="${XSDK_DIR:?}/cmake/xsdk.cmake" -DXSDK_TOOLCHAIN="${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}")
    if [[ -f "$root/bench/1080p/build-$target/CMakeCache.txt" ]] && ! grep -Fq "CMAKE_C_COMPILER:FILEPATH=${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}/bin/powerpc-xsdk-elf-gcc" "$root/bench/1080p/build-$target/CMakeCache.txt"; then
        flags+=(--fresh)
    fi
fi
cmake -S "$root/bench/1080p" -B "$root/bench/1080p/build-$target" -DCMAKE_BUILD_TYPE=Release \
    -DFFMPEG_PREFIX="$stage/ffmpeg-$target" -DFFMPEG_SOURCE="$src" -DFFMPEG_BUILD="$stage/build-$target" "${flags[@]}"
cmake --build "$root/bench/1080p/build-$target" -j"${JOBS:-3}"
