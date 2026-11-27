"""Ataques de controle no app real, com HTTPS fechado e estado isolado por caso."""
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2] / 'tests'))
from settings_nav import seq
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
parser.add_argument('cases', nargs='*')
args = parser.parse_args()
work = args.output.resolve()
work.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(ROOT/'bench/search/prepare.py'), str(work)], check=True)
base = json.loads((work/'replay/responses.json').read_text())
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
                'testsrc2=size=640x360:rate=24:duration=8', '-an', '-c:v', 'libx264',
                '-threads', '2', '-preset', 'ultrafast', '-movflags', '+faststart',
                str(work/'replay/short.mp4')], check=True)
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
                'sine=frequency=440:duration=8', '-ac', '2', '-c:a', 'aac',
                str(work/'replay/short.m4a')], check=True)
player_reply = {'playabilityStatus': {'status': 'OK'},
    'videoDetails': {'videoId': 'f52vid00018', 'title': 'F52 synthetic Short', 'lengthSeconds': '8'},
    'streamingData': {'adaptiveFormats': [
        {'itag': 134, 'width': 640, 'height': 360, 'fps': 24,
         'mimeType': 'video/mp4; codecs="avc1.640028"', 'url': 'https://fixture.googlevideo.com/short.mp4'},
        {'itag': 140, 'mimeType': 'audio/mp4; codecs="mp4a.40.2"', 'bitrate': 128000,
         'url': 'https://fixture.googlevideo.com/short.m4a'}]}}
(work/'replay/short-player.json').write_text(json.dumps(player_reply))
(work/'replay/watch.html').write_text('<script>{"VISITOR_DATA":"fixture-visitor","jsUrl":"/s/player/fixture/base.js"}</script>')
(work/'replay/base.js').write_text('var player={signatureTimestamp:20775};')
base['requests'][:0] = [
    dict(url='https://www.youtube.com/watch?v=f52vid00018&bpctr=9999999999&has_verified=1', file='watch.html'),
    dict(url='https://www.youtube.com/s/player/fixture/base.js', file='base.js'),
    dict(url='https://www.youtube.com/youtubei/v1/player?prettyPrint=false',
         file='short-player.json', fields={'videoId': 'f52vid00018'}),
    dict(url='https://fixture.googlevideo.com/short.mp4', file='short.mp4', range=True),
    dict(url='https://fixture.googlevideo.com/short.m4a', file='short.m4a', range=True)]
results = []


def card(index, short=False, channel=False, huge=False, arabic=False):
    title = ('عنوان طويل جدا لاختبار التفاف النص والمساحة الآمنة ' if arabic else
             'Extremely long title with many words to exercise wrapping and clipping ') * (12 if huge else 1)
    if channel:
        return {'channelRenderer': {'channelId': 'UC'+str(index).zfill(22),
                                    'title': {'simpleText': title},
                                    'thumbnail': {'thumbnails': [{'url': 'https://i.ytimg.com/channel.png'}]}}}
    return {('reelItemRenderer' if short else 'videoRenderer'): {
        'videoId': f'f52vid{index:05d}', 'title': {'simpleText': title},
        'headline': {'simpleText': title}, 'shortBylineText': {'simpleText': title},
        'thumbnail': {'thumbnails': [{'url': f'https://i.ytimg.com/vi/f52vid{index:05d}/mqdefault.jpg'}]}}}


