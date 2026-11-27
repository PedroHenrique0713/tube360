#!/usr/bin/env python3
"""Celular lounge sobre conta, catálogo, Cast TLS e timer nativo; rede só em loopback."""
import contextlib
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
from urllib.parse import parse_qs, urlsplit

from e2e import Handler, Lounge, IDS, ROOT, dial, discover, wait_for
sys.path.insert(0, str(ROOT/'bench/standalone'))
from cast_test import Receiver

work = Path(sys.argv[1]).resolve()
work.mkdir(parents=True, exist_ok=True)
replay = work/'replay'
subprocess.run([sys.executable, str(ROOT/'bench/catalog/prepare-replay.py'), str(replay)], check=True)
sample = work/'sample.mp4'
if not sample.exists():
    subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=30',
        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100', '-t', '12', '-c:v', 'libx264',
        '-preset', 'ultrafast', '-threads', '2', '-pix_fmt', 'yuv420p', '-g', '30', '-c:a', 'aac',
        '-movflags', '+faststart', str(sample)], check=True)
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(sample), '-vn', '-c:a', 'copy',
    '-movflags', '+faststart', str(replay/'audio.m4a')], check=True)
subprocess.run(['magick', str(replay/'art.png'), str(work/'thumbnail.jpg')], check=True)
cert, key = work/'cert.pem', work/'key.pem'
subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
    '-subj', '/CN=cast-fixture', '-keyout', str(key), '-out', str(cert)], check=True,
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


class Audio(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        assert self.path == '/audio.m4a'
        data = (replay/'audio.m4a').read_bytes()
        self.send_response(200)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args):
        pass


audio_server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Audio)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(cert, key)
audio_server.socket = context.wrap_socket(audio_server.socket, server_side=True)
local_audio_url = f'https://127.0.0.1:{audio_server.server_port}/audio.m4a'
audio_url = 'https://fixture.googlevideo.com/audio.m4a'
manifest = json.loads((replay/'responses.json').read_text())
for entry in manifest['requests']:
    if entry['url'].startswith('https://music.youtube.com/youtubei/'):
        entry['authorization'] = 'fixture-access'
    if '/audio-F12audio' in entry['url']:
        entry['url'] = audio_url
for path in replay.glob('player-F12audio*.json'):
    player = json.loads(path.read_text())
    player['videoDetails']['lengthSeconds'] = '12'
    player['streamingData']['adaptiveFormats'][1]['url'] = audio_url
    path.write_text(json.dumps(player))
# A abertura pelo menu também funciona depois da seleção automática da conta nativa.
(replay/'music-home.json').write_text((replay/'radio.json').read_text())
(replay/'responses.json').write_text(json.dumps(manifest))
accounts = work/'account'
accounts.mkdir(exist_ok=True)
rules = json.loads((ROOT/'bench/account/fixtures/responses.json').read_text())
base_tile = next(r['json']['contents'][0] for r in rules if 'VLPLfixture' in r.get('contains', ''))
tiles = []
for index in range(1, 25):
    tile = json.loads(json.dumps(base_tile))
    video = f'F14race{index:04}'
    tile['tileRenderer']['contentId'] = video
    tile['tileRenderer']['onSelectCommand']['watchEndpoint']['videoId'] = video
    tiles.append(tile)
rules[0:0] = [
    {'url': '/youtubei/v1/browse?', 'contains': '"browseId":"VLPLF14race"',
     'json': {'contents': {'items': tiles, 'continuation': {'nextContinuationData': {'continuation': 'F14_PENDING'}}}}},
    {'url': '/youtubei/v1/browse?', 'contains': '"continuation":"F14_PENDING"',
     'delay_ms': 1800, 'json': {'contents': []}},
]
(accounts/'responses.json').write_text(json.dumps(rules))
shutil.copyfile(ROOT/'bench/account/fixtures/thumb.png', accounts/'thumb.png')
lounge = Lounge(work)
lounge.sleep_wire = 'ok\toff\t0\txbox\tminutos\n'
lounge.remote_commands = []
lounge.ended = []
lounge.owner_claims = 0
lounge.legacy_requests = []


