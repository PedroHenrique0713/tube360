"""Compara capturas do shader real com ffmpeg Lanczos, inteiras e no recorte central."""
import json
from pathlib import Path
import re
import subprocess
import sys

media, proofs = map(Path, sys.argv[1:])
results = []
for name in ["chart", "fractal"]:
    ref = proofs / f"{name}-lanczos.ppm"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pixel_format", "yuv420p",
        "-video_size", "1920x1080", "-i", str(media / f"{name}.yuv"), "-frames:v", "1", "-vf",
        "scale=1280:720:flags=lanczos:in_color_matrix=bt709:out_color_matrix=bt709,format=rgb24", str(ref)], check=True)
    for method in ["bilinear", "filter"]:
        for crop in [False, True]:
            row = dict(source=name, method=method, crop=crop)
            for metric in ["psnr", "ssim"]:
                before = "format=gbrp" + (",crop=640:360:320:180" if crop else "")
                p = subprocess.run(["ffmpeg", "-v", "info", "-i", str(proofs / f"{name}-{method}.ppm"),
                    "-i", str(ref), "-filter_complex", f"[0]{before}[a];[1]{before}[b];[a][b]{metric}",
                    "-frames:v", "1", "-f", "null", "-"], capture_output=True, text=True, check=True)
                match = re.search(r"average:([\d.]+)" if metric == "psnr" else r"All:([\d.]+)", p.stderr)
                if not match:
                    raise RuntimeError(p.stderr)
                row[metric] = float(match[1])
            results.append(row)
print(json.dumps(results, indent=2))
