"""Replay de conta fictícia: WEB_REMIX recusado, música pela TV, público e áudio original.

Nenhum token real: todo HTTPS é substituído no link e pedidos sem gravação falham.
"""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
proof = Path(sys.argv[1]).resolve()
proof.mkdir(parents=True, exist_ok=True)
replay = proof/'replay'
subprocess.run([sys.executable, str(root/'bench/catalog/prepare-replay.py'), str(replay)], check=True)
subprocess.run([sys.executable, str(root/'bench/catalog/build-replay.py'), str(proof)], check=True)
base_manifest = json.loads((replay/'responses.json').read_text())
for i in range(1, 4):
    player_file = replay/f'player-F12audio{i:03}.json'
    player = json.loads(player_file.read_text())
    original = player['streamingData']['adaptiveFormats'][-1]
    original['audioTrack'] = {'id':'en.4', 'displayName':'English (original)', 'audioIsDefault':False}
    dub = copy.deepcopy(original)
    dub['url'] = f'https://fixture.googlevideo.com/dubbed-F12audio{i:03}.m4a'
    dub['audioTrack'] = {'id':'pt.4', 'displayName':'Portuguese (auto-dubbed)', 'audioIsDefault':True}
    player['streamingData']['adaptiveFormats'].append(dub)
    player_file.write_text(json.dumps(player))
    base_manifest['requests'].append({'url':dub['url'], 'file':'audio.m4a', 'range':True, 'authorization':''})
for entry in base_manifest['requests']:
    if entry['url'].startswith('https://fixture.googlevideo.com/audio-'):
        entry['authorization'] = ''
home = json.loads((replay/'tv-music-public.json').read_text().replace('F35audio', 'F12audio'))
shelves = []
def collect(value):
    if isinstance(value, dict):
        for key, child in value.items():
            if key == 'shelfRenderer':
                shelves.append(child)
            else:
                collect(child)
    elif isinstance(value, list):
        for child in value:
            collect(child)
collect(home)
for shelf, title in zip(shelves, ['Para Demo · conta fictícia', 'Ouvidas por Demo · fixture', 'Artistas de Demo · fixture', 'Mixes de Demo · fixture']):
    shelf['headerRenderer'] = {'shelfHeaderRenderer': {'avatarLockup': {'avatarLockupRenderer': {'title': {'runs': [{'text': title}]}}}}}
(replay/'account-home.json').write_text(json.dumps(home, ensure_ascii=False))
liked_fixture = json.loads((replay/'playlist.json').read_text())
liked_fixture['header']['playlistHeaderRenderer']['title']['simpleText'] = 'Curtidas Demo · fixture'
(replay/'account-liked.json').write_text(json.dumps(liked_fixture, ensure_ascii=False))
for name, subset in [('account-recent', shelves[:1]), ('account-library', shelves[-1:])]:
    (replay/(name+'.json')).write_text(json.dumps({'contents': {'sectionListRenderer': {'contents': [{'shelfRenderer': s} for s in subset]}}}, ensure_ascii=False))
# Arte fictícia para cada cartão. A captura pública é usada só como contrato de formato.
for i in range(1, 8):
    base_manifest['requests'].append({'url': f'https://i.ytimg.com/vi/F12audio{i:03}/mqdefault.jpg', 'file': 'art.png'})
base_manifest['requests'].append({'url': 'https://i.ytimg.com/vi/F12audio001/mqdefault.jpg', 'file': 'art.png'})
accounts = proof/'account-fixtures'
shutil.copytree(root/'bench/account/fixtures', accounts, dirs_exist_ok=True)
identity = {'url': '/account/accounts_list?', 'contains': '', 'json': {'contents': [
    {'accountItemRenderer': {'accountName': {'simpleText': 'Demo · conta fictícia'}, 'isSelected': True}}]}}
rules = json.loads((accounts/'responses.json').read_text())
rules.insert(0, identity)
(accounts/'responses.json').write_text(json.dumps(rules))

