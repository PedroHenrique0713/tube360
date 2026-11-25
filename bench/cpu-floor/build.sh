#!/usr/bin/env bash
set -euo pipefail
bench=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd "$bench/../.." && pwd)
target=${1:-pc}
[[ $target == pc || $target == xbox ]] || { echo "usage: $0 [pc|xbox]" >&2; exit 2; }
# O script do produto roda sem alterações numa raiz privada; nunca sobrescreve as libs do app.
stage="$bench/.work/app-build"
mkdir -p "$stage/tools" "$stage/.work/src" "$bench/results"
cp "$root/tools/build-ffmpeg-x360.sh" "$stage/tools/"
cp -a "$root/tools/ffmpeg-x360" "$stage/tools/"
archive=${FFMPEG_ARCHIVE:-$root/.work/src/ffmpeg-7.1.5.tar.xz}
if [[ -f $archive ]]; then cp --reflink=auto "$archive" "$stage/.work/src/"; fi
export TUBE360_BUILD_ENV=/dev/null
export PATH="/home/pedrohenriquequadro/Documentos/VS/tube360-frota/f5-h264/.work/src/usr/bin:$PATH"
src="$stage/.work/src/ffmpeg-7.1.5"
[[ $target == pc ]] || src+="-xbox"
compiler=${CC:-cc}
[[ $target != xbox ]] || compiler="${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}/bin/powerpc-xsdk-elf-gcc"
base_stamp=$(python3 "$root/tools/ffmpeg-x360/source-id.py" --cc "$compiler")
patch_stamp=$(cat "$bench/prepare.py" "$bench/f30_stats.h" | sha256sum | cut -d' ' -f1)
prefix="$stage/.work/ffmpeg-$target"
if [[ ! -f "$prefix/.f30-base" || $(cat "$prefix/.f30-base") != "$base_stamp" ]]; then
    # Primeiro compila exatamente a configuração LGPL/VMX do app, depois instrumenta e relinka.
    if [[ $target == pc && -d $src ]]; then rm -rf -- "$src"; fi
    bash "$stage/tools/build-ffmpeg-x360.sh" "$target" > "$bench/results/build-ffmpeg-$target.txt" 2>&1
    echo "$base_stamp" > "$prefix/.f30-base"
    rm -f -- "$prefix/.f30-built"
fi
if [[ ! -f "$prefix/.f30-built" || $(cat "$prefix/.f30-built") != "$patch_stamp" ]]; then
    # Só estes quatro arquivos são instrumentados; preserva as mudanças VMX do app.
    for file in h264dec.h h264dec.c h264_mb.c h264_slice.c; do
        tar -xOf "$stage/.work/src/ffmpeg-7.1.5.tar.xz" "ffmpeg-7.1.5/libavcodec/$file" > "$src/libavcodec/$file"
    done
    rm -f -- "$src/libavcodec/f30_stats.h"
    python3 "$bench/prepare.py" "$src"
    make_args=()
    if [[ $target == xbox ]]; then
        for unit in h264chroma_init idctdsp me_cmp audiodsp; do
            make_args+=("--eval=libavcodec/ppc/$unit.o: CFLAGS += -include $stage/tools/ffmpeg-x360/scalar-unit.h")
        done
    fi
    make -C "$stage/.work/build-$target" "${make_args[@]}" -j"${JOBS:-3}" install >> "$bench/results/build-ffmpeg-$target.txt" 2>&1
    if [[ $target == xbox ]]; then
        python3 "$stage/tools/ffmpeg-x360/check-vmx.py" "${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}/bin/powerpc-xsdk-elf-objdump" \
            "$stage/.work/build-$target" >> "$bench/results/build-ffmpeg-$target.txt" 2>&1
    fi
    # Snapshots de evidência sem espaços ao fim; os arquivos usados no build ficam intactos.
    sed 's/[[:blank:]]*$//' "$stage/.work/build-$target/config.h" > "$bench/results/config-$target.h.txt"
    sed 's/[[:blank:]]*$//' "$stage/.work/build-$target/ffbuild/config.mak" > "$bench/results/config-$target.mak.txt"
    echo "$patch_stamp" > "$prefix/.f30-built"
fi
flags=()
if [[ $target == xbox ]]; then
    : "${XSDK_DIR:?Set XSDK_DIR explicitly to the built SDK}"
    flags+=(-DCMAKE_TOOLCHAIN_FILE="$XSDK_DIR/cmake/xsdk.cmake" -DXSDK_TOOLCHAIN="${XSDK_TOOLCHAIN:-$HOME/xsdk-gcc}")
    if [[ -f "$bench/build-$target/CMakeCache.txt" ]] && ! grep -Fq "CMAKE_C_COMPILER:FILEPATH=$compiler" "$bench/build-$target/CMakeCache.txt"; then
        flags+=(--fresh)
    fi
fi
cmake -S "$bench" -B "$bench/build-$target" -DCMAKE_BUILD_TYPE=Release \
    -DFFMPEG_PREFIX="$stage/.work/ffmpeg-$target" "${flags[@]}"
cmake --build "$bench/build-$target" -j"${JOBS:-3}"
