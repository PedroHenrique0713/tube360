"""Capturas de membros com replay fechado e servidor real isolado. Não acessa YouTube/Xbox."""
import copy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import threading
import time
from urllib.request import urlopen

root = Path(__file__).resolve().parents[2]
proof = Path(sys.argv[1]).resolve()
proof.mkdir(parents=True, exist_ok=True)
replay = proof / 'replay'
for script, dest in [('prepare-replay.py', replay), ('build-replay.py', proof)]:
    subprocess.run([sys.executable, str(root / 'bench/catalog' / script), str(dest)], check=True)
rules = json.loads((replay / 'responses.json').read_text())['requests']
node = json.loads((replay / 'members-pt.json').read_text())['contents'][0]['lockupViewModel']
nodes = []
for i, title in enumerate(['Exclusivo para membros', 'Membro com acesso', 'Vídeo público']):
    v = copy.deepcopy(node)
    v['contentId'] = f'member0000{i+1}'
    v['metadata']['lockupMetadataViewModel']['title']['content'] = title
    if i == 1:
        v['isPlayable'] = True
    elif i == 2:
        v['metadata']['lockupMetadataViewModel']['metadata'] = {}
    nodes.append({'lockupViewModel': v})
page = {'contents': {'twoColumnBrowseResultsRenderer': {'tabs': [{'tabRenderer': {
    'selected': True, 'title': 'Videos', 'content': nodes}}]}}}
(replay / 'members-ui.json').write_text(json.dumps(page))
web, music = 'https://www.youtube.com/', 'https://music.youtube.com/'
for origin in (web, music):
    for route in ('browse', 'search', 'next'):
        for suffix in ('', '&alt=json'):
            rules.insert(0, {'url': origin + 'youtubei/v1/' + route + '?prettyPrint=false' + suffix, 'file': 'members-ui.json'})
for i in range(1, 4):
    video = f'member0000{i}'
    rules.append({'url': web + 'watch?v=' + video + '&bpctr=9999999999&has_verified=1', 'file': 'watch.html'})
    rules.append({'url': f'https://i.ytimg.com/vi/{video}/mqdefault.jpg', 'file': 'art.png'})
    filename = 'members-player.json' if i == 1 else 'player-member.json'
    rules.insert(0, {'url': web + 'youtubei/v1/player?prettyPrint=false', 'fields': {'videoId': video}, 'file': filename})
player = json.loads((replay / 'player-F12audio001.json').read_text())
player['videoDetails'].update(videoId='member00002', title='Membro com acesso')
(replay / 'player-member.json').write_text(json.dumps(player))
accounts = proof / 'accounts'
accounts.mkdir(exist_ok=True)
(accounts / 'responses.json').write_text(json.dumps([
    {'url': '/youtubei/v1/player?', 'contains': 'member00002', 'authorization': 'fixture-access', 'json': player},
    {'url': '/account/accounts_list', 'json': {'contents': []}},
    {'url': '/youtubei/v1/browse?', 'json': page},
]))
(replay / 'responses.json').write_text(json.dumps({'requests': rules}))

entries = [dict(id=f'member0000{i+1}', type='video', duration=600, channel='Canal de teste', title=title,
                membership=state, views_text='', channel_id='', section='Seleção de membros')
           for i, (title, state) in enumerate([('Exclusivo para membros', 'members'), ('Membro com acesso', 'member'), ('Vídeo público', '')])]
class Account(BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def do_GET(self):
        body = dict(state='signed_in', name='Conta fictícia', handle='fixture') if self.path.startswith('/status') else dict(items=entries, more=False)
        data = json.dumps(body).encode()
        self.send_response(200); self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)

home = proof / 'server-home'
thumbs = home / '.cache/tube360/thumbs'
thumbs.mkdir(parents=True, exist_ok=True)
for entry in entries:
    subprocess.run(['magick', str(replay / 'art.png'), str(thumbs / (entry['id'] + '_320x180.jpg'))], check=True)
config = json.loads((root / 'server/config.example.json').read_text())
config.pop('cookies_from_browser', None)
config['home_queries'] = config['warm_queries'] = []
(proof / 'config.json').write_text(json.dumps(config))
base = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
base.update(HOME=str(home), PYTHONUSERBASE='/home/pedrohenriquequadro/.local',
            TUBE360_CONFIG=str(proof / 'config.json'), TUBE360_ACCOUNT_URL='http://127.0.0.1:8250',
            TUBE360_PORT='8150', TUBE360_WARM_CACHE='0', TUBE360_SLEEP_TEST='1')
