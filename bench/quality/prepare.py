"""Mídia sintética local: sem cookies, rede ou cache de produção. Usa ffmpeg do PATH."""
import argparse
import json
from pathlib import Path
import subprocess


def ffmpeg(*args):
    subprocess.run(["ffmpeg", "-y", "-v", "error", *args], check=True)


parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
parser.add_argument("--mode", choices=["1080", "60", "live"], default="1080")
parser.add_argument("--seconds", type=int, default=40)
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
root = args.directory
for height, fps in [(1080, 30), (720, 30), (720, 60)]:
    name = f"{height}{'60' if fps == 60 else ''}.mp4"
    if not (root / name).exists():
        ffmpeg("-f", "lavfi", "-i", f"testsrc2=size={height * 16 // 9}x{height}:rate={fps}",
               "-t", str(args.seconds), "-c:v", "libx264", "-preset", "ultrafast", "-crf", "23",
               "-threads", "2", "-g", str(fps), "-an", "-movflags", "+faststart", str(root / name))
if not (root / "audio.m4a").exists():
    ffmpeg("-f", "lavfi", "-i", "sine=frequency=1000:sample_rate=44100", "-t", str(args.seconds),
           "-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart", str(root / "audio.m4a"))


def video(tag, height, fps, name):
    return dict(itag=tag, height=height, width=height * 16 // 9, fps=fps,
                mimeType='video/mp4; codecs="avc1.640028"', url=f"https://media.example/{name}")


formats = ([video(137, 1080, 30, "1080.mp4"), video(136, 720, 30, "720.mp4")]
           if args.mode == "1080" else [video(298, 720, 60, "72060.mp4")])
formats.append(dict(itag=140, mimeType='audio/mp4; codecs="mp4a.40.2"', bitrate=128000,
                    url="https://media.example/audio.m4a"))
streaming = dict(expiresInSeconds="3600", adaptiveFormats=formats)
if args.mode == "live":
    ffmpeg("-i", str(root / "72060.mp4"), "-i", str(root / "audio.m4a"), "-c", "copy", "-f", "hls",
           "-hls_time", "4", "-hls_list_size", "0", "-hls_segment_filename", str(root / "live%03d.ts"),
           str(root / "video.m3u8"))
    (root / "master.m3u8").write_text('#EXTM3U\n#EXT-X-STREAM-INF:CODECS="avc1.640028,mp4a.40.2",'
                                     'RESOLUTION=1280x720,FRAME-RATE=60\nvideo.m3u8\n')
    streaming["hlsManifestUrl"] = "https://media.example/master.m3u8"
(root / "player.json").write_text(json.dumps(dict(playabilityStatus=dict(status="OK"),
    videoDetails=dict(videoId="4p_9OPKgcDE", title=f"F15 — fonte {args.mode}", isLive=args.mode == "live",
                      lengthSeconds=str(args.seconds)), streamingData=streaming)))
