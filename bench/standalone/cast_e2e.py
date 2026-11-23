#!/usr/bin/env python3
"""Exercita o binário desktop com replay sanitizado e receptor Cast TLS local."""
import json
import http.server
import os
import pathlib
import shutil
import subprocess
import sys
import threading
import time
import ssl
import urllib.request
from cast_test import Receiver

root = pathlib.Path(sys.argv[1]).resolve()
proof = root/'.work/provas'
assets = proof/'assets'
replay = proof/'replay'
assets.mkdir(parents=True, exist_ok=True)
replay.mkdir(parents=True, exist_ok=True)
for font in (root/'client/assets').glob('inter-*.ttf'):
    shutil.copyfile(font, assets/font.name)
(assets/'settings.txt').write_text('lang=pt\ncols=4\nquality=720\nsound=nest\nserver=off\nsync_ms=0\n')
(assets/'autoplay.txt').write_text('F13fixture0 0\n')
(replay/'watch.html').write_text('<script>ytcfg.set({"VISITOR_DATA":"fixture","jsUrl":"/s/player/fixture/base.js"})</script>')
(replay/'base.js').write_text('var fixture={signatureTimestamp:20775};')
(replay/'home.html').write_text('<script>ytcfg.set({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"fixture"}}})</script>')
(replay/'browse.json').write_text('{"contents":{}}')
(replay/'player.json').write_text(json.dumps({'playabilityStatus': {'status': 'OK'}, 'videoDetails': {
    'videoId': 'F13fixture0', 'title': 'Bancada Cast · 720p30', 'lengthSeconds': '40'}, 'streamingData': {
    'expiresInSeconds': '3600', 'adaptiveFormats': [
        {'itag': 136, 'mimeType': 'video/mp4; codecs="avc1.64001f"', 'width': 1280, 'height': 720,
         'fps': 30, 'url': 'https://fixture.googlevideo.com/video'},
        {'itag': 140, 'mimeType': 'audio/mp4; codecs="mp4a.40.2"', 'bitrate': 128000,
         'url': 'https://fixture.googlevideo.com/audio'}]}}))
if not (replay/'video.mp4').exists():
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
                    'testsrc2=size=1280x720:rate=30:duration=40', '-c:v', 'libx264', '-preset', 'fast',
                    '-crf', '28', '-an', '-movflags', '+faststart', '-y', str(replay/'video.mp4')], check=True)
if not (replay/'audio.m4a').exists():
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
                    'sine=frequency=660:sample_rate=44100:duration=40', '-c:a', 'aac', '-b:a', '128k',
                    '-vn', '-movflags', '+faststart', '-y', str(replay/'audio.m4a')], check=True)
