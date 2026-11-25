/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Tube360, 2026-11-24: Xenon-safe extensions to FFmpeg's H.264 DSP dispatch.
 */
#include "config.h"
#include "libavutil/cpu.h"
#include "libavcodec/h264chroma.h"
#include "libavcodec/h264qpel.h"
#include "libavcodec/h264dsp.h"
#include "h264-vmx128.h"

void ff_tube360_set_vmx(unsigned mask);
void ff_tube360_chroma_init(H264ChromaContext *c, int depth);
void ff_tube360_qpel_init(H264QpelContext *c, int depth);
void ff_tube360_weight_init(H264DSPContext *c, int depth);

/* Sonda A/B chama somente antes de criar workers; o app usa os três estágios. */
static unsigned tube_vmx_mask = 7;
void ff_tube360_set_vmx(unsigned mask) { tube_vmx_mask = mask & 7; }

void ff_tube360_chroma_init(H264ChromaContext *c, int depth)
{
    if (!(tube_vmx_mask & 1) || depth != 8 || !(av_get_cpu_flags() & AV_CPU_FLAG_ALTIVEC)) return;
    h264_chroma_mc_func put[] = {tube_put_c8,tube_put_c4,tube_put_c2};
    h264_chroma_mc_func avg[] = {tube_avg_c8,tube_avg_c4,tube_avg_c2};
    for (int i=0;i<3;++i) {
        c->put_h264_chroma_pixels_tab[i]=put[i];
        c->avg_h264_chroma_pixels_tab[i]=avg[i];
    }
}
void ff_tube360_qpel_init(H264QpelContext *c, int depth)
{
    if (!(tube_vmx_mask & 2) || depth != 8 || !(av_get_cpu_flags() & AV_CPU_FLAG_ALTIVEC)) return;
    const qpel_mc_func put[3][16] = {TUBE_QTABLE(put,8),TUBE_QTABLE(put,4),TUBE_QTABLE(put,2)};
    const qpel_mc_func avg[3][16] = {TUBE_QTABLE(avg,8),TUBE_QTABLE(avg,4),TUBE_QTABLE(avg,2)};
    for (int i=0;i<3;++i) for (int p=0;p<16;++p) {
        c->put_h264_qpel_pixels_tab[i+1][p]=put[i][p];
        /* Upstream deixa avg de largura 2 sem implementação/uso. */
        if (i<2) c->avg_h264_qpel_pixels_tab[i+1][p]=avg[i][p];
    }
}
void ff_tube360_weight_init(H264DSPContext *c, int depth)
{
    if (!(tube_vmx_mask & 4) || depth != 8 || !(av_get_cpu_flags() & AV_CPU_FLAG_ALTIVEC)) return;
    h264_weight_func w[] = {tube_w16,tube_w8,tube_w4,tube_w2};
    h264_biweight_func b[] = {tube_b16,tube_b8,tube_b4,tube_b2};
    for (int i=0;i<4;++i) {
        c->weight_h264_pixels_tab[i]=w[i];
        c->biweight_h264_pixels_tab[i]=b[i];
    }
}
