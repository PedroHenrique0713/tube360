#!/usr/bin/env python3
"""Instrument only the private FFmpeg 7.1.5 tree, with exact source anchors."""
import argparse
from pathlib import Path
import shutil


def replace(root, file, old, new):
    path = root / "libavcodec" / file
    data = path.read_text()
    if data.count(old) != 1:
        raise RuntimeError(f"{file}: expected one anchor, found {data.count(old)}: {old[:80]}")
    path.write_text(data.replace(old, new, 1))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    root = args.source
    if (root / "libavcodec/f30_stats.h").exists():
        raise RuntimeError("F30 already applied; recreate the private source tree")
    shutil.copyfile(Path(__file__).with_name("f30_stats.h"), root / "libavcodec/f30_stats.h")
    replace(root, "h264dec.h", "    int nal_length_size;", "    int f30_entropy_only, f30_audit;\n    int nal_length_size;")
    replace(root, "h264dec.c", "static const AVOption h264_options[] = {", """static const AVOption h264_options[] = {
    { "f30_entropy_only", "F30: omit pixel reconstruction (invalid pictures)", OFFSET(f30_entropy_only), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, VD },
    { "f30_audit", "F30: hash parsed syntax, outside timing runs", OFFSET(f30_audit), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, VD },""")
    replace(root, "h264_slice.c", "    // SPS/PPS\n", "    h->f30_entropy_only = h1->f30_entropy_only;\n    h->f30_audit = h1->f30_audit;\n\n    // SPS/PPS\n")
    replace(root, "h264_slice.c", "    if (h->postpone_filter)\n        return;", "    if (h->postpone_filter || h->f30_entropy_only)\n        return;")
    # Progresso por linha continua sendo necessário aos MVs colocados dos B-frames.
    replace(root, "h264_slice.c", "    ff_h264_draw_horiz_band(h, sl, top, height);", "    if (!h->f30_entropy_only)\n        ff_h264_draw_horiz_band(h, sl, top, height);")
    replace(root, "h264_slice.c", "static int decode_slice(struct AVCodecContext *avctx, void *arg)", "static int f30_decode_slice_inner(struct AVCodecContext *avctx, void *arg)")
    replace(root, "h264_slice.c", "/**\n * Call decode_slice() for each context.", """#include "libavutil/time.h"
#include "f30_stats.h"
static int decode_slice(struct AVCodecContext *avctx, void *arg)
{
    H264SliceContext *sl = arg;
    const H264Context *h = sl->h264;
    AVBufferRef *ref = h->cur_pic.f->opaque_ref;
    int64_t start = av_gettime_relative();
    int ret = f30_decode_slice_inner(avctx, arg);
    if (ref && ref->size == sizeof(F30Stats)) {
        F30Stats *stats = (F30Stats *)ref->data;
        stats->slice_ns += (av_gettime_relative() - start) * UINT64_C(1000);
        stats->slices++;
    }
    return ret;
}

/**
 * Call decode_slice() for each context.""")
    # Tudo o que alimenta o próximo MB é produzido por CABAC/CAVLC antes daqui.
    replace(root, "h264_mb.c", "void ff_h264_hl_decode_mb(const H264Context *h, H264SliceContext *sl)\n{", r'''#include "f30_stats.h"

static uint64_t f30_mix(uint64_t hash, unsigned value)
{
    /* Inteiros, sem depender da ordem de bytes PC/Xenon. Só na rodada de auditoria. */
    return (hash ^ value) * UINT64_C(1099511628211);
}

static void f30_mb_stats(const H264Context *h, H264SliceContext *sl)
{
    AVBufferRef *ref = h->cur_pic.f->opaque_ref;
    F30Stats *stats;
    uint64_t hash;
    unsigned type;
    int i, list;
    if (!ref || ref->size != sizeof(F30Stats))
        return;
    stats = (F30Stats *)ref->data;
    stats->parsed++;
    if (!h->f30_entropy_only)
        stats->reconstructed++;
    if (!h->f30_audit)
        return;
    hash = stats->syntax_hash;
    type = h->cur_pic.mb_type[sl->mb_xy];
    hash = f30_mix(hash, sl->mb_xy);
    hash = f30_mix(hash, type);
    hash = f30_mix(hash, h->cur_pic.qscale_table[sl->mb_xy]);
    stats->syntax_hash = hash;
    hash = stats->nnz_hash;
    for (i = 0; i < 24; i++) {
        int n = i < 16 ? i : i < 20 ? 16 + (i-16)%2 + 4*((i-16)/2) : 32 + (i-20)%2 + 4*((i-20)/2);
        hash = f30_mix(hash, h->non_zero_count[sl->mb_xy][n]);
    }
    stats->nnz_hash = hash;
    hash = stats->coeff_hash;
    if (!IS_INTRA_PCM(type) && !IS_SKIP(type)) {
        /* Layout FFmpeg: Y [0,256), Cb [256,320), Cr [512,576). */
        for (i = 0; i < 768; i++)
            if (i < 320 || (i >= 512 && i < 576))
                hash = f30_mix(hash, (uint16_t)sl->mb[i]);
        if (IS_INTRA16x16(type))
            for (i = 0; i < 16; i++)
                hash = f30_mix(hash, (uint16_t)sl->mb_luma_dc[0][i]);
    }
    if (IS_INTRA_PCM(type))
        for (i = 0; i < 384; i++)
            hash = f30_mix(hash, sl->intra_pcm_ptr[i]);
    stats->coeff_hash = hash;
    hash = stats->prediction_hash;
    if (IS_INTRA(type) && !IS_INTRA_PCM(type)) {
        hash = f30_mix(hash, sl->chroma_pred_mode);
        if (IS_INTRA16x16(type))
            hash = f30_mix(hash, sl->intra16x16_pred_mode);
        if (IS_INTRA4x4(type))
            for (i = 0; i < 16; i++)
                hash = f30_mix(hash, sl->intra4x4_pred_mode_cache[scan8[i]]);
    } else if (!IS_INTRA(type)) {
        for (list = 0; list < sl->list_count; list++) {
            if (!USES_LIST(type, list))
                continue;
            for (i = 0; i < 16; i++) {
                int n = scan8[i];
                hash = f30_mix(hash, (uint8_t)sl->ref_cache[list][n]);
                if (sl->ref_cache[list][n] >= 0) {
                    hash = f30_mix(hash, (uint16_t)sl->mv_cache[list][n][0]);
                    hash = f30_mix(hash, (uint16_t)sl->mv_cache[list][n][1]);
                }
            }
        }
    }
    stats->prediction_hash = hash;
}

void ff_h264_hl_decode_mb(const H264Context *h, H264SliceContext *sl)
{
    f30_mb_stats(h, sl);
    if (h->f30_entropy_only) {
        h->list_counts[sl->mb_xy] = sl->list_count;
        /* A inversa normalmente zera esses coeficientes. Não deixar lixo para o próximo MB. */
        memset(sl->mb, 0, sizeof(sl->mb));
        return;
    }'''.replace(r'\"', '"'))
    print(f"F30 patched {root}")


if __name__ == "__main__":
    main()