def run(name, payload, keys='', selected=None, screen='search', lang='en', cols=4,
        large=False, height=720, hide=False, next_page=None, settings=None,
        persisted=None, restart=False, player=None, filter_open=False, rail=False, searches=None):
    if args.cases and name not in args.cases:
        return
    assets = work/name
    if assets.exists() and not restart:
        previous = work/'previous'
        previous.mkdir(exist_ok=True)
        index = 1
        while (previous/f'{name}-{index}').exists():
            index += 1
        assets.rename(previous/f'{name}-{index}')
    assets.mkdir(exist_ok=True)
    replay = assets/'replay'
    shutil.copytree(work/'replay', replay, dirs_exist_ok=True)
    manifest = json.loads(json.dumps(base))
    (replay/'adversarial.json').write_text(json.dumps({'contents': payload}))
    routes = [dict(url='https://www.youtube.com/youtubei/v1/search?prettyPrint=false', file='adversarial.json'),
              dict(url='https://www.youtube.com/youtubei/v1/browse?prettyPrint=false',
                   file='adversarial.json', fields={'browseId': 'FEwhat_to_watch'})]
    if next_page is not None:
        (replay/'next.json').write_text(json.dumps({'contents': next_page}))
        routes.insert(0, dict(url='https://www.youtube.com/youtubei/v1/search?prettyPrint=false',
                              file='next.json', fields={'continuation': 'F52_ADVERSARIAL_NEXT'}))
    for i in range(100):
        routes.append(dict(url=f'https://i.ytimg.com/vi/f52vid{i:05d}/mqdefault.jpg', file='art.png'))
    routes.append(dict(url='https://i.ytimg.com/channel.png', file='art.png'))
    manifest['requests'] = routes+manifest['requests']
    (replay/'responses.json').write_text(json.dumps(manifest))
    for font in (ROOT/'client/assets').glob('*.ttf'):
        shutil.copyfile(font, assets/font.name)
    settings_file = assets/'settings.txt'
    if not restart:
        settings_file.write_text(settings if settings is not None else
            f'lang={lang}\ncols={cols}\nsound=tv\nserver=off\nshorts={"hide" if hide else "row"}\n'
            f'error_reports=on\noutput_height={height}\nlarge_text={"on" if large else "off"}\n')
    env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(assets/'home'), XDG_CONFIG_HOME=str(assets/'config'),
               XDG_CACHE_HOME=str(assets/'cache'), SDL_VIDEO_DRIVER='offscreen',
               SDL_AUDIO_DRIVER='dummy', XT_CAST_DISABLE='1', XT_ASSETS=str(assets),
               XT_SEARCH_REPLAY=str(replay), XT_ACCOUNT_FIXTURES=str(work/'accounts'),
               XT_SEARCH_AUDIT=str(assets/'requests.log'), XT_KEYS=keys, XT_KEYS_MS='180',
               XT_SHOT=str(assets/'screen.ppm'), XT_SHOT_MS=str(max(2600, len(keys)*180+1200)))
    if screen != 'home':
        env.update(XT_SCREEN=screen, XT_SEARCH_TEXT='adversarial')
    log_file = assets/('restart.log' if restart else 'app.log')
    try:
        with log_file.open('w') as log:
            subprocess.run([str(work/'tube360-search-replay')], env=env, stdout=log,
                           stderr=subprocess.STDOUT, timeout=50, check=True)
        text = log_file.read_text()
        assert 'captura:' in text and 'desligado: só console' in text, text[-2200:]
        assert 'atlas full' not in text, text[-2200:]
        if screen == 'search':
            match = re.search(r'captura navegação: search sel=(\d+).*menu=(\d+)', text)
            assert match and int(match[2]) == int(rail), text[-2200:]
            if selected is not None:
                assert int(match[1]) == selected, f'expected sel={selected}, got {match[1]}'
        if screen == 'home' and selected is not None:
            positions = re.findall(r'home: categoria 0, foco (\d+)/', text)
            if selected:
                assert positions and int(positions[-1])-1 == selected, (selected, positions)
            assert 'tela 0, vídeo' in text, text[-2200:]
        if player is not None:
            assert f'player: pedido f52vid{player:05d}' in text, text[-2200:]
            assert 'fonte=360p' in text and 'primeiro quadro:' in text, text[-2200:]
            position = re.search(r'captura:.*posição ([\d.]+)', text)
            assert position and float(position[1]) > 0, 'Short must decode before returning'
        if filter_open:
            assert 'captura busca: painel=1' in text, text[-2200:]
            assert 'tipo=0' in text, 'the Filters pill opens the panel on the current type (All), not on a chip it walked over'
        if searches is not None:
            made = (assets/'requests.log').read_text().count('/youtubei/v1/search')
            assert made == searches, f'expected {searches} search request(s), got {made}'
        if next_page is not None:
            assert '"continuation":"F52_ADVERSARIAL_NEXT"' in (assets/'requests.log').read_text()
        if persisted:
            saved = settings_file.read_text()
            for value in persisted:
                assert value+'\n' in saved, (value, saved)
        with Image.open(assets/'screen.ppm') as img:
            if screen in ('home', 'search') and not filter_open:
                pixels = img.convert('RGB').tobytes()
                artwork = sum(28 <= r <= 175 and 35 <= g <= 70 and 65 <= b <= 96
                              for r, g, b in zip(pixels[::3], pixels[1::3], pixels[2::3]))
                assert artwork > 1000, 'Expected visible fixture artwork, found empty grid'
            img.save(assets/('restart.png' if restart else 'screen.png'))
        result = dict(name=name, restart=restart, status='PASS', selected=selected)
        print('PASS', name, 'restart' if restart else '', f'sel={selected}', flush=True)
    except (AssertionError, subprocess.SubprocessError) as e:
        result = dict(name=name, restart=restart, status='FAIL', error=str(e))
        if (assets/'screen.ppm').exists():
            with Image.open(assets/'screen.ppm') as img:
                img.save(assets/'screen.png')
        print('FAIL', name, str(e), flush=True)
    results.append(result)
    (work/'manifest.json').write_text(json.dumps(results, indent=2))