class Combined(Handler):
    def get(self):
        path = urlsplit(self.path).path
        if path.startswith(('/api/account', '/api/sleep', '/api/nest/', '/api/feed',
                            '/api/thumb/', '/api/remote/', '/api/stream/')):
            lounge.legacy_requests.append(path)
        if path == '/api/account':
            self.response('', 503)
        elif path == '/api/sleep':
            self.response(lounge.sleep_wire)
        elif path == '/api/nest/owner':
            assert parse_qs(urlsplit(self.path).query) == {'owner': ['console']}
            assert self.headers['User-Agent'] == 'Tube360/1.0'
            # Este servidor falso não abre Cast: o único remetente está no app.
            lounge.owner_claims += 1
            self.response('QUIET\t1\n')
        elif path == '/api/feed':
            self.response('ok\t1\tReserva de demonstração\n'+IDS[2]+'\t12\tDemonstração\t\tVídeo da reserva\n')
        elif path.startswith('/api/thumb/'):
            self.response(lounge.thumbnail, headers={'Content-Type': 'image/jpeg'})
        elif path == '/api/remote/poll':
            args = parse_qs(urlsplit(self.path).query)
            if args.get('ended'):
                lounge.ended.append(args['ended'][0])
            self.response(lounge.remote_commands.pop(0) if lounge.remote_commands else 'none\n')
        elif path == '/audio.m4a':
            data = (replay/'audio.m4a').read_bytes()
            first, last = map(int, self.headers['Range'][6:].split('-'))
            last = min(last, len(data)-1)
            self.response(data[first:last+1], 206, {'Content-Range': f'bytes {first}-{last}/{len(data)}'})
        else:
            super().get()

    def post(self):
        if urlsplit(self.path).path != '/youtubei/v1/player':
            return super().post()
        data = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        video = data['videoId']
        assert video in IDS, 'obsolete navigation started '+video
        self.response({'playabilityStatus': {'status': 'OK'}, 'videoDetails': {
            'videoId': video, 'title': 'Celular sobre conta, Música e Nest', 'lengthSeconds': '12'},
            'streamingData': {'expiresInSeconds': '3600', 'formats': [
                {'itag': 22, 'width': 1280, 'height': 720, 'fps': 30,
                 'mimeType': 'video/mp4; codecs="avc1.64001f, mp4a.40.2"', 'url': 'https://fixture.googlevideo.com/sample.mp4'}],
                'adaptiveFormats': [{'itag': 140, 'mimeType': 'audio/mp4; codecs="mp4a.40.2"', 'bitrate': 128000, 'url': audio_url}]}})


server = http.server.ThreadingHTTPServer(('127.0.0.1', 8234), Combined)
server.daemon_threads = True
server.lounge = lounge
threads = [threading.Thread(target=s.serve_forever) for s in (server, audio_server)]
for thread in threads:
    thread.start()


