"""App de PC, server=off, conta falsa; capturas e auditoria não usam a rede externa."""
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
manifest = json.loads((replay/'responses.json').read_text())
captures = json.loads((replay/'oauth-requests.json').read_text())['requests']
manifest['requests'].insert(0, {'url': 'https://www.youtube.com/sw.js_data', 'file': 'tv-session.txt'})
files = ['account-home.json', 'empty.json', 'music-explore.json', 'music-charts.json',
         'music-search.json', 'radio.json', 'lyrics-next.json', 'lyrics.json']
for request, filename in zip(captures, files):
    manifest['requests'].insert(0, {'url': request['url'], 'fields': {k: v for k, v in request['body'].items()
        if k not in ('context', 'isAudioOnly')}, 'file': filename, 'authorization': 'console-fixture-token'})
# Marcadores de conta sintéticos sobre o formato de renderer gravado, nunca dados de uma conta real.
art = 'https://lh3.googleusercontent.com/fixture-account-art'
cards = [{'musicTwoRowItemRenderer': {'title': {'runs': [{'text': 'Faixa da conta · teste '+str(i)}]},
    'subtitle': {'runs': [{'text': 'Conta Premium fictícia'}]},
    'navigationEndpoint': {'watchEndpoint': {'videoId': 'F12audio00'+str(i)}},
    'thumbnailRenderer': {'musicThumbnailRenderer': {'thumbnail': {'thumbnails': [{'url': art, 'width': 320}]}}}}}
    for i in range(1, 4)]
(replay/'account-home.json').write_text(json.dumps({'contents': {'sectionListRenderer': {'contents': [
    {'musicCarouselShelfRenderer': {'header': {'musicCarouselShelfBasicHeaderRenderer':
        {'title': {'runs': [{'text': 'Da sua conta · fixture'}]}}}, 'contents': cards}}]}}}, ensure_ascii=False))
manifest['requests'].append({'url': art, 'file': 'art.png'})
(replay/'responses.json').write_text(json.dumps(manifest))
account = proof/'account'
shutil.copytree(root/'bench/account/fixtures', account, dirs_exist_ok=True)
identity = {'url': '/account/accounts_list?', 'contains': '', 'json': {'contents': [
    {'accountItemRenderer': {'accountName': {'simpleText': 'Conta de teste'}, 'isSelected': True}}]}}
rules = json.loads((account/'responses.json').read_text())
rules.insert(0, identity)
(account/'responses.json').write_text(json.dumps(rules))
for name, signed in [('account', True), ('guest', False), ('charts-account', True), ('rejected-account', True)]:
    if len(sys.argv) > 2 and name not in sys.argv[2:]:
        continue
    case_manifest = json.loads(json.dumps(manifest))
    if name == 'rejected-account':
        for entry in case_manifest['requests']:
            if entry.get('authorization') and entry.get('fields', {}).get('browseId') == 'FEmusic_home':
                entry['status'] = 400
    (replay/'responses.json').write_text(json.dumps(case_manifest))
    assets = proof/('assets-'+name)
    shutil.copytree(replay/'assets', assets, dirs_exist_ok=True)
    if signed:
        (assets/'yt-console-account.json').write_text(json.dumps({'access_token': 'console-fixture-token',
            'refresh_token': 'fixture-refresh', 'expires_at': 4070908800,
            'client': {'client_id': 'fixture-client', 'client_secret': 'fixture-secret'}, 'name': 'Conta de teste'}))
        (assets/'yt-console-account.json').chmod(0o600)
    home = proof/('home-'+name)
    home.mkdir(exist_ok=True)
    audit = proof/(name+'.audit')
    audit.unlink(missing_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
    env.update({'HOME': str(home), 'SDL_VIDEO_DRIVER': 'offscreen', 'SDL_AUDIO_DRIVER': 'dummy', 'LP_NUM_THREADS': '2',
        'XT_CAST_DISABLE': '1', 'XT_ASSETS': str(assets), 'XT_SERVER': '127.0.0.1:8136',
        'XT_CATALOG_REPLAY': str(replay), 'XT_ACCOUNT_FIXTURES': str(account), 'XT_CATALOG_AUDIT': str(audit),
        'XT_MUSIC': '1', 'XT_NAV': 'music', 'XT_NAV_QUERY': 'charts' if name == 'charts-account' else '',
        'XT_SHOT': str(proof/(name+'.ppm')), 'XT_SHOT_MS': '2400', 'XT_SHOT_FRAME': '2147483647'})
    with (proof/(name+'.log')).open('w') as log:
        subprocess.run([str(proof/'tube360-replay')], env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=35)
    log = (proof/(name+'.log')).read_text()
    assert 'catalogo console: /api/nav,' in log, log
    assert ('conta recusada: browse HTTP 400' in log) if name == 'rejected-account' else ('conta recusada:' not in log), log
    if signed:
        entries = audit.read_text().splitlines()
        assert entries and all(line.endswith('\tauthorized_fixture') for line in entries), entries
        assert len(entries) == (2 if name == 'charts-account' else 1), entries
    else:
        assert not audit.exists(), 'guest sent OAuth route'
    subprocess.run(['magick', str(proof/(name+'.ppm')), str(proof/(name+'.png'))], check=True)
    print(name, 'server=off', proof/(name+'.png'), flush=True)
