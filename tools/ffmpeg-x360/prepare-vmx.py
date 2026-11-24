#!/usr/bin/env python3
"""O qpel só multiplica por 1, 5 e 20: shifts/adds preservam o inteiro exato no VMX128."""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]) / "libavcodec/ppc"
path = root / "h264qpel_template.c"
text = path.read_text()


def mladd(match):
    value, factor, bias = match.groups()
    shift = f"vec_sl({value}, vec_splat_u16(2))"
    other = value if factor == "v5ss" else f"vec_sl({value}, vec_splat_u16(4))"
    return f"vec_add(vec_add({shift}, {other}), {bias})"


text, count = re.subn(r"vec_mladd\((\w+), (v20ss|v5ss), (\w+)\)", mladd, text)
if count != 12:
    raise RuntimeError(f"qpel mladd: esperadas 12, achadas {count}")
text, count = re.subn(r"vec_mul(e|o)\((\w+), v(20|5|1)ss\)",
                      lambda m: f"tube360_mul_{m[1]}({m[2]}, {m[3]})", text)
if count != 10:
    raise RuntimeError(f"qpel mul: esperadas 10, achadas {count}")
path.write_text(text)
path = root / "h264qpel.c"
text = path.read_text()
anchor = "#if HAVE_ALTIVEC\n"
if text.count(anchor) != 2:
    raise RuntimeError("qpel init incompatível")
helpers = """
static av_always_inline vec_s32 tube360_mul_e(vec_s16 a, int factor)
{
    const vec_u8 select = {0,1,4,5,8,9,12,13,0,1,4,5,8,9,12,13};
    vec_s32 words = vec_unpackh((vec_s16)vec_perm((vec_u8)a, (vec_u8)a, select));
    if (factor == 1) return words;
    vec_s32 five = vec_add(vec_sl(words, vec_splat_u32(2)), words);
    return factor == 5 ? five : vec_sl(five, vec_splat_u32(2));
}
static av_always_inline vec_s32 tube360_mul_o(vec_s16 a, int factor)
{
    const vec_u8 select = {2,3,6,7,10,11,14,15,2,3,6,7,10,11,14,15};
    vec_s32 words = vec_unpackh((vec_s16)vec_perm((vec_u8)a, (vec_u8)a, select));
    if (factor == 1) return words;
    vec_s32 five = vec_add(vec_sl(words, vec_splat_u32(2)), words);
    return factor == 5 ? five : vec_sl(five, vec_splat_u32(2));
}
"""
path.write_text(text.replace(anchor, anchor + helpers, 1))

path = root / "../pthread_frame.c"
text = path.read_text()
anchor = '#include "libavutil/thread.h"'
text = text.replace(anchor, anchor + '\n#include "libavutil/time.h"\nextern void ffmpeg_video_work(unsigned us, int frame);', 1)
anchor = "            ret = ff_decode_receive_frame_internal(avctx, frame);"
if text.count(anchor) != 1:
    raise RuntimeError("worker de decode incompatível")
text = text.replace(anchor, """            int64_t tube360_begin = av_gettime_relative();
            ret = ff_decode_receive_frame_internal(avctx, frame);
            if (avctx->codec_id == AV_CODEC_ID_H264)
                ffmpeg_video_work((unsigned)(av_gettime_relative() - tube360_begin), ret == 0);""", 1)
path.write_text(text)
