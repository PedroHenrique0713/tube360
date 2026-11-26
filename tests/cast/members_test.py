#!/usr/bin/env python3
"""HTTP 403 no Player real; conta e mídia de bancada, sem rede externa."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory(prefix='tube360-f53-members-') as directory:
        folder = Path(directory)
        (folder/'account.json').write_text(json.dumps({
            'access_token': 'fake-a', 'refresh_token': 'fake-r',
            'expires_at': int(time.time())+3600,
            'client': {'client_id': 'fake-client', 'client_secret': 'fake-secret'},
        }))
        subprocess.run([
            'ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'color=size=160x90:rate=30',
            '-t', '0.5', '-an', '-c:v', 'libx264', '-threads', '1', '-movflags', '+faststart',
            str(folder/'video.mp4'),
        ], check=True)
        env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
        env.update(SDL_AUDIO_DRIVER='dummy')
        binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT/'client/build/member-playback-test'
        subprocess.run([str(binary), directory],
                       env=env, check=True, timeout=20)


if __name__ == '__main__':
    main()
