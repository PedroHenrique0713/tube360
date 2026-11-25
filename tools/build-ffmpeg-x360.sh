#!/usr/bin/env bash
# Bibliotecas usadas pelo experimento local. Os builds de PC e Xbox ficam isolados.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
target=${1:-xbox}
version=7.1.5
digest=de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f
work="$root/.work"
archive="$work/src/ffmpeg-$version.tar.xz"
src="${archive%.tar.xz}"
mkdir -p "$work/src" "$work/build-$target"
if [[ ! -f "$archive" ]]; then
    curl -fL --retry 2 "https://ffmpeg.org/releases/ffmpeg-$version.tar.xz" -o "$archive.part"
    mv "$archive.part" "$archive"
fi
echo "$digest  $archive" | sha256sum -c -
[[ -d "$src" ]] || tar -C "$work/src" -xf "$archive"
flags=()
make_args=()
case "$target" in
    xbox|scalar)
        src="$work/src/ffmpeg-$version-$target"
        mkdir -p "$src"
        # Cópia própria: o PC continua usando a árvore oficial.
        tar -C "$src" --strip-components=1 -xf "$archive"
        patch -s -d "$src" -p1 < "$root/tools/ffmpeg-x360/h264dsp.patch"
        if [[ $target == xbox ]]; then python3 "$root/tools/ffmpeg-x360/prepare-vmx.py" "$src"; fi
        env_file=${TUBE360_BUILD_ENV:-$HOME/.config/tube360/build.env}
        [[ ! -f "$env_file" ]] || . "$env_file"
        prefix="${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}/bin/powerpc-xsdk-elf-"
        cflags="-O3 -mcpu=cell -mtune=cell -fno-pic -fno-tree-vectorize -I${XSDK_DIR:?}/include"
        flags+=(--enable-cross-compile --target-os=none --arch=ppc --cpu=generic
            --cross-prefix="$prefix" --disable-vsx --disable-power8 --disable-pic
            --extra-ldflags="-static -nostdlib -T $XSDK_DIR/src/xsdk.ld"
            --extra-libs="-Wl,--start-group $XSDK_DIR/build/libxsdk.a -lc -lm -lgcc -Wl,--end-group")
        if [[ $target == scalar ]]; then
            flags+=(--disable-altivec)
            cflags+=" -mno-altivec"
        else
            flags+=(--enable-altivec)
            units="h264chroma_init idctdsp me_cmp audiodsp"
            for unit in $units; do
                make_args+=("--eval=libavcodec/ppc/$unit.o: CFLAGS += -include $root/tools/ffmpeg-x360/scalar-unit.h")
            done
        fi
        flags+=(--extra-cflags="$cflags")
        ;;
    pc) ;;
    *) echo "usage: $0 [xbox|scalar|pc]" >&2; exit 2 ;;
esac
compiler=${prefix:-}gcc
[[ $target != pc ]] || compiler=${CC:-cc}
compiler_id=$(python3 "$root/tools/ffmpeg-x360/source-id.py" --cc "$compiler" --compiler-only)
# Make não invalida objetos quando só CC muda (por exemplo, GCC do milestone 52).
if [[ ! -f "$work/build-$target/.tube360-compiler-id" || $(cat "$work/build-$target/.tube360-compiler-id") != "$compiler_id" ]]; then
    rm -rf -- "$work/build-$target"
    mkdir -p "$work/build-$target"
fi
cd "$work/build-$target"
ln -sfnT "$src" src
"$src/configure" --prefix="$work/ffmpeg-$target" --disable-autodetect --disable-everything \
    --enable-static --disable-shared --disable-gpl --disable-version3 --disable-nonfree \
    --disable-network --disable-programs --disable-doc --disable-debug --disable-avdevice \
    --disable-avfilter --disable-swscale --disable-postproc --enable-pthreads \
    --enable-decoder=h264,aac,opus --enable-parser=h264,aac,opus \
    --enable-demuxer=mov,mpegts,hls,matroska --enable-protocol=file "${flags[@]}"
# Configure pode aceitar a opção mas retirar dependências ao detectar o alvo.
for feature in HAVE_PTHREADS CONFIG_H264_DECODER CONFIG_AAC_DECODER CONFIG_OPUS_DECODER CONFIG_MOV_DEMUXER CONFIG_MPEGTS_DEMUXER; do
    grep -qx "#define $feature 1" config.h config_components.h
done
make "${make_args[@]}" -j"${JOBS:-3}" install
if [[ $target == xbox ]]; then python3 "$root/tools/ffmpeg-x360/check-vmx.py" "${prefix}objdump" "$PWD"; fi
printf '%s\n' "$compiler_id" > "$work/build-$target/.tube360-compiler-id"
python3 "$root/tools/ffmpeg-x360/source-id.py" --cc "$compiler" > "$work/ffmpeg-$target/.tube360-source-id"
