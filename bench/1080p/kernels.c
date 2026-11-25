/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Tests the actual PPC instructions against the pinned FFmpeg C templates.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifdef F33_FREESTANDING
void *memcpy(void *d,const void *s,size_t n) { uint8_t *a=d; const uint8_t *b=s; while(n--) *a++=*b++; return d; }
void *memset(void *d,int c,size_t n) { uint8_t *p=d; while(n--) *p++=c; return d; }
int memcmp(const void *a,const void *b,size_t n) { const uint8_t *p=a,*q=b; while(n--) { if(*p!=*q) return *p-*q; ++p; ++q; } return 0; }
static void line(const char *s) {
    size_t n=0; while(s[n]) ++n;
    register long r0 __asm__("r0")=4, r3 __asm__("r3")=1;
    register const char *r4 __asm__("r4")=s;
    register size_t r5 __asm__("r5")=n;
    __asm__ volatile("sc" : "+r"(r0),"+r"(r3) : "r"(r4),"r"(r5) : "cr0","ctr","lr","memory");
}
static void finish(int code) __attribute__((noreturn));
static void finish(int code) {
    register long r0 __asm__("r0")=1, r3 __asm__("r3")=code;
    __asm__ volatile("sc" :: "r"(r0),"r"(r3) : "memory");
    __builtin_unreachable();
}
#else
#include <stdio.h>
#include <stdlib.h>
#include <xsdk/log.h>
static void line(const char *s) { xsdk_log_write("%s",s); }
static void finish(int code) { exit(code); }
#endif

#define pixeltmp int16_t
#define BIT_DEPTH 8
#include "libavcodec/h264qpel_template.c"
#include "libavcodec/h264chroma_template.c"
#include "libavcodec/h264dsp_template.c"
#include "h264-vmx128.h"
typedef void (*Qpel)(uint8_t*,const uint8_t*,ptrdiff_t);
typedef void (*Chroma)(uint8_t*,const uint8_t*,ptrdiff_t,int,int,int);
typedef void (*Weight)(uint8_t*,ptrdiff_t,int,int,int,int);
typedef void (*Biweight)(uint8_t*,uint8_t*,ptrdiff_t,int,int,int,int,int);
#define C_Q(P,W,X,Y) P##_h264_qpel##W##_mc##X##Y##_8_c
#define C_TABLE(P,W) {C_Q(P,W,0,0),C_Q(P,W,1,0),C_Q(P,W,2,0),C_Q(P,W,3,0), \
C_Q(P,W,0,1),C_Q(P,W,1,1),C_Q(P,W,2,1),C_Q(P,W,3,1), \
C_Q(P,W,0,2),C_Q(P,W,1,2),C_Q(P,W,2,2),C_Q(P,W,3,2), \
C_Q(P,W,0,3),C_Q(P,W,1,3),C_Q(P,W,2,3),C_Q(P,W,3,3)}
static const Qpel cq[2][3][16] = {{C_TABLE(put,8),C_TABLE(put,4),C_TABLE(put,2)},
                                                  {C_TABLE(avg,8),C_TABLE(avg,4),{0}}};
static const Qpel vq[2][3][16] = {{TUBE_QTABLE(put,8),TUBE_QTABLE(put,4),TUBE_QTABLE(put,2)},
                                                  {TUBE_QTABLE(avg,8),TUBE_QTABLE(avg,4),{0}}};
static const Chroma cc[2][3] = {{put_h264_chroma_mc8_8_c,put_h264_chroma_mc4_8_c,put_h264_chroma_mc2_8_c},
                                               {avg_h264_chroma_mc8_8_c,avg_h264_chroma_mc4_8_c,avg_h264_chroma_mc2_8_c}};