normal = [card(i) for i in range(16)]
shorts = [card(i, True) for i in range(16, 29)]
mixed = normal[:4]+shorts+normal[4:]
for screen in ('search', 'home'):
    prefix = 'S.....' if screen == 'search' else '......'
    run(screen+'-start-cross', shorts+normal, prefix+'RRDRRRU', 2, screen)
    run(screen+'-middle-column', mixed, prefix+'RRDRRRDUU', 2, screen)
    run(screen+'-middle-down', mixed, prefix+'RRDRRRD', 19, screen)
    run(screen+'-end-boundary', normal+shorts, prefix+'DDDD'+'R'*20, 28, screen)
    run(screen+'-end-return', normal+shorts, prefix+'DDDD'+'R'*20+'UD', 28, screen)
    run(screen+'-only-shorts', shorts, prefix+'R'*20+'DU', 12, screen)
    run(screen+'-only-one', [shorts[0]], prefix+'RRDDU', 0, screen)
    run(screen+'-single-middle', normal[:4]+[shorts[0]]+normal[4:], prefix+'RRRDDU', 4, screen)
    run(screen+'-no-shorts', normal, prefix+'RRDDU', 6, screen)
    # O Short abre o player de Shorts (F63): B sai direto para a tela de origem (no player comum o 1º B só esconde a barra).
    run(screen+'-player-back', mixed, prefix+'DRRA........B', 6, screen, player=18)

run('search-left-end', shorts, 'S.....'+'R'*20+'L'*13, 0, rail=True)
toggle = '......DRRLLL'+'D'*6+'A'+seq(7,'RBR')
run('home-toggle-focused-short', mixed, toggle+'R', 18, screen='home',
    persisted=['shorts=hide'])
run('home-toggle-row-again', mixed, toggle+'L'+'D'*6+'ARBRUR', 5, screen='home',
    persisted=['shorts=row'])

for lang, cols, large, height in [('en', 4, False, 720), ('pt', 3, False, 720),
                                 ('es', 4, True, 1080), ('ar', 4, True, 480),
                                 ('ar', 3, False, 1080)]:
    cards = [card(i, 4 <= i < 17, channel=i in (1, 19), huge=True, arabic=lang == 'ar') for i in range(29)]
    profile = f'{lang}-{cols}-{int(large)}-{height}'
    for screen in ('search', 'home'):
        run('layout-'+screen+'-'+profile, cards, ('S.....' if screen == 'search' else '......')+'D',
            screen=screen, lang=lang, cols=cols, large=large, height=height)
        effective = 2 if height == 480 else min(3, cols) if large else cols if screen == 'home' else 4
        # O canal do índice 1 vira o cartão em destaque da busca (fora da grade); na Home ele já era tirado: 3 antes dos Shorts nos dois.
        before_shorts = 3
        run('portrait-'+screen+'-'+profile, cards,
            ('S.....' if screen == 'search' else '......')+'D'*((before_shorts+effective-1)//effective),
            selected=before_shorts, screen=screen, lang=lang, cols=cols, large=large, height=height)
    # O painel de filtros abre pela pílula (6ª posição da fileira de chips): sobe até os chips (o canal em destaque é a 1ª parada).
    run('filters-'+profile, cards, 'S.....UU'+'R'*5+'A', lang=lang, cols=cols,
        large=large, height=height, filter_open=True)

# Chips da busca (A2): a direcional só anda pelos chips; buscar é com A. "Shorts" é filtro no cliente (sem nova busca).
run('search-chips-walk-no-search', mixed, 'S.....U'+'RRRR', 0, searches=1)
run('search-chip-type-searches', mixed, 'S.....U'+'RRRRA', 0, searches=2)
run('search-chip-shorts-client-only', mixed, 'S.....U'+'RRAD', 4, searches=1)
run('search-chip-rb', mixed, 'S.....U]', 0, searches=2)

cursor = {'continuationCommand': {'token': 'F52_ADVERSARIAL_NEXT'}}
run('continuation-new-row', [card(i) for i in range(40)]+[cursor], 'S.....'+'D'*9+'.....D',
    40, next_page=[card(40, True), card(41, True)])
run('continuation-extend-row', [card(0, True)]+[card(i) for i in range(1, 41)]+[cursor],
    'S.....'+'D'*10+'.....'+'U'*10+'R', 41, next_page=[card(41, True), card(42)])

for mode in ('on', 'auto'):
    for legacy in ('notebook', 'together', 'xbox_notebook'):
        name = f'migration-{mode}-{legacy}'
        run(name, mixed, seq(7,'R',8,'R'), screen='settings',
            settings=f'server={mode}\nsleep_what={legacy}\n',
            persisted=['server=off', 'sleep_what=xbox', 'shorts=hide', 'error_reports=off'])
        run(name, mixed, seq(7,'R',8,'R'), screen='settings', restart=True,
            persisted=['server=off', 'sleep_what=xbox', 'shorts=row', 'error_reports=on'])
run('invalid-settings', mixed, seq(8,'R'), screen='settings',
    settings='server=INVALID\nsleep_what=INVALID\ncols=INVALID\nshorts=INVALID\nerror_reports=INVALID\n',
    persisted=['server=off', 'sleep_what=xbox', 'shorts=row', 'error_reports=off'])
run('reports-on', mixed, seq(8,'R'), screen='settings', settings='error_reports=off\n',
    persisted=['error_reports=on'])
run('reports-on', mixed, screen='settings', restart=True, persisted=['error_reports=on'])

if not results:
    raise SystemExit('No matching adversarial cases')
failed = [r for r in results if r['status'] == 'FAIL']
print(f'{len(results)-len(failed)}/{len(results)} adversarial app cases passed', flush=True)
raise SystemExit(bool(failed))
