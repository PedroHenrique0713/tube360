#!/usr/bin/env python3
"""Synthetic High/CABAC fixtures. The external GPL encoder is not linked into the bench."""
import hashlib
import json
from pathlib import Path
import subprocess

BENCH = Path(__file__).resolve().parent
SPECS = [("720p30-4000", 1280, 720, 30, 4000, "3.1"),
         ("1080p30-2500", 1920, 1080, 30, 2500, "4.0"),
         ("1080p30-5500", 1920, 1080, 30, 5500, "4.0"),
         ("1080p60-2500", 1920, 1080, 60, 2500, "4.2"),
         ("1080p60-5500", 1920, 1080, 60, 5500, "4.2")]


def main():
    media = BENCH / "media"
    media.mkdir(exist_ok=True)
    fixtures = []
    version = subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0]
    for name, width, height, fps, kbps, level in SPECS:
        path = media / f"{name}.mp4"
        params = (f"cabac=1:8x8dct=1:ref=3:bframes=3:b-adapt=0:b-pyramid=normal:"
                  f"direct=temporal:weightp=2:weightb=1:keyint={fps*2}:min-keyint={fps*2}:"
                  "scenecut=0:slices=1:nal-hrd=cbr:force-cfr=1:lookahead-threads=1")
        command = ["ffmpeg", "-hide_banner", "-loglevel", "warning", "-y", "-f", "lavfi", "-i",
                   f"testsrc2=size={width}x{height}:rate={fps}:duration=6", "-an", "-c:v", "libx264",
                   "-threads", "2", "-preset", "medium", "-profile:v", "high", "-level:v", level,
                   "-pix_fmt", "yuv420p", "-b:v", f"{kbps}k", "-minrate", f"{kbps}k", "-maxrate",
                   f"{kbps}k", "-bufsize", f"{kbps*2}k", "-x264-params", params, "-movflags", "+faststart", str(path)]
        print(f"F30 GENERATE {name}", flush=True)
        subprocess.run(command, check=True)
        info = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-count_frames", "-show_streams",
                                                  "-show_format", "-of", "json", str(path)], text=True))
        stream, = info["streams"]
        assert (stream["profile"], stream["level"], stream["pix_fmt"], stream["width"], stream["height"],
                stream["r_frame_rate"], int(stream["nb_read_frames"])) == (
                    "High", int(float(level)*10), "yuv420p", width, height, f"{fps}/1", fps*6), stream
        if width == 1920:
            assert 2500000 <= int(stream["bit_rate"]) <= 6000000, stream["bit_rate"]
        fixtures.append({"name": name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                         "bytes": path.stat().st_size, "command": command[:-1] + [f"media/{name}.mp4"], "probe": info})
    (BENCH / "results/fixtures.json").write_text(json.dumps({"encoder": version, "fixtures": fixtures}, indent=2) + "\n")


if __name__ == "__main__":
    main()
