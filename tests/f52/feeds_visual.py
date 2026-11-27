"""Home e conta reais com transportes fechados; confere geometria dos prints."""
import copy
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
work = Path(sys.argv[1]).resolve()
subprocess.run([sys.executable, str(ROOT/'bench/search/prepare.py'), str(work)], check=True)
replay = work/'replay'
base = json.loads((replay/'responses.json').read_text())
mixed = json.loads((ROOT/'tests/f52/fixtures/feeds.json').read_text())
results = []
wanted = set(sys.argv[2:])


def short(index):
    return {'reelItemRenderer': {'videoId': f'f52vid{index:05d}',
                                 'headline': {'simpleText': f'Short {index}'}}}


def ids(node):
    if isinstance(node, dict):
        for key, value in node.items():
            if key == 'videoId':
                yield value
            yield from ids(value)
    elif isinstance(node, list):
        for value in node:
            yield from ids(value)


def artwork_runs(image, y, sd):
    scale, left, top, width = (.6, 32, 24, 960) if sd else (1, 0, 0, 1280)
    pixels = image.load()

    def art(x, yy):
        r, g, b = pixels[x, yy][:3]
        return 28 <= r <= 175 and 35 <= g <= 70 and 65 <= b <= 96

    runs, first = [], None
    right = int(left+(width-64)*scale)
    for x in range(int(left+120*scale), right):
        inside = art(x, int(top+y*scale))
        if inside and first is None:
            first = x
        if first is not None and not inside:
            if x-first >= 20:
                runs.append(round((x-first)/scale, 1))
            first = None
    if first is not None and right-first >= 20:
        runs.append(round((right-first)/scale, 1))
    if sd:
        assert not any(art(x, yy) for yy in range(456, 480) for x in range(96, 609)), 'artwork outside CRT safe area'
    return runs


