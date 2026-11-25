/*
 * Tube360 H.264 8-bit DSP additions, 2026-11-24.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * This file is compiled into FFmpeg, not into the MIT application.
 */
#ifndef TUBE360_H264_VMX128_H
#define TUBE360_H264_VMX128_H
#include <altivec.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef __vector unsigned char tube_u8;
typedef __vector signed short tube_s16;
typedef __vector unsigned short tube_u16;
typedef __vector signed int tube_s32;
typedef __vector unsigned int tube_u32;
#define TUBE_INLINE static inline __attribute__((always_inline))
/* O3 expande FIRs de partições pequenas para >170 KiB: evita pressão no I-cache. */
#define TUBE_KERNEL static __attribute__((optimize("O2")))

TUBE_INLINE tube_u8 tube_load(const uint8_t *p)
{
    return vec_perm(vec_ld(0, p), vec_ld(15, p), vec_lvsl(0, p));
}
/* Só escreve a partição: RMW de 16 bytes correria com o MB vizinho. */
TUBE_INLINE void tube_store(uint8_t *p, tube_u8 v, int w)
{
    uint8_t row[16] __attribute__((aligned(16)));
    vec_st(v, 0, row);
    memcpy(p, row, w);
}
TUBE_INLINE tube_s16 tube_pixels(const uint8_t *p)
{
    return (tube_s16)vec_mergeh(vec_splat_u8(0), tube_load(p));
}
TUBE_INLINE tube_s16 tube_fir16(tube_s16 a, tube_s16 b, tube_s16 c,
                               tube_s16 d, tube_s16 e, tube_s16 f)
{
    tube_s16 cd = vec_add(c, d), be = vec_add(b, e);
    tube_s16 cd5 = vec_add(cd, vec_sl(cd, vec_splat_u16(2)));
    tube_s16 be5 = vec_add(be, vec_sl(be, vec_splat_u16(2)));
    return vec_sub(vec_add(vec_add(a, f), vec_sl(cd5, vec_splat_u16(2))), be5);
}
TUBE_INLINE tube_s32 tube_fir32(tube_s32 a, tube_s32 b, tube_s32 c,
                               tube_s32 d, tube_s32 e, tube_s32 f)
{
    tube_s32 cd = vec_add(c, d), be = vec_add(b, e);
    tube_s32 cd5 = vec_add(cd, vec_sl(cd, vec_splat_u32(2)));
    tube_s32 be5 = vec_add(be, vec_sl(be, vec_splat_u32(2)));
    return vec_sub(vec_add(vec_add(a, f), vec_sl(cd5, vec_splat_u32(2))), be5);
}
TUBE_INLINE tube_s16 tube_horizontal(const uint8_t *p)
{
    return tube_fir16(tube_pixels(p-2), tube_pixels(p-1), tube_pixels(p),
                      tube_pixels(p+1), tube_pixels(p+2), tube_pixels(p+3));
}
TUBE_INLINE tube_u8 tube_half(tube_s16 v)
{
    v = vec_sra(vec_add(v, vec_splats((short)16)), vec_splat_u16(5));
    return vec_packsu(v, v);
}
TUBE_INLINE tube_u8 tube_vertical(const uint8_t *p, ptrdiff_t s)
{
    return tube_half(tube_fir16(tube_pixels(p-2*s), tube_pixels(p-s), tube_pixels(p),
                               tube_pixels(p+s), tube_pixels(p+2*s), tube_pixels(p+3*s)));
}
TUBE_INLINE tube_u8 tube_diagonal(const tube_s16 *rows)
{
    tube_s16 a=rows[0],b=rows[1],c=rows[2],d=rows[3],e=rows[4],f=rows[5];
    /* Sem clipping/arredondamento entre eixos. O segundo FIR exige int32. */
    tube_s32 hi = tube_fir32(vec_unpackh(a), vec_unpackh(b), vec_unpackh(c),
                             vec_unpackh(d), vec_unpackh(e), vec_unpackh(f));
    tube_s32 lo = tube_fir32(vec_unpackl(a), vec_unpackl(b), vec_unpackl(c),
                             vec_unpackl(d), vec_unpackl(e), vec_unpackl(f));
    hi = vec_sra(vec_add(hi, vec_splats(512)), vec_splat_u32(10));
    lo = vec_sra(vec_add(lo, vec_splats(512)), vec_splat_u32(10));
    tube_s16 v = vec_packs(hi, lo);
    return vec_packsu(v, v);
}
TUBE_INLINE void tube_qpel(uint8_t *dst, const uint8_t *src, ptrdiff_t s,
                           int w, int x, int y, int avg)
{
    tube_s16 raw[13];
    if (x && y && (x==2 || y==2))
        for (int row=0;row<w+5;++row) raw[row]=tube_horizontal(src+(row-2)*s);
    for (int row = 0; row < w; ++row, dst += s, src += s) {
        tube_u8 v;
        if (!x && !y) v = tube_load(src);
        else if (!y) {
            v = tube_half(tube_horizontal(src));
            if (x & 1) v = vec_avg(v, tube_load(src + (x == 3)));
        } else if (!x) {
            v = tube_vertical(src, s);
            if (y & 1) v = vec_avg(v, tube_load(src + (y == 3)*s));
        } else if ((x & 1) && (y & 1)) {
            v = vec_avg(tube_half(tube_horizontal(src + (y == 3)*s)),
                        tube_vertical(src + (x == 3), s));
        } else {
            v = tube_diagonal(raw+row);
            if (y & 1) v = vec_avg(v, tube_half(raw[row+2+(y==3)]));
            if (x & 1) v = vec_avg(v, tube_vertical(src + (x == 3), s));
        }
        if (avg) v = vec_avg(v, tube_load(dst));
        tube_store(dst, v, w);
    }
}
#define TUBE_QPEL(W, X, Y) \
TUBE_KERNEL void tube_put_q##W##_##X##Y(uint8_t *d, const uint8_t *s, ptrdiff_t st) \
{ tube_qpel(d,s,st,W,X,Y,0); } \
TUBE_KERNEL void tube_avg_q##W##_##X##Y(uint8_t *d, const uint8_t *s, ptrdiff_t st) \
{ tube_qpel(d,s,st,W,X,Y,1); }
#define TUBE_PHASES(W) \
TUBE_QPEL(W,0,0) TUBE_QPEL(W,1,0) TUBE_QPEL(W,2,0) TUBE_QPEL(W,3,0) \
TUBE_QPEL(W,0,1) TUBE_QPEL(W,1,1) TUBE_QPEL(W,2,1) TUBE_QPEL(W,3,1) \
TUBE_QPEL(W,0,2) TUBE_QPEL(W,1,2) TUBE_QPEL(W,2,2) TUBE_QPEL(W,3,2) \
TUBE_QPEL(W,0,3) TUBE_QPEL(W,1,3) TUBE_QPEL(W,2,3) TUBE_QPEL(W,3,3)
TUBE_PHASES(8)
TUBE_PHASES(4)
TUBE_PHASES(2)
#define TUBE_QTABLE(P,W) {tube_##P##_q##W##_00,tube_##P##_q##W##_10,tube_##P##_q##W##_20,tube_##P##_q##W##_30, \
 tube_##P##_q##W##_01,tube_##P##_q##W##_11,tube_##P##_q##W##_21,tube_##P##_q##W##_31, \
 tube_##P##_q##W##_02,tube_##P##_q##W##_12,tube_##P##_q##W##_22,tube_##P##_q##W##_32, \
 tube_##P##_q##W##_03,tube_##P##_q##W##_13,tube_##P##_q##W##_23,tube_##P##_q##W##_33}