static const Chroma vc[2][3] = {{tube_put_c8,tube_put_c4,tube_put_c2},{tube_avg_c8,tube_avg_c4,tube_avg_c2}};
static const Weight cw[] = {weight_h264_pixels16_8_c,weight_h264_pixels8_8_c,weight_h264_pixels4_8_c,weight_h264_pixels2_8_c};
static const Weight vw[] = {tube_w16,tube_w8,tube_w4,tube_w2};
static const Biweight cb[] = {biweight_h264_pixels16_8_c,biweight_h264_pixels8_8_c,biweight_h264_pixels4_8_c,biweight_h264_pixels2_8_c};
static const Biweight vb[] = {tube_b16,tube_b8,tube_b4,tube_b2};
static uint8_t src[4096] __attribute__((aligned(16)));
static uint8_t a[4096] __attribute__((aligned(16)));
static uint8_t b[4096] __attribute__((aligned(16)));
static uint8_t original[4096] __attribute__((aligned(16)));
static uint32_t rng=0xf331080, cases;
static uint8_t random8(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng>>24; }
static void number(const char *prefix,unsigned n) {
    char buf[100]; int at=0,begin;
    while(*prefix) buf[at++]=*prefix++;
    begin=at; do { buf[at++]='0'+n%10; n/=10; } while(n);
    for(int lo=begin,hi=at-1;lo<hi;++lo,--hi) { char t=buf[lo];buf[lo]=buf[hi];buf[hi]=t; }
    buf[at++]='\n';buf[at]=0;line(buf);
}
static void fill(int pattern) {
    for(unsigned i=0;i<sizeof(a);++i) {
        src[i]=pattern==0 ? 0 : pattern==1 ? 255 : pattern==2 ? (i&1)*255 : pattern==3 ? i : random8();
        a[i]=random8();
    }
    memcpy(original,src,sizeof(src));
}
static void check(void) {
    ++cases;
    if(memcmp(a,b,sizeof(a)) || memcmp(src,original,sizeof(src))) {
        number("F33 KERNEL FAIL case=",cases);finish(1);
    }
}
static int run(void) {
    const int strides[]={64,73,128,-64}, offsets[]={-128,-1,0,1,127};
    line("F33 KERNEL START actual_PPC_VMX vs FFmpeg_7.1.5_C\n");
    for(int si=0;si<4;++si) for(int pattern=0;pattern<12;++pattern) {
        int s=strides[si]; fill(pattern);
        for(int align=0;align<16;++align) {
            int base=s>0 ? 4*s : -26*s;
            int dest=base+(align*7)%16, source=base+align;
            for(int avg=0;avg<2;++avg) for(int w=0;w<3;++w) for(int p=0;p<16;++p) {
                if(avg && w==2) continue;
                memcpy(b,a,sizeof(a));
                cq[avg][w][p](a+dest,src+source,s);
                vq[avg][w][p](b+dest,src+source,s);check();
            }
            for(int avg=0;avg<2;++avg) for(int w=0;w<3;++w)
                for(int h=2;h<=8;h*=2) for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
                    memcpy(b,a,sizeof(a));
                    cc[avg][w](a+dest,src+source,s,h,x,y);
                    vc[avg][w](b+dest,src+source,s,h,x,y);check();
                }
        }
    }
    number("F33 KERNEL qpel_chroma cases=",cases);
    for(int si=0;si<4;++si) for(int w=0;w<4;++w) for(int den=0;den<8;++den)
        for(int weight=-128;weight<=128;++weight) for(int oi=0;oi<5;++oi) {
            int s=strides[si],h=(w+den)%2 ? 4 : 8,off=offsets[oi];
            int base=s>0 ? 4*s : -26*s;
            int dest=base+(weight+128)%16,source=base+(den*3+oi)%16;
            fill(4);memcpy(b,a,sizeof(a));
            cw[w](a+dest,s,h,den,weight,off);vw[w](b+dest,s,h,den,weight,off);check();
            memcpy(b,a,sizeof(a));
            int ws=oi==0 ? -128 : oi==4 ? 128 : 128-weight%257;
            if(ws>128) ws-=257;
            cb[w](a+dest,src+source,s,h,den,weight,ws,off);
            vb[w](b+dest,src+source,s,h,den,weight,ws,off);check();
        }
    /* Caminho avg especializado, com valores que exercitam o arredondamento. */
    for(int w=0;w<4;++w) for(int den=0;den<8;++den) {
        fill(4);memcpy(b,a,sizeof(a));
        cb[w](a+512,src+512,64,8,den,1<<den,1<<den,0);
        vb[w](b+512,src+512,64,8,den,1<<den,1<<den,0);check();
        for(int off=-256;off<=254;++off) {
            fill(4);memcpy(b,a,sizeof(a));
            cb[w](a+512,src+512,64,8,den,-128,127,off);
            vb[w](b+512,src+512,64,8,den,-128,127,off);check();
        }
    }
    number("F33 KERNEL DONE PASS cases=",cases);
    return 0;
}
#ifdef F33_FREESTANDING
void _start(void) { finish(run()); }
#else
int main(void) { return run(); }
#endif
