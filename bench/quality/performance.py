"""A/B do app real com transporte gravado. Não acessa conta, servidor ou console reais."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
parser.add_argument("--milliseconds", type=int, default=10000)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
root = args.directory.resolve()
if not (root / "media/player.json").is_file():
    raise RuntimeError("Prepare a mídia com prepare.py --mode both")
cases = [("staging", 0, 6, 0), ("mapped", 1, 6, 0), ("nonref", 1, 6, 1), ("four", 1, 4, -1)]
results = []
for mode in ("1080p30", "720p60"):
    for name, direct, threads, loop in cases:
        folder = root / f"{mode}-{name}"
        assets = folder / "assets"
        assets.mkdir(parents=True, exist_ok=True)
        (folder / "home").mkdir(exist_ok=True)
        for font in (repo / "client/assets").glob("inter-*.ttf"):
            shutil.copy2(font, assets)
        (assets / "settings.txt").write_text(f"lang=pt\nquality=720\nsound=tv\nvideo_mode={mode}\noutput_height=1080\n")
        (assets / "decode-config.txt").write_text(f"{direct} {threads} 1 {loop}\n")
        env = dict(os.environ, HOME=str(folder / "home"), SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy",
                   XT_CAST_DISABLE="1", XT_ASSETS=str(assets), XT_QUALITY_FIXTURES=str(root / "media"),
                   XT_PLAY="4p_9OPKgcDE", XT_SHOT=str(folder / "player.ppm"), XT_SHOT_MS=str(args.milliseconds))
        with (folder / "player.log").open("w") as log:
            subprocess.run([repo / "client/build/tube360-quality-fixture"], env=env, stdout=log, stderr=subprocess.STDOUT,
                           timeout=args.milliseconds / 1000 + 45, check=True)
        text = (folder / "player.log").read_text()
        samples = re.findall(r"qualidade: .*?alvo (\d+), ([\d.]+) fps na TV.*?decode ([\d.]+) ms/s de video, copia ([\d.]+) ms/quadro", text)
        uploads = re.findall(r"upload: .*?staging ([\d.]+) ms/quadro.*?submit-CPU ([\d.]+) ms", text)
        if len(samples) < 3 or len(uploads) < 3 or "captura:" not in text or "falha H.264" in text:
            raise RuntimeError(f"Prova incompleta: {folder}")
        if direct and not re.search(r"decod-memoria: direto [1-9]", text):
            raise RuntimeError(f"O caso não usou get_buffer2 mapeado: {folder}")
        def median(rows, column):
            return statistics.median(float(row[column]) for row in rows[1:])
        result = dict(mode=mode, case=name, samples=len(samples), fps=median(samples, 1),
                      decode_ms_per_second=median(samples, 2), handoff_ms=median(samples, 3),
                      staging_ms=median(uploads, 0), submit_ms=median(uploads, 1))
        results.append(result)
        print(json.dumps(result), flush=True)
(root / "performance.json").write_text(json.dumps(results, indent=2) + "\n")