TUBE_INLINE tube_s16 tube_small_mul(tube_s16 a, int k)
{
    tube_s16 r = vec_splat_s16(0);
    if (k & 1) r = a;
    if (k & 2) r = vec_add(r, vec_sl(a, vec_splat_u16(1)));
    if (k & 4) r = vec_add(r, vec_sl(a, vec_splat_u16(2)));
    return r;
}
TUBE_INLINE void tube_chroma(uint8_t *d, const uint8_t *p, ptrdiff_t s,
                             int h, int x, int y, int w, int avg)
{
    for (int row = 0; row < h; ++row, d += s, p += s) {
        tube_s16 a = tube_pixels(p), r;
        if (!x && !y) r = a;
        else {
            tube_s16 top = vec_sl(a, vec_splat_u16(3));
            if (x) top = vec_add(top, tube_small_mul(vec_sub(tube_pixels(p+1), a), x));
            tube_s16 sum = vec_sl(top, vec_splat_u16(3));
            if (y) {
                tube_s16 b = tube_pixels(p+s), bottom = vec_sl(b, vec_splat_u16(3));
                if (x) bottom = vec_add(bottom, tube_small_mul(vec_sub(tube_pixels(p+s+1), b), x));
                sum = vec_add(sum, tube_small_mul(vec_sub(bottom, top), y));
            }
            r = vec_sra(vec_add(sum, vec_splats((short)32)), vec_splat_u16(6));
        }
        tube_u8 v = vec_packsu(r,r);
        if (avg) v = vec_avg(v, tube_load(d));
        tube_store(d,v,w);
    }
}
#define TUBE_CHROMA(W) \
TUBE_KERNEL void tube_put_c##W(uint8_t *d,const uint8_t *s,ptrdiff_t st,int h,int x,int y) \
{ tube_chroma(d,s,st,h,x,y,W,0); } \
TUBE_KERNEL void tube_avg_c##W(uint8_t *d,const uint8_t *s,ptrdiff_t st,int h,int x,int y) \
{ tube_chroma(d,s,st,h,x,y,W,1); }
TUBE_CHROMA(8)
TUBE_CHROMA(4)
TUBE_CHROMA(2)

