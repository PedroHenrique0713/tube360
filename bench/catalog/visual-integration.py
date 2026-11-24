"""Prova visual da integração F9/F11/F12/F15, sem servidor, conta real ou Xbox.

Uso: python3 bench/catalog/visual-integration.py <pasta> [nomes de casos]
As três imagens em <pasta>/art são cópias públicas opcionais, nunca da conta.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
work = Path(sys.argv[1]).resolve()
work.mkdir(parents=True, exist_ok=True)
replay, accounts, media = work/'replay', work/'accounts', work/'media'
for args in [
    ['bench/catalog/prepare-replay.py', str(replay)],
    ['bench/catalog/build-replay.py', str(work)],
    ['bench/quality/prepare.py', str(media), '--mode', 'both', '--audio-tracks', '--seconds', '12'],
]:
    subprocess.run([sys.executable, *args], cwd=root, check=True)
manifest = json.loads((replay/'responses.json').read_text())
rules = json.loads((root/'bench/account/fixtures/responses.json').read_text())
accounts.mkdir(exist_ok=True)
shutil.copyfile(root/'bench/account/fixtures/thumb.png', accounts/'thumb.png')
for name in ('avatar.png', 'banner.jpg', 'thumb.jpg'):
    source = work/'art'/name
    if source.exists():
        shutil.copyfile(source, replay/name)
        shutil.copyfile(source, accounts/name)
for rule in manifest['requests']:
    if rule['file'] == 'banner.png' and (replay/'banner.jpg').exists():
        rule['file'] = 'banner.jpg'
    elif 'googleusercontent.com/' in rule['url'] and (replay/'avatar.png').exists():
        rule['file'] = 'avatar.png'
    elif 'ytimg.com/vi/' in rule['url'] and (replay/'thumb.jpg').exists():
        rule['file'] = 'thumb.jpg'

avatar = 'https://yt3.ggpht.com/fixture-avatar'
rules.insert(0, {'url': '/account/accounts_list?', 'json': {'contents': [{'accountItemRenderer': {
    'accountName': {'simpleText': 'Conta fictícia'}, 'isSelected': True,
    'accountPhoto': {'thumbnails': [{'url': avatar, 'width': 160, 'height': 160}]}}}]}})
for rule in rules:
    if rule.get('contains') == '"browseId":"FEplaylist_aggregation"':
        mix = rule['json']['contents'][-1]['tileRenderer']
        mix['header'] = {'tileHeaderRenderer': {'thumbnail': {'thumbnails': [{'url': 'https://i.ytimg.com/vi/video000001/mqdefault.jpg', 'width': 320}]}}}
    if rule.get('url') == avatar:
        rule['file'] = 'avatar.png' if (accounts/'avatar.png').exists() else 'thumb.png'
(accounts/'responses.json').write_text(json.dumps(rules))
manifest['requests'].append({'url': avatar, 'file': 'avatar.png' if (replay/'avatar.png').exists() else 'art.png'})
for i in range(1, 9):
    manifest['requests'].append({'url': f'https://i.ytimg.com/vi/video{i:06}/mqdefault.jpg',
        'file': 'thumb.jpg' if (replay/'thumb.jpg').exists() else 'art.png'})
for path in media.glob('*'):
    if path.suffix in ('.mp4', '.m4a'):
        shutil.copyfile(path, replay/path.name)
        manifest['requests'].append({'url': 'https://media.example/'+path.name, 'file': path.name, 'range': True})
player = json.loads((media/'player.json').read_text())
player['videoDetails'].update(title='Vídeo de bancada · Original e dublagem', author='Canal de bancada', channelId='UC_x5XG1OV2P6uZZ5FSM9Ttw')
player['microformat'] = {'playerMicroformatRenderer': {'publishDate': '2026-11-22'}}
(replay/'player-visual.json').write_text(json.dumps(player))
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(media/'720.mp4'), '-i', str(media/'audio.m4a'),
    '-c', 'copy', '-f', 'hls', '-hls_time', '2', '-hls_list_size', '0',
    '-hls_segment_filename', str(replay/'live%03d.ts'), str(replay/'live.m3u8')], check=True)
(replay/'master.m3u8').write_text('#EXTM3U\n#EXT-X-STREAM-INF:CODECS="avc1.640028,mp4a.40.2",RESOLUTION=1280x720,FRAME-RATE=30\nlive.m3u8\n')
for path in [*replay.glob('*.m3u8'), *replay.glob('live*.ts')]:
    manifest['requests'].append({'url': 'https://media.example/'+path.name, 'file': path.name})
manifest['requests'].insert(0, {'url': 'https://www.youtube.com/youtubei/v1/player?prettyPrint=false',
    'fields': {'videoId': '4p_9OPKgcDE'}, 'file': 'player-visual.json'})
manifest['requests'].append({'url': 'https://www.youtube.com/watch?v=4p_9OPKgcDE&bpctr=9999999999&has_verified=1', 'file': 'watch.html'})
(replay/'responses.json').write_text(json.dumps(manifest))

cases = [
    ('channel', {'XT_NAV': 'channel', 'XT_NAV_ID': 'UC_x5XG1OV2P6uZZ5FSM9Ttw'}),
    ('subscriptions', {'XT_NAV': 'subscriptions'}),
    ('library', {'XT_NAV': 'library'}),
    ('music', {'XT_NAV': 'music', 'XT_MUSIC': '1'}),
    ('player', {'XT_PLAY': '4p_9OPKgcDE', 'XT_KEYS': 'ULLL'}),
    ('quality', {'XT_PLAY': '4p_9OPKgcDE', 'XT_KEYS': 'ULLLA'}),
    ('audio', {'XT_PLAY': '4p_9OPKgcDE', 'XT_KEYS': 'UL'}),
    ('audio-panel', {'XT_PLAY': '4p_9OPKgcDE', 'XT_KEYS': 'ULA'}),
    ('live', {'XT_PLAY': '4p_9OPKgcDE'}),
    ('live-paused', {'XT_PLAY': '4p_9OPKgcDE', 'XT_KEYS': 'AURRR'}),
    ('live-return', {'XT_PLAY': '4p_9OPKgcDE', 'XT_KEYS': 'AURRRA', 'XT_SHOT_MS': '5200'}),
]
for label, config in list(cases[:4]):
    prefix = '' if label == 'subscriptions' else 'D'
    cases.extend([(label+'-rail', {**config, 'XT_KEYS': prefix+'L'}), (label+'-return', {**config, 'XT_KEYS': prefix+'LR'})])
wanted = set(sys.argv[2:])
previous = json.loads((work/'visual-results.json').read_text()) if wanted and (work/'visual-results.json').exists() else []
results = {item['name']: item for item in previous}
for height in (720, 1080):
    for lang in ('pt', 'en'):
        for label, config in cases:
            name = f'{label}-{height}-{lang}'
            if wanted and label not in wanted and name not in wanted:
                continue
            data = json.loads(json.dumps(player))
            if label.startswith('live'):
                data['videoDetails']['isLive'] = True
                data['streamingData']['hlsManifestUrl'] = 'https://media.example/master.m3u8'
            (replay/'player-visual.json').write_text(json.dumps(data))
            assets = work/('assets-'+name)
            shutil.copytree(replay/'assets', assets, dirs_exist_ok=True)
            (assets/'settings.txt').write_text(f'lang={lang}\ncols=4\nquality=720\nsound=tv\nserver=off\noutput_height={height}\nvideo_mode=1080p30\n')
            (assets/'yt-console-account.json').write_text(json.dumps({'access_token': 'console-fixture-token',
                'refresh_token': 'fixture-refresh', 'expires_at': 4070908800,
                'client': {'client_id': 'fixture-client', 'client_secret': 'fixture-secret'}, 'name': 'Conta fictícia'}))
            (assets/'yt-console-account.json').chmod(0o600)
            home = work/('home-'+name)
            home.mkdir(exist_ok=True)
            env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
            env.update(HOME=str(home), XDG_CACHE_HOME=str(home/'.cache'), SDL_VIDEO_DRIVER='offscreen',
                SDL_AUDIO_DRIVER='dummy', LP_NUM_THREADS='2', XT_CAST_DISABLE='1', XT_ASSETS=str(assets),
                XT_SERVER='127.0.0.1:8118', XT_ACCOUNT_FIXTURES=str(accounts), XT_CATALOG_REPLAY=str(replay),
                XT_SHOT=str(work/(name+'.ppm')), XT_SHOT_FRAME='2147483647', XT_SHOT_MS='3200', XT_KEYS_MS='450')
            env.update(config)
            with (work/(name+'.log')).open('w') as log:
                subprocess.run([str(work/'tube360-replay')], env=env, stdout=log, stderr=log, check=True, timeout=35)
            log = (work/(name+'.log')).read_text()
            if 'XT_PLAY' in config:
                assert 'primeiro quadro:' in log, log
                if label.startswith('live'):
                    assert 'live 1' in log, log
                if label == 'live-return':
                    assert 'live: retorno manual à borda' in log and log.count('primeiro quadro:') >= 2, log
            else:
                assert f'navegação {config["XT_NAV"]}:' in log, log
                if label.endswith('-rail'):
                    assert 'menu=1' in log, log
                if label.endswith('-return'):
                    selection = 'prateleira=1' if config['XT_NAV'] == 'music' else 'sel=0' if config['XT_NAV'] == 'subscriptions' else 'sel=4'
                    assert 'menu=0' in log and selection in log, log
            assert 'captura conta: conectada=1 foto=1' in log, name+' has no signed-in avatar'
            match = re.search(r'miniaturas: (\d+) bytes GPU', log)
            assert match and int(match[1]) > 0, name+' has no artwork'
            assert int(match[1]) <= 64*320*180*4, name+' exceeded artwork budget'
            png = work/(name+'.png')
            subprocess.run(['magick', str(work/(name+'.ppm')), str(png)], check=True)
            size = subprocess.check_output(['identify', '-format', '%wx%h', str(png)], text=True)
            assert size == ('1920x1080' if height == 1080 else '1280x720'), size
            results[name] = {'name': name, 'size': size, 'gpu_art_bytes': int(match[1])}
            (work/'visual-results.json').write_text(json.dumps(list(results.values()), indent=2))
            print('PASS', name, size, flush=True)
