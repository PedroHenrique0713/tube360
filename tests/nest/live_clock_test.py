#!/usr/bin/env python3
"""Mede PTS de duas aberturas independentes do HLS, sem fingir um Nest físico."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT/'.work/f25/live-clock'
WORK.mkdir(parents=True, exist_ok=True)
if not (WORK/'segment13.ts').exists():
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=30',
                    '-f', 'lavfi', '-i', 'sine=frequency=660:sample_rate=48000', '-t', '28',
                    '-c:v', 'libx264', '-preset', 'ultrafast', '-threads', '2', '-g', '60', '-sc_threshold', '0',
                    '-c:a', 'aac', '-f', 'hls', '-hls_time', '2', '-hls_list_size', '0',
                    '-hls_segment_filename', str(WORK/'segment%02d.ts'), str(WORK/'all.m3u8')], check=True)
subprocess.run([str(ROOT/'client/build/nest-live-clock-test'), str(WORK)], check=True, timeout=10)