TUBE_INLINE tube_s16 tube_weight_mul(tube_s16 a, int k)
{
    int n = k < 0 ? -k : k;
    tube_s16 r = tube_small_mul(a,n);
    if (n & 8) r = vec_add(r,vec_sl(a,vec_splat_u16(3)));
    if (n & 16) r = vec_add(r,vec_sl(a,vec_splats((unsigned short)4)));
    if (n & 32) r = vec_add(r,vec_sl(a,vec_splats((unsigned short)5)));
    if (n & 64) r = vec_add(r,vec_sl(a,vec_splats((unsigned short)6)));
    if (n & 128) r = vec_add(r,vec_sl(a,vec_splats((unsigned short)7)));
    return k < 0 ? vec_sub(vec_splat_s16(0),r) : r;
}
TUBE_INLINE tube_u8 tube_weight_result(tube_s16 a, tube_s16 b, int bias, int shift)
{
    tube_s32 hi = vec_add(vec_add(vec_unpackh(a),vec_unpackh(b)),vec_splats(bias));
    tube_s32 lo = vec_add(vec_add(vec_unpackl(a),vec_unpackl(b)),vec_splats(bias));
    hi = vec_sra(hi,vec_splats((unsigned)shift));
    lo = vec_sra(lo,vec_splats((unsigned)shift));
    tube_s16 v = vec_packs(hi,lo);
    return vec_packsu(v,v);
}
TUBE_INLINE void tube_weight(uint8_t *d,ptrdiff_t s,int h,int denom,int weight,int offset,int w)
{
    int bias = offset * (1 << denom) + (denom ? 1 << (denom-1) : 0);
    for (int row=0;row<h;++row,d+=s) for (int col=0;col<w;col+=8) {
        tube_s16 a = tube_weight_mul(tube_pixels(d+col),weight);
        tube_store(d+col,tube_weight_result(a,vec_splat_s16(0),bias,denom),w==16 ? 8 : w);
    }
}
TUBE_INLINE void tube_biweight(uint8_t *d,uint8_t *p,ptrdiff_t s,int h,int denom,
                               int wd,int ws,int offset,int w)
{
    int bias = ((offset+1)|1) * (1 << denom);
    for (int row=0;row<h;++row,d+=s,p+=s) for (int col=0;col<w;col+=8) {
        tube_u8 v;
        if (wd==(1<<denom) && ws==wd && !offset) v=vec_avg(tube_load(d+col),tube_load(p+col));
        else v=tube_weight_result(tube_weight_mul(tube_pixels(d+col),wd),
                                   tube_weight_mul(tube_pixels(p+col),ws),bias,denom+1);
        tube_store(d+col,v,w==16 ? 8 : w);
    }
}
#define TUBE_WEIGHT(W) \
TUBE_KERNEL void tube_w##W(uint8_t *d,ptrdiff_t s,int h,int den,int wt,int off) \
{ tube_weight(d,s,h,den,wt,off,W); } \
TUBE_KERNEL void tube_b##W(uint8_t *d,uint8_t *p,ptrdiff_t s,int h,int den,int wd,int ws,int off) \
{ tube_biweight(d,p,s,h,den,wd,ws,off,W); }
TUBE_WEIGHT(16)
TUBE_WEIGHT(8)
TUBE_WEIGHT(4)
TUBE_WEIGHT(2)
#endif
