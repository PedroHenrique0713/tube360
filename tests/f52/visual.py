"""App e controle reais; o transporte de prova recusa pedidos HTTPS não previstos."""
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
work = Path(sys.argv[1]).resolve()
phase = sys.argv[2]
replay = work / 'replay'
routes = json.loads((replay / 'responses.json').read_text())
base_routes = list(routes['requests'])
manifest = []


def video(index, short=False, modern=False):
    vid = f'f52vid{index:05d}'
    url = f'https://i.ytimg.com/vi/{vid}/mqdefault.jpg'
    routes['requests'].append(dict(url=url, file='art.png'))
    if short and modern:
        return {'shortsLockupViewModel': {
            'onTap': {'innertubeCommand': {'reelWatchEndpoint': {'videoId': vid}}},
            'overlayMetadata': {'primaryText': {'content': f'Short {index}'}},
            'thumbnail': {'sources': [{'url': url, 'width': 320}]}}}
    return {('reelItemRenderer' if short else 'videoRenderer'): {
        'videoId': vid, 'title': {'simpleText': f'{"Short" if short else "Video"} {index}'},
        'shortBylineText': {'simpleText': 'F52 fixture'},
        'thumbnail': {'thumbnails': [{'url': url, 'width': 320}]}}}


def run(name, keys='S', shorts='row', mixed=False, screen='search', lang='en', height=720):
    routes['requests'] = list(base_routes)
    contents = [video(i) for i in range(1, 21)]
    if mixed:
        contents = contents[:4] + [video(101, True)] + contents[4:8] + [
            video(i, True, i % 2 == 0) for i in range(102, 114)] + contents[8:]
    (replay / 'search-public.json').write_text(json.dumps({'contents': contents}))
    (replay / 'responses.json').write_text(json.dumps(routes))
    assets = work / name
    assets.mkdir(exist_ok=True)
    for font in (ROOT / 'client/assets').glob('*.ttf'):
        shutil.copyfile(font, assets / font.name)
    settings = assets / 'settings.txt'
    settings.write_text(f'lang={lang}\ncols=4\nsound=tv\nserver=off\nshorts={shorts}\nerror_reports=on\noutput_height={height}\n')
    env = dict(os.environ, HOME=str(assets / 'home'), XDG_CONFIG_HOME=str(assets / 'config'),
               XDG_CACHE_HOME=str(assets / 'cache'), SDL_VIDEO_DRIVER='offscreen',
               SDL_AUDIO_DRIVER='dummy', XT_CAST_DISABLE='1', XT_ASSETS=str(assets),
               XT_SEARCH_REPLAY=str(replay), XT_ACCOUNT_FIXTURES=str(work / 'accounts'),
               XT_SCREEN=screen, XT_SEARCH_TEXT='layout', XT_KEYS=keys, XT_KEYS_MS='130',
               XT_SHOT=str(assets / 'screen.ppm'), XT_SHOT_MS=str(max(2000, len(keys)*130+650)))
    with (assets / 'app.log').open('w') as log:
        subprocess.run([str(work / 'tube360-search-replay')], env=env, stdout=log,
                       stderr=subprocess.STDOUT, timeout=35, check=True)
    text = (assets / 'app.log').read_text()
    assert 'captura:' in text, text[-2500:]
    if screen == 'search' and not name.startswith('sleep-'):
        assert 'captura navegação: search' in text, text[-2500:]
    if phase == 'after' and name in ('regular-down', 'shorts', 'shorts-right', 'shorts-crt'):
        selected = int(re.search(r'captura navegação: search sel=(\d+)', text)[1])
        assert selected == (19 if name == 'shorts-right' else 4), (name, selected)
    if phase == 'after' and name == 'sleep-apply':
        assert 'timer: on 900 s minutos (console)' in text, text[-3000:]
    if phase == 'after' and name == 'settings-shorts':
        assert 'shorts=hide\n' in settings.read_text()
    if phase == 'after' and name == 'settings-reports':
        assert 'error_reports=off\n' in settings.read_text()
    subprocess.run(['magick', str(assets / 'screen.ppm'), str(assets / 'screen.png')], check=True)
    (assets / 'screen.ppm').unlink()
    manifest.append(dict(name=name, image=str(assets / 'screen.png')))
    print(phase, name, 'OK', flush=True)


run('regular')
run('regular-down', keys='S....D')
run('shorts', keys='S....D', mixed=True)
run('shorts-right', keys='S....D'+'R'*11, mixed=True)
run('shorts-hide', mixed=True, shorts='hide')
run('settings-shorts', screen='settings', keys='D'*7+'R', lang='pt')
run('settings-reports', screen='settings', keys='D'*8+'R', lang='pt')
run('shorts-crt', keys='S....DD', mixed=True, height=480)
run('settings-ar', screen='settings', keys='D'*8, lang='ar', height=480)
run('sleep-panel', keys='S....A....DA')
run('sleep-apply', keys='S....A....DADRA')
(work / 'manifest.json').write_text(json.dumps(manifest, indent=2))
