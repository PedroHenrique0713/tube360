#!/usr/bin/env python3
"""F15 composta com conta, catálogo e Nest TLS locais, nas duas saídas físicas."""
import array
import http.server
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

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'bench/standalone'))
from cast_test import Receiver

work = Path(sys.argv[1]).resolve()
media, replay = work/'media', work/'replay'
work.mkdir(parents=True, exist_ok=True)
assert (media/'audio-pt.m4a').exists(), 'run prepare.py --mode both --audio-tracks first'
subprocess.run([sys.executable, str(ROOT/'bench/catalog/prepare-replay.py'), str(replay)], check=True)
subprocess.run([sys.executable, str(ROOT/'bench/catalog/build-replay.py'), str(work)], check=True)
cert, key = work/'cert.pem', work/'key.pem'
subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
    '-subj', '/CN=cast-fixture', '-keyout', str(key), '-out', str(cert)], check=True,
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


class Audio(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        assert self.path in ('/audio.m4a', '/audio-pt.m4a')
        data = (media/self.path[1:]).read_bytes()
        self.send_response(200)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args):
        pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Audio)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(cert, key)
server.socket = context.wrap_socket(server.socket, server_side=True)
server_thread = threading.Thread(target=server.serve_forever)
server_thread.start()
origin = f'https://127.0.0.1:{server.server_port}'
manifest = json.loads((replay/'responses.json').read_text())
player = json.loads((media/'player.json').read_text())
for entry in manifest['requests']:
    if entry['url'].startswith('https://music.youtube.com/youtubei/'):
        entry['authorization'] = 'fixture-access'
for path in media.glob('*'):
    if path.suffix not in ('.mp4', '.m4a'):
        continue
    shutil.copyfile(path, replay/path.name)
    url = origin+'/'+path.name if path.suffix == '.m4a' else 'https://media.example/'+path.name
    manifest['requests'].append({'url': url, 'file': path.name, 'range': True})
for fmt in player['streamingData']['adaptiveFormats']:
    if 'audio/' in fmt['mimeType']:
        fmt['url'] = origin+'/'+fmt['url'].rsplit('/', 1)[1]
for video in ('4p_9OPKgcDE', 'F12audio001', 'F12audio002', 'F12audio003'):
    data = json.loads(json.dumps(player))
    data['videoDetails'].update(videoId=video, title='Original e dublagem — bancada F15')
    filename = f'player-{video}.json'
    (replay/filename).write_text(json.dumps(data))
    if video == '4p_9OPKgcDE':
        manifest['requests'].extend([
            {'url': f'https://www.youtube.com/watch?v={video}&bpctr=9999999999&has_verified=1', 'file': 'watch.html'},
            {'url': 'https://www.youtube.com/youtubei/v1/player?prettyPrint=false', 'fields': {'videoId': video}, 'file': filename}])
(replay/'responses.json').write_text(json.dumps(manifest))
accounts = ROOT/'bench/account/fixtures'
for index in range(1, 9):
    manifest['requests'].append({'url': f'https://i.ytimg.com/vi/video{index:06}/mqdefault.jpg', 'file': 'art.png'})
(replay/'responses.json').write_text(json.dumps(manifest))
wanted = set(sys.argv[2:])
previous = json.loads((work/"results.json").read_text()) if (work/"results.json").exists() else []
results = [item for item in previous if item["name"] not in wanted] if wanted else []