@contextlib.contextmanager
def app(name, seconds, mode='off', race=False, timer=''):
    assets = work/name
    assets.mkdir(exist_ok=True)
    for suffix in ('.ppm', '.png'):
        (work/(name+suffix)).unlink(missing_ok=True)
    for font in (ROOT/'client/assets').glob('inter-*.ttf'):
        shutil.copyfile(font, assets/font.name)
    (assets/'settings.txt').write_text(f'lang=pt\ncols=4\nquality=720\nsound=nest\nserver={mode}\n')
    (assets/'yt-console-account.json').write_text(json.dumps({'access_token': 'fixture-access',
        'refresh_token': 'fixture-refresh', 'expires_at': int(time.time())+3600,
        'client': {'client_id': 'fixture-tv-client', 'client_secret': 'fixture-tv-secret'}, 'name': 'Conta F14 integrada'}))
    receiver = Receiver(cert, key)
    downloads = []

    def consume(url):
        assert url == audio_url
        with urllib.request.urlopen(local_audio_url, context=ssl._create_unverified_context(), timeout=3) as response:
            data = response.read()
            assert data[4:8] == b'ftyp'
            downloads.append(len(data))

    receiver.consume = consume
    (assets/'nest.txt').write_text(f'127.0.0.1 {receiver.port}\nNest de bancada\n')
    audit = work/(name+'-account.tsv')
    audit.unlink(missing_ok=True)
    music_audit = work/(name+'-music.tsv')
    music_audit.unlink(missing_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
    env.update(SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', LP_NUM_THREADS='2',
        XT_NEST_NETWORK='127.77.53.1/255.255.255.252',
        XT_ASSETS=str(assets), XT_SERVER='127.0.0.1:8234', XT_ACCOUNT_FIXTURES=str(accounts), XT_ACCOUNT_AUDIT=str(audit),
        XT_CATALOG_REPLAY=str(replay), XT_CATALOG_AUDIT=str(music_audit), XT_KEYS_MS='200',
        XT_SHOT=str(work/(name+'.ppm')), XT_SHOT_FRAME='2147483647', XT_SHOT_MS=str(int(seconds*1000)))
    if mode == 'auto':
        # A Música da conta agora tem uma prateleira de atalhos antes dos vídeos.
        env.update(XT_SCREEN='rail', XT_KEYS='DDDDADAA')
    else:
        env.update(XT_NAV='playlist' if race else 'radio', XT_NAV_ID='PLF14race' if race else 'F12audio001', XT_KEYS='UA')
        if not race:
            env['XT_MUSIC'] = '1'
    prefix = env['XT_KEYS']
    env['XT_KEYS'] = prefix+'.'*max(0, int(seconds*5)-3-len(prefix))+'U'
    if timer == 'fade':
        env.pop('XT_SCREEN', None)
        env.update(XT_PLAY=IDS[0], XT_SLEEP_PATH='/api/sleep/set?what=xbox&min=0.5',
                   XT_KEYS='.'*39+'B'+'.'*15+'L')  # L acorda os controles; U agora ajusta o volume com eles escondidos
    elif timer == 'end':
        env['XT_SLEEP_PATH'] = '/api/sleep/set?what=xbox&end=1'
    with lounge.cv:
        lounge.pending.clear()
        code = lounge.code_count
    owner_claims = lounge.owner_claims
    log_path = work/(name+'.log')
    started = time.monotonic()
    with log_path.open('w') as log:
        process = subprocess.Popen([str(ROOT/'client/build/cast-test-app')], cwd=ROOT, env=env, stdout=log, stderr=log)
        try:
            wait_for(lambda: lounge.code_count > code, 'new lounge session')
            yield receiver, log_path, audit, time.monotonic()
            assert process.wait(seconds+20) == 0
            if mode == 'auto':
                assert lounge.owner_claims == owner_claims, 'legacy server was asked for Nest ownership'
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(10)
            receiver.close()
            (work/(name+'-cast.json')).write_text(json.dumps(receiver.events, indent=2))
    assert downloads and not receiver.errors, receiver.errors
    assert receiver.commands('STOP') and not (assets/'nest-owner.json').exists()
    assert '/api/stats/' in audit.read_text(), 'account history was not updated'
    if timer == 'end':
        assert 'timer: prazo do console venceu, desligando' in log_path.read_text()
    else:
        subprocess.run(['magick', str(work/(name+'.ppm')), str(work/(name+'.png'))], check=True)
    print('PASS', name, 'AAC downloads', len(downloads), flush=True)


def pair():
    discover()
    assert dial('POST', '/apps/YouTube', 'pairingCode=FAKE-PHONE-CODE')[0] == 201
    lounge.message('loungeStatus', {'devices': json.dumps([{'id': 'PHONE', 'type': 'REMOTE_CONTROL', 'name': 'Celular falso F14', 'theme': 'cl'}])})


def send_video(position=0):
    mark = lounge.mark()
    lounge.message('setPlaylist', {'listId': 'FAKE-QUEUE', 'videoIds': ','.join(IDS[:2]),
        'videoId': IDS[0], 'currentIndex': '0', 'currentTime': str(position)})
    wait_for(lambda: lounge.seen('nowPlaying', lambda p: p.get('videoId') == IDS[0] and p.get('state') == '1', mark), 'phone playback')


try:
    with app('phone-over-music-nest', 7) as (nest, log, audit, _):
        wait_for(lambda: 'musica: primeiro audio' in log.read_text() and 'som: Nest' in log.read_text(), 'account Music AAC on Nest')
        pair()
        assert abs(nest.volume-.42) < .001, 'initial Nest volume was overwritten'
        mark = lounge.mark()
        lounge.message('getVolume', {})
        wait_for(lambda: lounge.seen('onVolumeChanged', lambda p: abs(float(p['volume'])-42) < .01, mark), 'initial Nest volume reaches phone')
        send_video()
        lounge.message('setVolume', {'volume': '35', 'muted': 'false'})
        wait_for(lambda: abs(nest.volume-.35) < .001, 'Nest volume 35%')
        lounge.message('setVolume', {'volume': '35', 'muted': 'true'})
        wait_for(lambda: nest.muted, 'Nest mute')
        lounge.message('setVolume', {'volume': '70', 'muted': 'false'})
        wait_for(lambda: abs(nest.volume-.7) < .001 and not nest.muted, 'Nest unmute 70%')
        mark = lounge.mark()
        lounge.message('seekTo', {'newTime': '3'})
        wait_for(lambda: lounge.seen('nowPlaying', lambda p: p.get('videoId') == IDS[0] and float(p.get('currentTime', 0)) >= 3, mark), 'phone seek with Nest')
    text = log.read_text()
    assert 'video 0 quadros' in text and 'primeiro quadro' in text
    assert 'F12audio002' not in text and 'F12audio003' not in text
    assert 'authorized_fixture' in (work/'phone-over-music-nest-music.tsv').read_text()

    with app('phone-volume-timer', 12, mode='auto', timer='fade') as (nest, log, audit, _):
        wait_for(lambda: 'som: Nest' in log.read_text(), 'native video on Nest')
        pair()
        send_video()
        lounge.remote_commands.append('play\tF14race0001\t0\tFila antiga\tnavigation\n')
        wait_for(lambda: 'timer: on' in log.read_text(), 'timer fade received')
        lounge.message('setVolume', {'volume': '35', 'muted': 'false'})
        wait_for(lambda: .14 < nest.volume < .18, 'phone 35% times timer gain')
        lounge.message('setVolume', {'volume': '35', 'muted': 'true'})
        wait_for(lambda: nest.muted, 'mute during timer fade')
        lounge.message('setVolume', {'volume': '70', 'muted': 'false'})
        wait_for(lambda: .25 < nest.volume < .36 and not nest.muted, 'unmute retains timer fade')
        wait_for(lambda: 'timer: off' in log.read_text(), 'controller cancels native timer')
        wait_for(lambda: abs(nest.volume-.7) < .001, 'timer cancellation restores phone volume')
    assert 'cast: play F14race' not in log.read_text()
    assert abs(nest.volume-.7) < .001

    with app('phone-cancels-pending-navigation', 5, race=True) as (nest, log, audit, _):
        wait_for(lambda: audit.exists() and 'fixture_delay_ms=1800' in audit.read_text(), 'pending private queue request')
        pair()
        send_video()
        time.sleep(2)
        assert 'cast: play F14race' not in log.read_text() and 'player: encerrando F14race' not in log.read_text()
    assert not lounge.errors, lounge.errors

    with app('timer-blocks-phone-next', 9, mode='auto', timer='end') as (nest, log, audit, _):
        wait_for(lambda: 'musica: primeiro audio' in log.read_text(), 'initial Music before timer')
        pair()
        wait_for(lambda: 'fim_do_video' in log.read_text(), 'end-of-video timer bound')
        send_video(11)
        wait_for(lambda: 'timer: fim de video, aguardando desligar' in log.read_text(), 'timer suppresses lounge auto-next')
        lounge.message('next', {})
        time.sleep(.4)
        assert 'cast: play '+IDS[1] not in log.read_text()
        lounge.message('setPlaylist', {'videoIds': IDS[1], 'videoId': IDS[1]})
        wait_for(lambda: 'timer: prazo do console venceu, desligando' in log.read_text(), 'native timer wins over queued phone command')
    assert 'cast: play '+IDS[1] not in log.read_text()
    assert not lounge.errors, lounge.errors
    assert not lounge.legacy_requests, lounge.legacy_requests
    (work/'events.json').write_text(json.dumps(lounge.events, ensure_ascii=False, indent=2))
    print('PASS integrated account, Music/channel queue priority, Nest volume/mute/fade, pending navigation and timer priority', flush=True)
finally:
    for server in (server, audio_server):
        server.shutdown()
        server.server_close()
    for thread in threads:
        thread.join()
