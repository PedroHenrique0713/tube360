#!/usr/bin/env python3
"""Recusa multiplicações inteiras AltiVec ausentes no Xenon nos DSPs habilitados."""
from pathlib import Path
import re
import subprocess
import sys

forbidden = set("vmladduhm vmhaddshs vmhraddshs vmulesb vmulesh vmuleub vmuleuh vmulosb vmulosh vmuloub vmulouh vmsummbm vmsumshm vmsumshs vmsumubm vmsumuhm vmsumuhs".split())
objdump, build = sys.argv[1:]
for name in ("h264qpel", "h264dsp", "idctdsp"):
    path = Path(build) / f"libavcodec/ppc/{name}.o"
    text = subprocess.check_output([objdump, "-d", path], text=True)
    opcodes = re.findall(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+){4}\s*([a-z0-9.]+)", text, re.M)
    bad = forbidden.intersection(opcodes)
    vectors = sum(op.startswith("v") for op in opcodes)
    if not opcodes or bad or (name != "idctdsp" and vectors < 16):
        raise RuntimeError(f"VMX inválido: {path}: {len(opcodes)} instruções, {vectors} vetoriais, proibidas {sorted(bad)}")
    print(f"VMX {name}: {len(opcodes)} instruções, {vectors} vetoriais, 0 multiplicações incompatíveis")