def run(name, signed=False, hide=False, keys='....D', expected='portrait', sd=False,
        nav=False, only=False, continuation=False, toggle=False, category=0, sample=None, focus=None,
        nav_kind='subscriptions', nav_focus=4):
    if wanted and name not in wanted:
        return
    assets = work/name
    assets.mkdir(parents=True, exist_ok=True)
    for font in (ROOT/'client/assets').glob('*.ttf'):
        shutil.copyfile(font, assets/font.name)
    settings = assets/'settings.txt'
    settings.write_text(f'lang=en\nserver=off\nsound=tv\ncols=4\nshorts={"hide" if hide else "row"}\noutput_height={480 if sd else 720}\n')
    payload = copy.deepcopy(mixed)
    if only:
        payload = {'contents': [short(i) for i in range(101, 114)]}
    if continuation:
        payload = {'contents': [short(i) for i in range(10100, 10124)]+
                   [{'continuationCommand': {'token': 'F52_NEXT'}}]}
    tail = {'contents': [mixed['contents'][0], short(10200), short(10100)]}
    manifest = copy.deepcopy(base)
    (replay/'public-feed.json').write_text(json.dumps(payload))
    manifest['requests'].insert(0, dict(url='https://www.youtube.com/youtubei/v1/browse?prettyPrint=false',
                                       file='public-feed.json', fields={'browseId': 'FEwhat_to_watch'}))
    for vid in set(ids(payload)) | set(ids(tail)):
        manifest['requests'].append(dict(url=f'https://i.ytimg.com/vi/{vid}/mqdefault.jpg', file='art.png'))
    (replay/'responses.json').write_text(json.dumps(manifest))
    accounts = assets/'account-fixtures'
    accounts.mkdir(exist_ok=True)
    channel = {'contents': [{'channelRenderer': {'channelId': 'UC0000000000000000000001',
                                                 'title': {'simpleText': 'Fixture channel'}}}]}
    rules = [dict(url='/account/accounts_list?', contains='', json={'contents': [
        {'accountItemRenderer': {'accountName': {'simpleText': 'F52 fixture'}, 'isSelected': True}}]})]
    for bid in ('FEwhat_to_watch', 'FEsubscriptions', 'FEhistory', 'VLWL', 'FEplaylist_aggregation'):
        rules.append(dict(url='/youtubei/v1/browse?', contains=bid, json=payload, authorization='F52_ACCESS'))
    rules.extend([dict(url='/youtubei/v1/browse?', contains='FEchannels', json=channel, authorization='F52_ACCESS'),
                  dict(url='/youtubei/v1/browse?', contains='F52_NEXT', json=tail, authorization='F52_ACCESS')])
    (accounts/'responses.json').write_text(json.dumps(rules))
    token = assets/'yt-console-account.json'
    if signed:
        token.write_text(json.dumps(dict(access_token='F52_ACCESS', refresh_token='F52_REFRESH',
                                        expires_at=int(time.time())+3600,
                                        client=dict(client_id='fixture-client', client_secret='fixture-secret'),
                                        name='F52 fixture')))
    else:
        token.unlink(missing_ok=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith('XT_')}
    env.update(HOME=str(assets/'home'), XDG_CONFIG_HOME=str(assets/'config'), XDG_CACHE_HOME=str(assets/'cache'),
               SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', XT_CAST_DISABLE='1', XT_ASSETS=str(assets),
               XT_SEARCH_REPLAY=str(replay), XT_ACCOUNT_FIXTURES=str(accounts), XT_ACCOUNT_AUDIT=str(assets/'requests.log'),
               XT_KEYS=keys, XT_KEYS_MS='180', XT_SHOT=str(assets/'screen.ppm'), XT_SHOT_MS=str(max(2600, len(keys)*180+900)))
    if nav:
        env['XT_NAV'] = nav_kind
    with (assets/'app.log').open('w') as log:
        subprocess.run([str(work/'tube360-search-replay')], env=env, stdout=log,
                       stderr=subprocess.STDOUT, timeout=40, check=True)
    text = (assets/'app.log').read_text()
    assert f'captura conta: conectada={int(signed)}' in text, text[-3000:]
    assert 'tela 4' in text if nav else 'tela 0' in text, text[-3000:]
    assert 'player: pedido' not in text, text[-3000:]
    if expected == 'portrait':
        if nav:
            assert f'captura navegação: {nav_kind} sel={nav_focus}' in text, text[-3000:]
        elif keys.endswith('D'):
            target = 3 if sd else 5
            assert f'home: categoria {category}, foco {target}/' in text, text[-3000:]
    if toggle:
        assert 'shorts=hide\n' in settings.read_text()
    if focus is not None:
        positions = re.findall(r'home: categoria (\d+), foco (\d+)/', text)
        assert positions and tuple(map(int, positions[-1])) == (category, focus), (name, positions)
    with Image.open(assets/'screen.ppm') as image:
        image = image.convert('RGB')
        runs = artwork_runs(image, sample if sample is not None else 450 if nav else 240, sd)
        if expected == 'portrait':
            assert len(runs) >= 3 and all(90 <= width <= 125 for width in runs), (name, runs)
        elif expected == 'landscape':
            assert runs and all(width > 180 for width in runs), (name, runs)
            assert len(runs) == (1 if continuation else 4), (name, runs)
        else:
            assert not runs, (name, runs)
        if continuation:
            assert len(runs) == 1, (name, runs)
        image.save(assets/'screen.png')
    (assets/'screen.ppm').unlink()
    results.append(dict(name=name, image=str(assets/'screen.png'), widths=runs))
    print(name, 'OK; artwork widths:', runs, flush=True)


run('public-row')
run('public-initial', keys='....', sample=470)
run('public-right', keys='....D'+'R'*11, focus=20)
run('public-return', keys='....DRRRDU', focus=12)
run('public-hide', hide=True, keys='', expected='landscape')
run('public-crt-row', keys='....DD', sd=True)
run('account-row', signed=True)
run('account-initial', signed=True, keys='....', sample=470)
run('account-right', signed=True, keys='....D'+'R'*11, focus=20)
run('account-hide', signed=True, hide=True, keys='', expected='landscape')
run('account-crt-row', signed=True, keys='....DD', sd=True)
run('account-history-row', signed=True, keys='....URR..DD', category=2)
run('account-later-row', signed=True, keys='....URRR..DD', category=3)
run('subscriptions-row', signed=True, nav=True)
run('subscriptions-hide', signed=True, hide=True, nav=True, keys='', expected='landscape', sample=390)
run('account-library-row', signed=True, nav=True, nav_kind='library', nav_focus=9, keys='....X...DDD', sample=240)
run('hidden-first-page', signed=True, hide=True, keys='', expected='landscape', continuation=True)
run('all-hidden', signed=True, hide=True, only=True, keys='....A', expected='empty')
run('toggle-loaded-feed', signed=True, keys='....L'+'D'*6+'A'+'D'*7+'RBR', expected='landscape', toggle=True)
(work/'manifest.json').write_text(json.dumps(results, indent=2))
