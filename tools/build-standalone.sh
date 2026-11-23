#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
src="$root/.work/src"
mkdir -p "$src"
target=${1:-pc}
args=()
case "$target" in
  xbox)
    env_file=${TUBE360_BUILD_ENV:-$HOME/.config/tube360/build.env}
    [[ ! -f "$env_file" ]] || . "$env_file"
    args+=("-DCMAKE_TOOLCHAIN_FILE=${XSDK_DIR:?}/cmake/xsdk.cmake" "-DXSDK_TOOLCHAIN=${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}") ;;
  pc|sources) ;;
  *) echo "usage: $0 [pc|xbox]" >&2; exit 2 ;;
esac
fetch() (
    local archive=$1 url=$2 digest=$3 folder=$4
    exec 8>"$src/.fetch.lock"
    flock 8
    if [[ ! -f "$src/$archive" ]]; then
        curl -fL "$url" -o "$src/$archive.part"
        mv "$src/$archive.part" "$src/$archive"
    fi
    echo "$digest  $src/$archive" | sha256sum -c -
    if [[ ! -d "$src/$folder" ]]; then
        local stage
        stage=$(mktemp -d "$src/.extract.XXXXXX")
        trap 'rm -rf -- "$stage"' EXIT
        tar xf "$src/$archive" -C "$stage"
        mv "$stage/$folder" "$src/$folder"
    fi
)
mbed=mbedtls-3.6.8
rev=a6b70bd06820e1c625f47fe0e592e08c5daaa6b4
fetch "$mbed.tar.bz2" "https://github.com/Mbed-TLS/mbedtls/releases/download/$mbed/$mbed.tar.bz2" bc54b818e87f49c0b3281dd85d24adaede0e63b4e822e3fbd49c005a38b9595b "$mbed"
fetch curl-xenon.tar.gz "https://codeload.github.com/xenosworks/curl-xenon/tar.gz/$rev" 154acc3d1e2906c18822a4ce7c061c63242325e7090ab682074cafc7f7314509 "curl-xenon-$rev"
[[ "$target" != sources ]] || exit 0
build="$root/.work/standalone-$target"
cmake -S "$root/bench/standalone" -B "$build" -DCMAKE_BUILD_TYPE=Release \
    "-DMBEDTLS_SOURCE=$src/$mbed" "-DCURL_SOURCE=$src/curl-xenon-$rev" "${args[@]}"
cmake --build "$build" --parallel "${JOBS:-3}" --target yt-check
if [[ "$target" == pc ]]; then
    cmake --build "$build" --parallel "${JOBS:-3}" --target yt-test
    ctest --test-dir "$build" --output-on-failure
fi
