#!/usr/bin/env bash
# Bancada do desafio `n` (QuickJS + redutor do player). `pc` roda no host; `xbox` só compila, para medir o XEX.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
target=${1:-pc}
args=()
case "$target" in
  xbox)
    env_file=${TUBE360_BUILD_ENV:-$HOME/.config/tube360/build.env}
    [[ ! -f "$env_file" ]] || . "$env_file"
    args+=("-DCMAKE_TOOLCHAIN_FILE=${XSDK_DIR:?}/cmake/xsdk.cmake" "-DXSDK_TOOLCHAIN=${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}") ;;
  pc) ;;
  *) echo "usage: $0 [pc|xbox]" >&2; exit 2 ;;
esac
build=${NSIG_BUILD_DIR:-$root/.work/nsig-$target}
cmake -S "$root/bench/nsig" -B "$build" -DCMAKE_BUILD_TYPE=Release "${args[@]}"
cmake --build "$build" --parallel "${JOBS:-3}"
