#!/usr/bin/env python3
"""App de PC + servidor real isolado + Nest TLS. Nenhum acesso ao YouTube/Nest físico."""
import asyncio
from dataclasses import replace
import http.server
import importlib.util
import json
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import sys
import threading
import time
import urllib.request
from unittest.mock import patch
import uvicorn
from owner_receiver import OwnerReceiver

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT/'.work/owner-e2e'
WORK.mkdir(parents=True, exist_ok=True)
REPLAY = WORK/'replay'
REPLAY.mkdir(exist_ok=True)
VIDEO = 'F18fixture0'
MP4 = REPLAY/'video.mp4'
if not MP4.exists():
    subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=30',
        '-f', 'lavfi', '-i', 'sine=frequency=660:sample_rate=44100', '-t', '24', '-c:v', 'libx264',
        '-preset', 'fast', '-crf', '30', '-threads', '2', '-c:a', 'aac', '-movflags', '+faststart', str(MP4)], check=True)
    subprocess.run(['ffmpeg', '-v', 'error', '-i', str(MP4), '-vn', '-c:a', 'copy', '-movflags', '+faststart',
        str(REPLAY/'audio.m4a')], check=True)
for name, text in {'watch.html': '<script>ytcfg.set({"VISITOR_DATA":"fixture","jsUrl":"/s/player/fixture/base.js"})</script>',
                   'base.js': 'var fixture={signatureTimestamp:20775};',
                   'home.html': '<script>ytcfg.set({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"fixture"}}})</script>',
                   'browse.json': '{"contents":{}}'}.items():
    (REPLAY/name).write_text(text)
cert, key = WORK/'cert.pem', WORK/'key.pem'
subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=cast-fixture',
                '-keyout', str(key), '-out', str(cert)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
config = WORK/'config.json'
config.write_text(json.dumps({'nest': {'host': '127.0.0.1'}, 'prefetch': 0}))
with patch.dict(os.environ, HOME=str(WORK/'home'), TUBE360_PORT='8138', TUBE360_CONFIG=str(config),
                TUBE360_ACCOUNT_URL='http://127.0.0.1:8238'), patch.object(threading.Thread, 'start'):
    spec = importlib.util.spec_from_file_location('owner_e2e_server', ROOT/'server/tube360_server.py')
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)


class Account(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        data = (REPLAY/'audio.m4a').read_bytes() if self.path == '/audio.m4a' else json.dumps({
            'signed_in': False, 'configured': False, 'status': 'signed_out', 'items': []}).encode()
        self.send_response(200)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def log_message(self, *args):
        pass


account = http.server.ThreadingHTTPServer(('127.0.0.1', 8238), Account)
account_thread = threading.Thread(target=account.serve_forever)
account_thread.start()
audio_server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Account)
audio_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
audio_context.load_cert_chain(cert, key)
audio_server.socket = audio_context.wrap_socket(audio_server.socket, server_side=True)
audio_thread = threading.Thread(target=audio_server.serve_forever)
audio_thread.start()


async def resolve(video_id):
    return {'video': {'url': str(MP4)}, 'audio': {'url': str(MP4)}, 'headers': {}, 'duration': 24,
            'live': False, 'title': 'Bancada Nest F18 · 720p30', 'original': {}, 'audio_tracks': [], 'video_formats': []}


async def feed(*args, **kwargs):
    return [{'id': VIDEO, 'title': 'Bancada Nest F18', 'duration': 24, 'channel': 'Simulação local', 'views': ''}]


async def empty_extract(*args, **kwargs):
    return {'entries': []}


async def no_more(*args, **kwargs):
    pass


def thumbnail(video_id, width, height, path):
    m.Image.new('RGB', (width, height), (30, 35, 40)).save(path, 'JPEG')
    return path


