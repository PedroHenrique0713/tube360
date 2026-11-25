#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
bench="$root/bench/1080p"
src="${FFMPEG_SRC:-$root/.work/src/ffmpeg-7.1.5-xbox}"
config="${FFMPEG_CONFIG:-$root/.work/build-xbox}"
prefix="${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}/bin/powerpc-xsdk-elf-"
qemu=${QEMU_PPC:-$root/.work/qemu/root/usr/bin/qemu-ppc-static}
mkdir -p "$bench/.work" "$bench/results"
# O GCC xsdk força instruções escalares PPC64 em ELF32. QEMU ppc32 não as
# aceita. Só nesta sonda usa-se o backend PPC32; os intrínsecos VMX são reais.
"${prefix}gcc" -E -DF33_FREESTANDING -I"$config" -I"$src" -I"$root/tools/ffmpeg-x360" \
    "$bench/kernels.c" > "$bench/.work/kernels.i"
cc1=$("${prefix}gcc" -print-prog-name=cc1)
"$cc1" -quiet -O3 -mcpu=cell -mcall-sysv -msdata=none -maltivec -mabi=altivec -mno-powerpc64 -fno-tree-vectorize \
    -fno-pic -fno-tree-loop-distribute-patterns -ffunction-sections -fdata-sections \
    "$bench/.work/kernels.i" -o "$bench/.work/kernels.s"
"${prefix}gcc" -nostdlib -static -Wl,--gc-sections,-e,_start,-Ttext=0x10000000 \
    "$bench/.work/kernels.s" -o "$bench/.work/kernels-ppc"
# Desmonta os mesmos kernels; emulador prova pixels, não a velocidade do Xenon.
"${prefix}objdump" -d "$bench/.work/kernels-ppc" > "$bench/.work/kernels-ppc.asm"
"$qemu" -cpu 7400 "$bench/.work/kernels-ppc"