cases = [
    ('account', 'pt', True, 'music', '', {}, 'tv'),
    ('account-en', 'en', True, 'music', '', {}, 'tv'),
    ('account-es', 'es', True, 'music', '', {}, 'tv'),
    ('account-ar', 'ar', True, 'music', '', {}, 'tv'),
    ('account-large', 'pt', True, 'music', '', {'settings': 'output_height=480\nlarge_text=on\n'}, 'tv'),
    ('guest', 'pt', False, 'music', '', {}, 'public'),
    ('rejected-account', 'pt', True, 'music', '', {}, 'public'),
    ('charts-account', 'pt', True, 'music', 'charts', {}, 'public'),
    ('mixes', 'pt', True, 'music', 'mixes', {}, 'tv'),
    ('recent', 'pt', True, 'music_recent', '', {}, 'tv'),
    ('library', 'pt', True, 'music_library', '', {}, 'tv'),
    ('liked', 'pt', True, 'music_playlist', '', {'XT_NAV_ID': 'VLLM'}, 'tv'),
    ('shelf-audio', 'pt', True, 'music', '', {'XT_KEYS': '..DA', 'XT_KEYS_MS': '650', 'XT_SHOT_MS': '5500'}, 'tv'),
    ('shortcut-back', 'pt', True, 'music', 'library', {'XT_KEYS': 'AB', 'XT_KEYS_MS': '650'}, 'tv'),  # aba Biblioteca: A abre o 1º atalho, B volta
]
wanted = set(sys.argv[2:])
for name, lang, signed, kind, query, config, expected_source in cases:
    if wanted and name not in wanted:
        continue
    manifest = copy.deepcopy(base_manifest)
    requests = manifest['requests']
    tv_context = {'client': {'clientName': 'TVHTML5', 'clientVersion': '7.20261123.19.00', 'visitorData': 'fixture-tv-visitor', 'hl': lang, 'gl': {'pt':'BR','en':'US','es':'MX','ar':'SA'}[lang]}, 'user': {}}
    web_context = {'client': {'clientName':'WEB_REMIX', 'clientVersion':'1.20261122.17.00', 'hl':lang, 'gl':{'pt':'BR','en':'US','es':'MX','ar':'SA'}[lang]}, 'user': {}}
    def post(host, route, fields, file, tv=True, status=200):
        requests.insert(0, {'url': 'https://'+host+'/youtubei/v1/'+route+'?prettyPrint=false&alt=json',
            'fields': {**fields, 'context': tv_context if tv else web_context},
            'file': file, 'status': status, 'authorization': 'console-fixture-token'})
    requests.insert(0, {'url':'https://www.youtube.com/tv', 'file':'tv-context.html'})
    post('music.youtube.com', 'browse', {}, 'music-invalid-bearer.json', tv=False, status=401)
    # Public data cannot accidentally satisfy these routes: the identity and token must match.
    for bid, file in [('FEtopics_music','account-home.json'), ('FEmusic_last_played','account-recent.json'),
                      ('FEplaylist_aggregation','account-library.json'), ('VLLM','account-liked.json')]:
        post('www.youtube.com','browse',{'browseId':bid},file,status=403 if name=='rejected-account' else 200)
    post('www.youtube.com','browse',{'browseId':'FEmusic_explore'},'empty.json',status=400)
    post('www.youtube.com','browse',{'continuation':'FIXTURE_CURSOR'},'empty.json')
    post('www.youtube.com','browse',{'continuation':'CONT_PLAYLIST'},'playlist-more.json')
    post('www.youtube.com','next',{'videoId':'F12audio001'},'radio.json')
    (replay/'responses.json').write_text(json.dumps(manifest))
    assets = proof/('assets-'+name)
    shutil.copytree(replay/'assets', assets, dirs_exist_ok=True)
    for font in (root/'client/assets').glob('noto-sans-arabic-*.ttf'):shutil.copy(font,assets)
    (assets/'settings.txt').write_text('lang='+lang+'\ncols=4\nquality=720\nsound=tv\nserver=off\n'+config.get('settings',''))
    token = assets/'yt-console-account.json'
    token.unlink(missing_ok=True)
    if signed:
        token.write_text(json.dumps({'access_token':'console-fixture-token', 'refresh_token':'fixture-refresh',
            'expires_at':4070908800, 'client':{'client_id':'fixture-client','client_secret':'fixture-secret'}, 'name':'Demo'}))
        token.chmod(0o600)
    home_dir = proof/('home-'+name)
    home_dir.mkdir(exist_ok=True)
    audit = proof/(name+'.audit')
    audit.unlink(missing_ok=True)
    env = {k:v for k,v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(home_dir), XDG_CACHE_HOME=str(home_dir/'.cache'), SDL_VIDEO_DRIVER='offscreen',
        SDL_AUDIO_DRIVER='dummy', LP_NUM_THREADS='2', XT_CAST_DISABLE='1', XT_ASSETS=str(assets),
        XT_SERVER='127.0.0.1:8136', XT_CATALOG_REPLAY=str(replay), XT_ACCOUNT_FIXTURES=str(accounts),
        XT_CATALOG_AUDIT=str(audit), XT_MUSIC='1', XT_NAV=kind, XT_NAV_QUERY=query,
        XT_SHOT=str(proof/(name+'.ppm')), XT_SHOT_MS='2400', XT_SHOT_FRAME='2147483647')
    env.update({k:v for k,v in config.items() if k.startswith('XT_')})
    with (proof/(name+'.log')).open('w') as log:
        subprocess.run([str(proof/'tube360-replay')], env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=40)
    log = (proof/(name+'.log')).read_text()
    assert 'music='+expected_source in log, (name, log)
    assert 'navegação '+kind+':' in log, (name, log)
    if name=='shortcut-back':
        assert 'navegação music_recent:' in log, log  # o atalho "Ouvidas recentemente" abriu de fato
        assert log.rstrip().count('captura navegação: music ') == 1 and 'pilha=1' in log, log  # e o B voltou à aba Biblioteca
    if name=='shelf-audio':
        assert 'musica: primeiro audio' in log and 'primeiro quadro:' not in log, log
        assert 'catalogo console: /api/nav/play' in log and 'navegação radio:' not in log, log  # música avulsa toca direto, sem a página da rádio
        audio_requests = audit.read_text()
        assert 'googlevideo.com/audio-F12audio001.m4a' in audio_requests and 'googlevideo.com/dubbed-' not in audio_requests, audio_requests
    if signed:
        lines = audit.read_text().splitlines()
        assert lines and all(line.endswith('\tauthorized_fixture') for line in lines), lines
        if expected_source=='tv':
            assert any('www.youtube.com/youtubei/' in line for line in lines), lines
    else:
        assert not audit.exists(), 'guest sent account route'
    subprocess.run(['magick', str(proof/(name+'.ppm')), str(proof/(name+'.png'))], check=True)
    print(name, 'music='+expected_source, proof/(name+'.png'), flush=True)
