"""Escada 720p60 -> 720p30 -> 480p30 com 2,5 Mbit/s compartilhados e servidor desligado."""
import argparse
import json
import re
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("work", type=Path)
parser.add_argument("--check-only", action="store_true")
args = parser.parse_args()
work = args.work.resolve()
if not args.check_only:
    for directory in ("media", "assets", "home", "proofs"):
        (work / directory).mkdir(parents=True, exist_ok=True)
    formats = []
    for name, width, height, fps, bitrate, tag in [("72060", 1280, 720, 60, 4200, 298),
                                                  ("72030", 1280, 720, 30, 3100, 136),
                                                  ("48030", 854, 480, 30, 1400, 135)]:
        path = work / "media" / (name + ".mp4")
        if not path.exists():
            subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", f"testsrc2=size={width}x{height}:rate={fps}",
                            "-t", "55", "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p", "-threads", "2",
                            "-b:v", f"{bitrate}k", "-maxrate", f"{bitrate}k", "-bufsize", f"{2*bitrate}k",
                            "-x264-params", "nal-hrd=cbr", "-g", str(fps), "-an", "-movflags", "+faststart", str(path)], check=True)
        formats.append(dict(itag=tag, width=width, height=height, fps=fps, bitrate=bitrate*1000,
                            mimeType='video/mp4; codecs="avc1"', url=f"https://media.example/{path.name}"))
    audio = work / "media/audio.m4a"
    if not audio.exists():
        subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "sine=frequency=1000:sample_rate=44100", "-t", "55",
                        "-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart", str(audio)], check=True)
    formats.append(dict(itag=140, mimeType='audio/mp4; codecs="mp4a"', bitrate=128000, url="https://media.example/audio.m4a"))
    (work / "media/player.json").write_text(json.dumps(dict(playabilityStatus=dict(status="OK"),
        videoDetails=dict(videoId="4p_9OPKgcDE", title="F19 - rede 2,5 Mbit/s - sem PC", lengthSeconds="55"),
        streamingData=dict(expiresInSeconds="3600", adaptiveFormats=formats))))
    for path in (root / "client/assets").glob("inter-*.ttf"):
        shutil.copyfile(path, work / "assets" / path.name)
    (work / "assets/settings.txt").write_text("lang=pt\nquality=720\nsound=tv\nserver=off\nvideo_mode=720p60\noutput_height=720\n")
    env = dict(os.environ, HOME=str(work / "home"), SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy", XT_CAST_DISABLE="1",
               XT_ASSETS=str(work / "assets"), XT_QUALITY_FIXTURES=str(work / "media"), XT_PLAY="4p_9OPKgcDE",
               XT_SHOT=str(work / "proofs/ladder.ppm"), XT_SHOT_MS="50000", XT_QUALITY_MBIT="2.5")
    with (work / "proofs/player.log").open("w") as log:
        subprocess.run([str(root / "client/build/tube360-quality-fixture")], env=env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=80)
text = (work / "proofs/player.log").read_text()
assert "falha H.264" not in text and "get_buffer() failed" not in text, "decoder failed after a source transition"
capture = re.search(r"captura:.*posição ([0-9.]+)", text)
assert capture and float(capture[1]) > 40, "playback did not keep advancing after the last transition"
assert "itag 298 -> 136" in text and "itag 136 -> 135" in text, text[-8000:]
assert text.count("primeiro quadro:") == 1 and text.count("audio: selecionada") == 1, "AAC/player restarted"
assert "reserva no servidor" not in text and "H.264 -> MPEG-1" not in text, "PC fallback used"
assert "fonte 720p60.000 -> 720p30.000" in text and "fonte 720p30.000 -> 480p30.000" in text
assert text.count("resultado 0") >= 2 and "VOD 480p 30.000 fps" in text, "lower sources never delivered decoded frames"
assert "som: fila PCM vazia" not in text, "audio reserve emptied during a source transition"
subprocess.run(["magick", str(work / "proofs/ladder.ppm"), str(work / "proofs/ladder.png")], check=True)
print("PASS native player: both source steps, one AAC session, one first frame, no server; screenshot:", work / "proofs/ladder.png")