m.resolve_video = resolve
m.home_videos = m.search_videos = feed
m.home_more = no_more
m.extract_video = empty_extract
m.make_thumb = thumbnail
m.source_args = lambda info, source, start: ['-ss', str(start), '-i', source['url']]
m.prefetch_later = lambda items: None
server = uvicorn.Server(uvicorn.Config(m.app, host='127.0.0.1', port=8138, log_level='warning', lifespan='off'))
server_thread = threading.Thread(target=server.run)
server_thread.start()
while not server.started:
    time.sleep(.02)


def scenario(name, native=True, reject=False, transition=False, silent=False, server_mode='auto', offline=False, retry=False):
    nest = OwnerReceiver(cert, key)
    nest.reject_launch = reject
    nest.ignore_launch = silent
    assets = WORK/name
    assets.mkdir(exist_ok=True)
    for font in (ROOT/'client/assets').glob('inter-*.ttf'):
        shutil.copyfile(font, assets/font.name)
    (assets/'settings.txt').write_text(f'lang=pt\ncols=4\nquality=720\nsound=nest\nserver={server_mode}\n')
    (assets/'nest.txt').write_text(f'127.0.0.1 {nest.port}\nNest simulado\n')
    (assets/'autoplay.txt').write_text(VIDEO+' 0\n')
    player = {'playabilityStatus': {'status': 'OK' if native else 'ERROR', 'reason': 'fixture native unavailable'},
        'videoDetails': {'videoId': VIDEO, 'title': 'Bancada Nest F18 · 720p30', 'lengthSeconds': '24'},
        'streamingData': {'expiresInSeconds': '3600', 'formats': [{'itag': 22, 'width': 1280, 'height': 720, 'fps': 30,
            'mimeType': 'video/mp4; codecs="avc1.64001f, mp4a.40.2"', 'url': 'https://fixture.googlevideo.com/video'}],
            'adaptiveFormats': [{'itag': 140, 'mimeType': 'audio/mp4; codecs="mp4a.40.2"', 'bitrate': 128000,
                                 'url': f'https://127.0.0.1:{audio_server.server_port}/audio.m4a'}]}}
    (REPLAY/'player.json').write_text(json.dumps(player))
    with patch.object(threading.Thread, 'start'):
        m.nest = m.Nest()
    m.nest.owner_file = assets/'server-owner.json'
    m.nest.loaded_id = ''
    threading.Thread(target=m.nest.watch, daemon=True).start()
    original = m.pychromecast.get_chromecast_from_cast_info
    def factory(info, **kwargs):
        return original(replace(info, port=nest.port, services={m.HostServiceInfo('127.0.0.1', nest.port)}), **kwargs)
    downloads = []
    def consume(url):
        if url.startswith('http://127.0.0.1:8138/api/audio/'):
            with urllib.request.urlopen(url, timeout=6) as response:
                downloads.append(len(response.read(8192)))
        else:
            with urllib.request.urlopen(url, context=ssl._create_unverified_context(), timeout=3) as response:
                data = response.read()
                assert data[4:8] == b'ftyp'
                downloads.append(len(data))
    nest.consume = consume
    seconds = 14 if transition else 12 if retry else 6 if silent else 4 if reject else 8
    env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
    env.update(SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', LP_NUM_THREADS='2',
        XT_ASSETS=str(assets), XT_SERVER='127.0.0.1:9' if offline else '127.0.0.1:8138', XT_YT_REPLAY=str(REPLAY), XT_CAST_DISABLE='1',
        XT_SHOT=str(WORK/(name+'.ppm')), XT_SHOT_MS=str(seconds*1000), XT_SHOT_FRAME='2147483647',
        XT_KEYS_MS='1000', XT_KEYS='.'*(seconds-2)+'U')
    if retry:
        env['XT_KEYS'] = '.....X....U'
    log_path = WORK/(name+'.log')
    with patch.object(m.pychromecast, 'get_chromecast_from_cast_info', side_effect=factory), log_path.open('w') as log:
        if transition:
            m.nest.prepare()
            assert m.nest.connected()
        process = subprocess.Popen([str(ROOT/'client/build/tube360')], env=env, stdout=log, stderr=log)
        try:
            if retry:
                until = time.monotonic()+5
                while time.monotonic() < until and 'som: TV' not in log_path.read_text():
                    time.sleep(.02)
                assert 'som: TV' in log_path.read_text(), 'initial failure never reached TV'
                nest.reject_launch = False
            if transition:
                until = time.monotonic()+6
                while time.monotonic() < until and not nest.commands('LOAD'):
                    time.sleep(.02)
                assert nest.commands('LOAD'), 'native did not load'
                # A fila do PC abre a reserva enquanto o console ainda é o dono do Cast nativo.
                m.remote.commands.append({'action': 'play', 'id': VIDEO, 't': 4, 'source': 'phone', 'at': time.monotonic()})
            assert process.wait(seconds+20) == 0
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(8)
            rev = m.nest.stop_intent(closing=False)
            if m.nest.may_sound:
                m.nest.command('stop', rev)
            m.nest.want_stop = False
            m.nest.stopping_token = ''
            m.nest.drop()
            nest.close()
    text = log_path.read_text()
    (WORK/(name+'-wire.json')).write_text(json.dumps(nest.wire, indent=2))
    assert not nest.errors, nest.errors
    assert 'primeiro quadro' in text, text[-5000:]
    if (reject or silent) and not retry:
        assert 'som: TV' in text, text[-5000:]
        assert not nest.commands('LOAD'), nest.events
        if reject:
            launches = [t for t, _, kind in nest.wire if kind == 'LAUNCH']
            # A pychromecast repete uma recusa imediata dentro do mesmo start_app.
            assert 1 <= len(launches) <= (1 if native else 2) and max(launches)-min(launches) < .5
            if not native:
                assert text.count('LAUNCH/LOAD delegado') == 1
    else:
        assert 'som: Nest' in text, text[-5000:]
        assert downloads
        if retry:
            assert 'som: TV' in text
    if transition:
        wire = nest.wire
        loads = [(t, sender) for t, sender, kind in wire if kind == 'LOAD']
        assert len(loads) >= 2 and loads[0][1].startswith('sender-tube360-') and loads[-1][1] == 'sender-0', wire
        assert any(loads[0][0] < t < loads[-1][0] and kind == 'DISCONNECT' and sender == loads[0][1] for t, sender, kind in wire), wire
    capture = next(line for line in text.splitlines() if 'captura: frame' in line)
    position = float(capture.rsplit('posição ', 1)[1])
    assert position > max(1, seconds-4), ('picture stalled', capture)
    assert not any(kind == 'LAUNCH_ERROR' for _, _, kind in nest.wire) or reject
    subprocess.run(['magick', str(WORK/(name+'.ppm')), str(WORK/(name+'.png'))], check=True)
    print('PASS', name, 'LOAD', len(nest.commands('LOAD')), 'downloads', downloads, flush=True)


try:
    for args in [('native-launch-error', True, True), ('reserve-launch-error', False, True),
                 ('native-launch-silent', True, False, False, True), ('reserve-launch-silent', False, False, False, True),
                 ('native-server-off', True, False, False, False, 'off'),
                 ('native-server-absent', True, False, False, False, 'auto', True),
                 ('reserve-nest', False, False), ('native-to-reserve', True, False, True),
                 ('native-retry', True, True, False, False, 'auto', False, True),
                 ('reserve-retry', False, True, False, False, 'auto', False, True)]:
        if not sys.argv[1:] or args[0] in sys.argv[1:]:
            scenario(*args)
finally:
    server.should_exit = True
    server_thread.join(12)
    account.shutdown()
    account.server_close()
    account_thread.join(3)
    audio_server.shutdown()
    audio_server.server_close()
    audio_thread.join(3)
