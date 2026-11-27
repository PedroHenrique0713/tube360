#!/usr/bin/env python3
"""Cadência de apresentações Vulkan e erro contra o relógio independente do Cast."""
import argparse
import bisect
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import time

from wifi_player_test import ROOT, REPLAY, Receiver, cert, key
from cast_test import JITTER_PROFILE

WORK = ROOT/'.work/f25b-jitter/player'


def run(name, binary, jitter, seconds):
    assets = WORK/name
    assets.mkdir(parents=True, exist_ok=True)
    home = assets/'home'
    home.mkdir(exist_ok=True)
    for font in (ROOT/'client/assets').glob('inter-*.ttf'):
        shutil.copyfile(font, assets/font.name)
    (assets/'nest-owner.json').unlink(missing_ok=True)
    (assets/'settings.txt').write_text('lang=pt\nquality=720\nsound=nest\nsync_ms=0\n')
    (assets/'autoplay.txt').write_text('F25fixture0 0\n')
    (assets/'video-metrics.txt').write_text('1\n')
    receiver = Receiver(cert, key)
    receiver.fragment = False
    receiver.clock_rate = 1.002
    receiver.drop_media = jitter
    profiles = []

    def timing(request):
        data = json.loads(request.payload_utf8)
        if jitter and request.namespace.endswith('.media') and data['type'] == 'GET_STATUS':
            pair = JITTER_PROFILE[len(profiles) % len(JITTER_PROFILE)]
            profiles.append(pair)
            return pair
        return .025, .025
    receiver.timing = timing
    (assets/'nest.txt').write_text(f'127.0.0.1 {receiver.port}\nJitter fixture\n')
    env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(home), XDG_CACHE_HOME=str(home/'.cache'), LP_NUM_THREADS='2',
               SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', XT_CAST_DISABLE='1',
               XT_NEST_NETWORK='127.77.28.1/255.255.255.252', XT_ASSETS=str(assets),
               XT_YT_REPLAY=str(REPLAY), XT_H264_THREADS='2',
               XT_SHOT=str(assets/'frame.ppm'), XT_SHOT_MS=str(seconds*1000), XT_SHOT_FRAME='2147483647')
    frames = []
    failures = []
    process = None
    log_path = assets/'player.log'

    def collect():
        try:
            with log_path.open('w') as log:
                for line in process.stdout:
                    received = time.monotonic()
                    log.write(line)
                    match = re.search(r'render-quadro: pts ([\d.]+)', line)
                    if match:
                        stamp = re.search(r'monotonic-ms ([\d.]+)', line)
                        with receiver.lock:
                            actual = receiver.at()
                        frames.append(dict(time=received, present_ms=float(stamp[1]) if stamp else None,
                                           pts=float(match[1]), actual=actual))
        except Exception as error:
            failures.append(repr(error))
    reader = None
    try:
        process = subprocess.Popen([str(binary)], env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True)
        reader = threading.Thread(target=collect)
        reader.start()
        assert process.wait(seconds+20) == 0
    finally:
        if process and process.poll() is None:
            process.terminate()
            process.wait(8)
        if reader:
            reader.join(5)
        if process:
            process.stdout.close()
        receiver.close()
    assert not failures and not receiver.errors, (failures, receiver.errors)
    log = log_path.read_text()
    assert 'som: Nest' in log and 'som: TV' not in log and 'liberando para TV' not in log
    assert 'busca unicast' not in log
    assert len(receiver.commands('LOAD')) == 1 and receiver.commands('STOP')
    assert len(frames) > 450, len(frames)
    # Uma marca por upload novo depois de vkQueuePresentKHR, sem contar repetições da UI.
    timestamps = [f['present_ms']/1000 if f['present_ms'] is not None else f['time'] for f in frames]
    counts = []
    ends = sorted({stamp for stamp in timestamps if stamp-timestamps[0]>=1} |
                  {stamp+1 for stamp in timestamps if stamp+1<=timestamps[-1]})
    for end in ends:
        for edge in (end, end-1e-7):
            if edge-timestamps[0]>=1:
                counts.append(bisect.bisect_right(timestamps, edge)-bisect.bisect_right(timestamps, edge-1))
    errors = [abs(f['pts']-f['actual'])*1000 for f in frames]
    intervals = [(b-a)*1000 for a, b in zip(timestamps, timestamps[1:])]
    result = dict(case=name, frames=len(frames), windows=len(counts), fps_min=min(counts), fps_max=max(counts),
                  sync_max_ms=max(errors), sync_mean_ms=sum(errors)/len(errors),
                  interval_max_ms=max(intervals), dropped=receiver.dropped, replies=receiver.media_replies,
                  oscillator_ppm=2000, configured_rtt_ms=[min(sum(p) for p in profiles)*1000,
                                                       max(sum(p) for p in profiles)*1000] if profiles else [50, 50])
    (assets/'frames.json').write_text(json.dumps(frames, indent=2))
    (assets/'result.json').write_text(json.dumps(result, indent=2))
    subprocess.run(['magick', str(assets/'frame.ppm'), str(assets/'frame.png')], check=True)
    print(json.dumps(result), flush=True)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT/'client/build/tube360')
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--seconds', type=int, default=26)
    args = parser.parse_args()
    results = [run(('baseline-' if args.baseline else '')+name, args.binary.resolve(), jitter, args.seconds)
               for name, jitter in [('control', False), ('jitter-50-900ms-loss30', True)]]
    for result in results:
        assert 29 <= result['fps_min'] <= result['fps_max'] <= 31, result
        assert result['sync_max_ms'] < 100, result
    assert results[1]['dropped'] > 10
    assert .25 < results[1]['dropped']/results[1]['replies'] < .35
