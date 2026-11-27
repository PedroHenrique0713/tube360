#!/usr/bin/env python3
"""Gera segmentos de live de teste no formato do YouTube: cada segmento é ftyp+moov+emsg+moof+mdat, PTS absoluto enorme.

    make_fixture.py <pasta> [segundos=16] [seg=2]
Saída: v_<i>.bin, a_<i>.bin (i = 0..), meta.txt com `segments <n>`, `segsec <s>`, `base <PTS absoluto inicial>`.
"""
import os, struct, subprocess, sys
out = sys.argv[1]; total = int(sys.argv[2]) if len(sys.argv) > 2 else 16; seg = int(sys.argv[3]) if len(sys.argv) > 3 else 2
BASE = 14077013  # segundos: o X-Head-Time de uma live real passa de 14 milhões
os.makedirs(out, exist_ok=True)
def run(*a): subprocess.run(a, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
v, a = os.path.join(out, "_v.mp4"), os.path.join(out, "_a.mp4")
run("ffmpeg", "-y", "-f", "lavfi", "-i", f"testsrc2=size=640x360:rate=30:duration={total}", "-c:v", "libx264", "-profile:v", "main", "-pix_fmt", "yuv420p",
    "-g", str(30 * seg), "-keyint_min", str(30 * seg), "-sc_threshold", "0", "-bf", "0", "-video_track_timescale", "90000",
    "-movflags", "frag_keyframe+empty_moov+default_base_moof", v)
run("ffmpeg", "-y", "-f", "lavfi", "-i", f"sine=frequency=440:sample_rate=44100:duration={total}", "-c:a", "aac", "-b:a", "96k",
    "-frag_duration", str(seg * 1000000), "-movflags", "frag_keyframe+empty_moov+default_base_moof", a)
def boxes(b):
    i, res = 0, []
    while i + 8 <= len(b):
        sz, t = struct.unpack(">I4s", b[i:i + 8]); res.append((t, b[i:i + sz])); i += sz
    return res
def shift(seg):
    """Soma BASE segundos ao tfdt (64 bits) do moof, como o YouTube: o PTS absoluto de uma live passa de 14 milhões de s."""
    m = seg.find(b"mdhd"); ts = struct.unpack(">I", seg[m + 16:m + 20])[0] if seg[m + 4] == 0 else struct.unpack(">I", seg[m + 24:m + 28])[0]
    t = seg.find(b"tfdt"); assert seg[t + 4] == 1, "tfdt de 32 bits"
    base = struct.unpack(">Q", seg[t + 8:t + 16])[0] + BASE * ts
    return seg[:t + 8] + struct.pack(">Q", base) + seg[t + 16:]
def emsg(): return struct.pack(">I4s", 20, b"emsg") + b"\0" * 12
count = 0
for name, path in (("v", v), ("a", a)):
    bx = boxes(open(path, "rb").read())
    init = b"".join(x for t, x in bx if t in (b"ftyp", b"moov"))
    frags = [(t, x) for t, x in bx if t in (b"moof", b"mdat")]
    n = 0
    for k in range(0, len(frags) - 1, 2):
        open(os.path.join(out, f"{name}_{n}.bin"), "wb").write(shift(init + emsg() + frags[k][1] + frags[k + 1][1])); n += 1
    count = n if name == "v" else min(count, n)
open(os.path.join(out, "meta.txt"), "w").write(f"segments {count}\nsegsec {seg}\nbase {BASE}\n")
print("segments", count)