# Recusa uma porta de outra frente antes de iniciar qualquer processo.
with socket.socket() as probe:
    probe.bind(('127.0.0.1', 8150))
account = ThreadingHTTPServer(('127.0.0.1', 8250), Account)
threading.Thread(target=account.serve_forever, daemon=True).start()
server = None
try:
    with (proof / 'server.log').open('w') as log:
        server = subprocess.Popen([sys.executable, str(root / 'server/tube360_server.py')], env=base, stdout=log, stderr=subprocess.STDOUT)
        for attempt in range(100):
            if server.poll() is not None: raise RuntimeError('isolated server stopped')
            try:
                with urlopen('http://127.0.0.1:8150/api/remote/state', timeout=1) as response: response.read()
                break
            except OSError: time.sleep(.1)
        else: raise RuntimeError('isolated server did not start')
        cases = [('home', {}), ('search', {'XT_NAV': 'search', 'XT_NAV_QUERY': 'fixture'}),
                 ('channel', {'XT_NAV': 'channel', 'XT_NAV_ID': 'UC0000000000000000000001'}),
                 ('music', {'XT_NAV': 'music', 'XT_MUSIC': '1'}),
                 ('denied', {'XT_KEYS': 'A'}), ('access', {'XT_KEYS': 'RA'}),
                 ('music-denied', {'XT_NAV': 'radio', 'XT_NAV_ID': 'member00001', 'XT_MUSIC': '1', 'XT_KEYS': 'A'}),
                 ('auth-access', {'auth': True, 'XT_KEYS': 'RA'}),
                 ('auth-audio', {'auth': True, 'XT_NAV': 'radio', 'XT_NAV_ID': 'member00001', 'XT_MUSIC': '1', 'XT_KEYS': 'RA'}),
                 ('server-home', {'server': True}), ('server-library', {'server': True, 'XT_NAV': 'library'})]
        for lang in ('pt', 'en'):
            for name, options in cases:
                assets = proof / ('assets-' + name + '-' + lang)
                shutil.copytree(replay / 'assets', assets, dirs_exist_ok=True)
                (assets / 'settings.txt').write_text(f'lang={lang}\ncols=4\nquality=720\nsound=tv\nserver={"auto" if options.get("server") else "off"}\n')
                env = dict(base, SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', LP_NUM_THREADS='2', XT_CAST_DISABLE='1',
                           XT_ASSETS=str(assets), XT_SERVER='127.0.0.1:8150', XT_CATALOG_REPLAY=str(replay),
                           XT_SHOT=str(proof / f'{name}-{lang}.ppm'), XT_SHOT_FRAME='2147483647', XT_SHOT_MS='2100', XT_KEYS_MS='550')
                if options.get('auth'):
                    (assets / 'yt-console-account.json').write_text(json.dumps({'access_token': 'fixture-access',
                        'refresh_token': 'fixture-refresh', 'expires_at': 4070908800,
                        'client': {'client_id': 'fixture-client', 'client_secret': 'fixture-secret'}, 'name': 'Conta fictícia'}))
                    (assets / 'yt-console-account.json').chmod(0o600)
                    env.update(XT_ACCOUNT_FIXTURES=str(accounts), XT_ACCOUNT_AUDIT=str(proof / f'{name}-{lang}.audit'))
                env.update({k: v for k, v in options.items() if k.startswith('XT_')})
                with (proof / f'{name}-{lang}.log').open('w') as app_log:
                    subprocess.run([str(proof / 'tube360-replay')], env=env, stdout=app_log, stderr=subprocess.STDOUT, timeout=25, check=True)
                if name in ('access', 'auth-access'):
                    assert 'primeiro quadro:' in (proof / f'{name}-{lang}.log').read_text(), 'member did not display video'
                if name == 'auth-access':
                    assert 'cliente TVHTML5' in (proof / f'{name}-{lang}.log').read_text(), 'member player was anonymous'
                if name == 'auth-audio':
                    assert 'musica: primeiro audio' in (proof / f'{name}-{lang}.log').read_text(), 'member audio did not start'
                subprocess.run(['magick', env['XT_SHOT'], str(proof / f'{name}-{lang}.png')], check=True)
                print(name, lang, flush=True)
finally:
    if server is not None:
        server.terminate()
        try: server.wait(timeout=8)
        except subprocess.TimeoutExpired: server.kill(); server.wait()
    account.shutdown(); account.server_close()