def run(name, height, seconds=5.5, preference='original', speaker=False, **config):
    if wanted and name not in wanted:
        return
    assets = work/name
    assets.mkdir(exist_ok=True)
    for font in (ROOT/'client/assets').glob('inter-*.ttf'):
        shutil.copyfile(font, assets/font.name)
    (assets/'settings.txt').write_text(f'lang=pt\ncols=4\nquality=720\nsound={"nest" if speaker else "tv"}\n'
        f'server=off\noutput_height={height}\nvideo_mode=1080p30\naudio_preference={preference}\n')
    (assets/'yt-console-account.json').write_text(json.dumps({'access_token': 'fixture-access',
        'refresh_token': 'fixture-refresh', 'expires_at': int(time.time())+3600,
        'client': {'client_id': 'fixture-tv-client', 'client_secret': 'fixture-tv-secret'}, 'name': 'Conta integrada F15'}))
    nest = Receiver(cert, key) if speaker else None
    downloads = []
    if nest:
        (assets/'nest.txt').write_text(f'127.0.0.1 {nest.port}\nNest de bancada F15\n')

        def consume(url):
            assert url in (origin+'/audio.m4a', origin+'/audio-pt.m4a')
            with urllib.request.urlopen(url, context=ssl._create_unverified_context(), timeout=3) as response:
                data = response.read()
            pcm = subprocess.run(['ffmpeg', '-v', 'error', '-i', 'pipe:0', '-t', '1', '-ac', '1',
                '-ar', '44100', '-f', 's16le', 'pipe:1'], input=data, capture_output=True, check=True).stdout
            samples = array.array('h', pcm)
            hz = sum(a <= 0 < b for a, b in zip(samples, samples[1:]))*44100/len(samples)
            assert abs(hz-(440 if 'audio-pt' in url else 1000)) < 5
            downloads.append({'url': url, 'hz': hz})

        nest.consume = consume
    env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
    env.update(XT_CAST_DISABLE='1', SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', LP_NUM_THREADS='2',
        XT_ASSETS=str(assets), XT_SERVER='127.0.0.1:8135', XT_ACCOUNT_FIXTURES=str(accounts),
        XT_ACCOUNT_AUDIT=str(work/(name+'-account.tsv')), XT_CATALOG_REPLAY=str(replay),
        XT_CATALOG_AUDIT=str(work/(name+'-music.tsv')), XT_SHOT=str(work/(name+'.ppm')),
        XT_SHOT_FRAME='2147483647', XT_SHOT_MS=str(int(seconds*1000)), XT_KEYS_MS='500')
    env.update(config)
    try:
        with (work/(name+'.log')).open('w') as log:
            subprocess.run([str(work/'tube360-replay')], env=env, cwd=ROOT, stdout=log, stderr=log,
                check=True, timeout=seconds+30)
    finally:
        if nest:
            nest.close()
            (work/(name+'-cast.json')).write_text(json.dumps(nest.events, indent=2))
    subprocess.run(['magick', str(work/(name+'.ppm')), str(work/(name+'.png'))], check=True)
    size = subprocess.check_output(['identify', '-format', '%wx%h', str(work/(name+'.png'))], text=True)
    assert size == ('1920x1080' if height == 1080 else '1280x720'), size
    text = (work/(name+'.log')).read_text()
    if config.get('XT_PLAY'):
        assert 'primeiro quadro' in text and '1080p' in text, text[-1500:]
    if nest:
        assert not nest.errors and downloads, nest.errors
        assert nest.commands('STOP') and not (assets/'nest-owner.json').exists()
        loads = nest.commands('LOAD')
        paths = [load['media']['contentId'].rsplit('/', 1)[1] for load in loads]
        if 'paused' in name:
            assert paths == ['audio.m4a', 'audio-pt.m4a'], paths
            assert loads[1]['autoplay'] is False, 'paused track change started the Nest'
            assert loads[1]['currentTime'] > .3, 'track change lost the position'
            assert abs(nest.position-loads[1]['currentTime']) < .2, 'Nest advanced while paused'
        elif 'roundtrip' in name:
            assert paths == ['audio.m4a', 'audio-pt.m4a', 'audio.m4a'], paths
            assert loads[1]['currentTime'] > 1 and loads[2]['currentTime'] >= loads[1]['currentTime']
        else:
            assert paths == ['audio-pt.m4a' if preference == 'interface' else 'audio.m4a'], paths
        assert abs(nest.volume-.42) < .001, 'audio switch changed Nest volume'
    results.append({'name': name, 'size': size, 'downloads': downloads})
    print('PASS', name, size, downloads, flush=True)


try:
    for height in (720, 1080):
        run(f'home-{height}', height)
        run(f'music-{height}', height, XT_NAV='music', XT_MUSIC='1')
        run(f'channel-{height}', height, XT_NAV='channel', XT_NAV_ID='UC_x5XG1OV2P6uZZ5FSM9Ttw')
        run(f'player-row-{height}', height, seconds=4.5, XT_PLAY='4p_9OPKgcDE', XT_KEYS='......U')
        run(f'settings-{height}', height, seconds=6.5, XT_SCREEN='settings', XT_KEYS='DDDDDDDDDD')
    run('nest-audio-roundtrip', 1080, seconds=9, speaker=True, XT_PLAY='4p_9OPKgcDE', XT_KEYS='..ULADA..AUA')
    run('nest-paused-audio', 1080, seconds=7, speaker=True, XT_PLAY='4p_9OPKgcDE', XT_KEYS='..AULADA')
    run('nest-music-interface', 720, seconds=5, preference='interface', speaker=True,
        XT_NAV='radio', XT_NAV_ID='F12audio001', XT_MUSIC='1', XT_KEYS='UA')
    (work/'results.json').write_text(json.dumps(results, indent=2))
finally:
    server.shutdown()
    server.server_close()
    server_thread.join()
