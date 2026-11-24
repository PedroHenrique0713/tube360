#!/usr/bin/env python3
"""Player/render reais com Cast TLS atrasado; replay, HOME e conta isolados."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'bench/standalone'))
from cast_test import Receiver

WORK = ROOT/'.work/f25/player'
REPLAY = WORK/'replay'
REPLAY.mkdir(parents=True, exist_ok=True)
for name, content in {
    'watch.html': '<script>ytcfg.set({"VISITOR_DATA":"fixture","jsUrl":"/s/player/fixture/base.js"})</script>',
    'base.js': 'var fixture={signatureTimestamp:20775};',
    'home.html': '<script>ytcfg.set({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"fixture"}}})</script>',
    'browse.json': '{"contents":{}}',
}.items():
    (REPLAY/name).write_text(content)
(REPLAY/'player.json').write_text(json.dumps({'playabilityStatus': {'status': 'OK'}, 'videoDetails': {
    'videoId': 'F25fixture0', 'title': 'Bancada F25 · Nest em Wi-Fi ruim', 'lengthSeconds': '40'}, 'streamingData': {
    'expiresInSeconds': '3600', 'adaptiveFormats': [
        {'itag': 136, 'mimeType': 'video/mp4; codecs="avc1.64001f"', 'width': 1280, 'height': 720, 'fps': 30,
         'url': 'https://fixture.googlevideo.com/video'},
        {'itag': 140, 'mimeType': 'audio/mp4; codecs="mp4a.40.2"', 'bitrate': 128000,
         'url': 'https://fixture.googlevideo.com/audio'}]}}))
if not (REPLAY/'video.mp4').exists():
    subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=30:duration=40',
                    '-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '34', '-threads', '2', '-an',
                    '-movflags', '+faststart', str(REPLAY/'video.mp4')], check=True)
if not (REPLAY/'audio.m4a').exists():
    subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'sine=frequency=660:sample_rate=44100:duration=40',
                    '-c:a', 'aac', '-vn', '-movflags', '+faststart', str(REPLAY/'audio.m4a')], check=True)
cert, key = WORK/'cert.pem', WORK/'key.pem'
subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=cast-fixture',
                '-keyout', str(key), '-out', str(cert)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def run(name, silence=False):
    assets = WORK/name
    assets.mkdir(exist_ok=True)
    for font in (ROOT/'client/assets').glob('inter-*.ttf'):
        shutil.copyfile(font, assets/font.name)
    (assets/'settings.txt').write_text('lang=pt\nquality=720\nsound=nest\nserver=off\nsync_ms=0\n')
    (assets/'autoplay.txt').write_text('F25fixture0 0\n')
    receiver = Receiver(cert, key)
    receiver.request_delay = receiver.response_delay = .065
    receiver.drop_media = not silence
    (assets/'nest.txt').write_text(f'127.0.0.1 {receiver.port}\nNest simulado F25\n')
    began_silence = [None]
    stop_times = []
    original = receiver.handle

    def handle(client, request):
        data = json.loads(request.payload_utf8)
        if request.namespace.endswith('.media') and data['type'] == 'STOP':
            stop_times.append(time.monotonic())
        original(client, request)
    receiver.handle = handle
    seconds = 32 if silence else 10
    env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
    home = WORK/'home'
    home.mkdir(exist_ok=True)
    env.update(HOME=str(home), XDG_CACHE_HOME=str(home/'.cache'), LP_NUM_THREADS='2',
               SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', XT_CAST_DISABLE='1',
               XT_ASSETS=str(assets), XT_YT_REPLAY=str(REPLAY), XT_SHOT=str(WORK/(name+'.ppm')),
               XT_SHOT_MS=str((seconds-3)*1000), XT_SHOT_FRAME='2147483647',
               XT_KEYS_MS='1000', XT_KEYS='.'*(seconds-2)+'U')
    quit_worker = threading.Event()
    failures = []

    def outage():
        try:
            deadline = time.monotonic()+5
            while not receiver.commands('LOAD') and time.monotonic() < deadline:
                if quit_worker.wait(.02):
                    return
            assert receiver.commands('LOAD'), 'no LOAD before outage'
            if quit_worker.wait(.8):
                return
            began_silence[0] = time.monotonic()
            receiver.replies = False
            if not quit_worker.wait(26):
                receiver.replies = True
        except Exception as error:
            failures.append(repr(error))
    worker = threading.Thread(target=outage) if silence else None
    process = None
    try:
        with (WORK/(name+'.log')).open('w') as log:
            process = subprocess.Popen([str(ROOT/'client/build/tube360')], env=env, stdout=log, stderr=log)
            if worker:
                worker.start()
            assert process.wait(seconds+15) == 0
    finally:
        if process and process.poll() is None:
            process.terminate()
            process.wait(8)
        quit_worker.set()
        if worker:
            worker.join(3)
        receiver.close()
    assert not receiver.errors and not failures, (receiver.errors, failures)
    log = (WORK/(name+'.log')).read_text()
    assert 'som: Nest' in log and 'primeiro quadro' in log
    assert len(receiver.commands('LOAD')) == 1
    assert receiver.commands('STOP') and not (assets/'nest-owner.json').exists()
    if silence:
        delay = min(stop_times)-began_silence[0]
        assert 24 < delay < 27, delay
        assert 'liberando para TV' in log and 'som: TV' in log
        print(f'PASS player silence: first STOP after {delay:.3f}s; TV only after quiet confirmation')
    else:
        assert 'liberando para TV' not in log and 'som: TV' not in log
        assert receiver.dropped > 5
        print(f'PASS player RTT>=130ms, loss={receiver.dropped}/{receiver.media_replies}: one LOAD, no TV fallback')
    (WORK/(name+'-wire.json')).write_text(json.dumps(receiver.events, indent=2))
    subprocess.run(['magick', str(WORK/(name+'.ppm')), str(WORK/(name+'.png'))], check=True)


run('wifi-130ms-loss30')
run('silence-25s', silence=True)