cert = proof/'cert.pem'
key = proof/'key.pem'
if not cert.exists():
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                    '-subj', '/CN=cast-fixture', '-keyout', str(key), '-out', str(cert)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def run(name, seconds, scenario=None, keys=''):
    receiver = Receiver(cert, key)
    audio = (replay/'audio.m4a').read_bytes()
    downloads = []
    class Audio(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            assert self.path == '/audio.m4a?itag=140'
            body = audio[:32768]
            self.send_response(206)
            self.send_header('Content-Type', 'audio/mp4')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Content-Range', f'bytes 0-{len(body)-1}/{len(audio)}')
            self.end_headers()
            self.wfile.write(body)
        def log_message(self, *args):
            pass
    audio_server = http.server.ThreadingHTTPServer(('127.0.0.1', 8233), Audio)
    audio_server.socket = receiver.context.wrap_socket(audio_server.socket, server_side=True)
    audio_thread = threading.Thread(target=audio_server.serve_forever, daemon=True)
    audio_thread.start()
    audio_url = 'https://127.0.0.1:8233/audio.m4a?itag=140'
    player = json.loads((replay/'player.json').read_text())
    player['streamingData']['adaptiveFormats'][1]['url'] = audio_url
    (replay/'player.json').write_text(json.dumps(player))
    def consume(url):
        assert url == audio_url
        request = urllib.request.Request(url, headers={'Range': 'bytes=0-32767'})
        with urllib.request.urlopen(request, context=ssl._create_unverified_context(), timeout=3) as response:
            assert response.status == 206
            body = response.read()
            assert body == audio[:32768] and body[4:8] == b'ftyp'
            downloads.append(len(body))
    receiver.consume = consume
    (assets/'nest.txt').write_text(f'127.0.0.1 {receiver.port}\nNest de bancada\n')
    if not keys:
        keys = '.'*(seconds-2)+'U'
    env = os.environ | {'SDL_VIDEO_DRIVER': 'offscreen', 'SDL_AUDIO_DRIVER': 'dummy',
                       'XT_ASSETS': str(assets), 'XT_SERVER': '127.0.0.1:8133',
                       'XT_YT_REPLAY': str(proof/'replay'), 'XT_SHOT': str(proof/(name+'.ppm')),
                       'XT_SHOT_MS': str(seconds*1000), 'XT_KEYS_MS': '1000', 'XT_KEYS': keys}
    with (proof/(name+'.log')).open('w') as log:
        app = subprocess.Popen([str(root/'client/build/tube360')], env=env, stdout=log, stderr=log)
        worker = None
        if scenario:
            worker = threading.Thread(target=scenario, args=(receiver,), daemon=True)
            worker.start()
        try:
            result = app.wait(seconds+20)
            assert result == 0, f'{name}: app exit {result}'
            assert (proof/(name+'.ppm')).stat().st_size > 1000000, name
        finally:
            if app.poll() is None:
                app.terminate()
                app.wait(5)
            if worker:
                worker.join(12)
            receiver.close()
            audio_server.shutdown()
            audio_thread.join(3)
            audio_server.server_close()
    (proof/(name+'-cast.json')).write_text(json.dumps(receiver.events, indent=2))
    assert not receiver.errors, receiver.errors
    log = (proof/(name+'.log')).read_text()
    assert 'primeiro quadro' in log, name
    assert receiver.commands('STOP'), f'{name}: missing stop'
    assert downloads, f'{name}: Cast did not fetch AAC'
    assert not (assets/'nest-owner.json').exists(), f'{name}: unconfirmed stop'
    print(name, 'LOAD', len(receiver.commands('LOAD')), 'SEEK', len(receiver.commands('SEEK')),
          'STOP', len(receiver.commands('STOP')), flush=True)
    return log, receiver.events


def idle(receiver):
    time.sleep(3)
    receiver.position = receiver.at()
    receiver.stamp = time.monotonic()
    receiver.state = 'IDLE'


def partition(receiver):
    time.sleep(3)
    receiver.online = False
    receiver.disconnect()
    time.sleep(9)
    receiver.online = True


def sparse(receiver):
    original = receiver.reply
    last = [0.0]
    def reply(client, request, payload, source=None):
        if payload.get('type') == 'MEDIA_STATUS':
            now = time.monotonic()
            if now-last[0] < 5 and json.loads(request.payload_utf8)['type'] == 'GET_STATUS':
                return
            last[0] = now
        original(client, request, payload, source)
    receiver.reply = reply


log, _ = run('pc-cast', 7)
assert 'som: Nest' in log
log, _ = run('pc-sparse', 12, sparse)
assert 'som: TV' not in log
log, _ = run('pc-tv-failover', 5, idle)
assert 'som: TV' in log
log, _ = run('pc-idle-recover', 13, idle)
assert 'som: TV' in log and log.count('som: Nest') >= 2
log, events = run('pc-partition', 20, partition)
assert 'som: TV' in log and log.count('som: Nest') >= 2
log, events = run('pc-seek', 10, keys='...R....U')
assert sum(ns.endswith('.media') and data['type'] == 'LOAD' for ns, data in events) == 1
assert sum(ns.endswith('.media') and data['type'] == 'SEEK' for ns, data in events) == 1
print('Desktop Cast playback, sparse status, quiet failover, recovery and stop verified', flush=True)
