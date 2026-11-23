#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
env_file=${TUBE360_BUILD_ENV:-$HOME/.config/tube360/build.env}
[[ ! -f "$env_file" ]] || . "$env_file"
version=3.6.8
name=mbedtls-$version
archive="$root/.work/src/$name.tar.bz2"
mkdir -p "$root/.work/src"
if [[ ! -f "$archive" ]]; then
    curl -fL --retry 2 "https://github.com/Mbed-TLS/mbedtls/releases/download/$name/$name.tar.bz2" -o "$archive.part"
    mv "$archive.part" "$archive"
fi
echo "bc54b818e87f49c0b3281dd85d24adaede0e63b4e822e3fbd49c005a38b9595b  $archive" | sha256sum -c -
tar -xf "$archive" -C "$root/.work/src"
cmake -S "$root/tools/mbedtls-x360" -B "$root/.work/mbedtls-xbox" \
    -DCMAKE_TOOLCHAIN_FILE="${XSDK_DIR:?}/cmake/xsdk.cmake" -DXSDK_TOOLCHAIN="${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}" \
    -DMBEDTLS_SOURCE="$root/.work/src/$name" -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/.work/mbedtls-xbox" --target mbedtls -j"${JOBS:-3}"
