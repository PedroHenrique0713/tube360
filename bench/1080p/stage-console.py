#!/usr/bin/env python3
"""Prepare disposable console folders locally. No XBDM, network or personal files."""
import hashlib
import argparse
import json
from pathlib import Path
import shutil

root = Path(__file__).resolve().parents[2]
bench = root / "bench/1080p"
stage = bench / ".work/console"
stage.mkdir(parents=True, exist_ok=True)
files = {}
cli = argparse.ArgumentParser()
cli.add_argument("--cpu-only", action="store_true")
args = cli.parse_args()


def copy(source, relative):
    dest = stage / relative
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, dest)
    files[relative] = hashlib.sha256(dest.read_bytes()).hexdigest()


def write(relative, text):
    dest = stage / relative
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(text)
    files[relative] = hashlib.sha256(dest.read_bytes()).hexdigest()


copy(bench / "build-xbox/f33-cpu.xex", "cpu/f33-cpu.xex")
copy(bench / "build-xbox/f33-kernels.xex", "kernels/f33-kernels.xex")
copy(root / "bench/cpu-floor/build-xbox/cpu-floor.xex", "floor/cpu-floor.xex")
for source in sorted((root / "bench/cpu-floor/media").glob("*.mp4")):
    copy(source, "floor/" + source.name)
    if "60-" not in source.name:
        copy(source, "cpu/" + source.name)
write("cpu/f33-config.txt", "1 6 0\n")
for mask in (1, 3, 7):
    write(f"configs/mask{mask}-6-full.txt", f"{mask} 6 0\n")
for threads in (4, 5, 6):
    for nonref in (0, 1):
        write(f"configs/mask7-{threads}-{'nonref' if nonref else 'full'}.txt", f"7 {threads} {nonref}\n")
if not args.cpu_only:
    copy(root / "client/build-x360/tube360.xex", "app/tube360.xex")
    for source in sorted((root / "client/assets").glob("inter-*.ttf")):
        copy(source, "app/" + source.name)
    write("app/settings.txt", "lang=pt\nquality=720\nvideo_mode=1080p30\noutput_height=1080\nsound=tv\n")
    write("app/decode-config.txt", "1 6 1 0\n")
    write("app/autoplay.txt", "4p_9OPKgcDE 31\n")
    write("app/f33-playback.txt", "120 10\n")
result = dict(stage=str(stage), files=files, executed_on_console=False,
              recipe_id=(root / "bench/cpu-floor/.work/app-build/.work/ffmpeg-xbox/.tube360-source-id").read_text().strip())
(bench / "results/artifacts.json").write_text(json.dumps(result, indent=2) + "\n")
print(stage)
print("Prepared", len(files), "files; no console access")
