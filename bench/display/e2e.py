"""Capturas do mesmo app com transporte gravado, sem conta ou rede reais."""
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2] / 'tests'))
from settings_nav import seq
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
work = Path(sys.argv[1]).resolve()
replay = work / 'replay'
manifest = json.loads((replay / 'responses.json').read_text())
accounts = ROOT / 'bench/account/fixtures'
public = next(rule['json'] for rule in json.loads((accounts/'responses.json').read_text()) if 'WEB' in rule.get('contains', ''))
(replay/'home-public.json').write_text(json.dumps(public))
manifest['requests'].append({'url': 'https://www.youtube.com/youtubei/v1/browse?prettyPrint=false', 'fields': {'browseId': 'FEwhat_to_watch'}, 'file': 'home-public.json'})
for index in range(1, 9):
    manifest['requests'].append({'url': f'https://i.ytimg.com/vi/video{index:06}/mqdefault.jpg', 'file': 'art.png'})
for path in (work/'media').iterdir():
    if path.suffix in ('.mp4', '.m4a'):
        shutil.copyfile(path, replay/path.name)
        manifest['requests'].append({'url': 'https://fixture.googlevideo.com/'+path.name, 'file': path.name, 'range': True})
player=json.loads((work/'media/player.json').read_text())
player['captions']={'playerCaptionsTracklistRenderer': {'captionTracks': [{'baseUrl': 'https://www.youtube.com/api/timedtext?v=4p_9OPKgcDE&lang=es', 'languageCode':'es', 'name':{'simpleText':'Español'}}]}}
(replay/'player-display.json').write_text(json.dumps(player))
(replay/'captions-display.json').write_text(json.dumps({'events':[{'tStartMs':0,'dDurationMs':10000,'segs':[{'utf8':'Un viaje para disfrutar en pantalla de tubo. Subtítulos grandes y legibles.'}]}]}))
manifest['requests'].append({'url':'https://www.youtube.com/api/timedtext?v=4p_9OPKgcDE&lang=es&fmt=json3','file':'captions-display.json'})
(replay/'watch-display.html').write_text('<script>ytcfg.set({"VISITOR_DATA":"fixture","jsUrl":"/s/player/fixture/base.js"})</script>')
(replay/'base-display.js').write_text('var a={signatureTimestamp:20775};')
manifest['requests'].extend([
    {'url': 'https://www.youtube.com/watch?v=4p_9OPKgcDE&bpctr=9999999999&has_verified=1', 'file': 'watch-display.html'},
    {'url': 'https://www.youtube.com/s/player/fixture/base.js', 'file': 'base-display.js'},
    {'url': 'https://www.youtube.com/youtubei/v1/player?prettyPrint=false', 'fields': {'videoId': '4p_9OPKgcDE'}, 'file': 'player-display.json'},
])
(replay/'responses.json').write_text(json.dumps(manifest))
wanted = set(sys.argv[2:])
results = []

def run(name, height=480, keys='', seconds=2.5, fill=False, large=False, captions=False, **args):
    if wanted and name not in wanted:
        return
    assets = work/name
    assets.mkdir(exist_ok=True)
    for font in (ROOT/'client/assets').glob('inter-*.ttf'):
        shutil.copyfile(font, assets/font.name)
    (assets/'settings.txt').write_text(f'lang=es\ncols=4\nquality=720\nsound=tv\nserver=off\noutput_height={height}\nlarge_text={"on" if large else "off"}\nvideo_fit={"fill" if fill else "fit"}\ncaptions={"on" if captions else "off"}\n')
    env = dict(os.environ, HOME=str(assets), SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy',
               XT_ASSETS=str(assets), XT_CAST_DISABLE='1', XT_CATALOG_REPLAY=str(replay),
               XT_ACCOUNT_FIXTURES=str(accounts), XT_KEYS=keys, XT_KEYS_MS='150',
               XT_SHOT=str(assets/'screen.ppm'), XT_SHOT_MS=str(int(seconds*1000)), **args)
    with (assets/'app.log').open('w') as log:
        subprocess.run([str(work/'tube360-replay')], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=35, check=True)
    subprocess.run(['magick', str(assets/'screen.ppm'), str(assets/'screen.png')], check=True)
    text = (assets/'app.log').read_text()
    assert f'tela {640 if height==480 else 1280}x{height}' in text
    assert 'servidor 0.0.0.0:8091 (desligado: só console)' in text
    assert 'captura: ' in text
    if name=='480-language-en': assert 'lang=en' in (assets/'settings.txt').read_text()
    if name=='480-language-pt': assert 'lang=pt' in (assets/'settings.txt').read_text()
    if name=='480-fill-toggle': assert 'video_fit=fill' in (assets/'settings.txt').read_text()
    if name=='720-text-toggle': assert 'large_text=on' in (assets/'settings.txt').read_text()
    if name in ('720-to480','480-to720'):
        with (assets/'screen.ppm').open('rb') as shot:
            shot.readline(); size=shot.readline().strip()
        assert size==(b'640 480' if name=='720-to480' else b'1280 720'), (name,size)
    if 'XT_PLAY' in args:
        assert 'primeiro quadro' in text and 'posição 0.00' not in text, text[-3000:]
    results.append({'name': name, 'image': str(assets/'screen.png')})

run('480-home')
run('480-home-scroll', keys='D')
run('480-rail', keys='DD', XT_SCREEN='rail')
run('480-phone', keys='DDDDDA', XT_SCREEN='rail')
run('480-settings', XT_SCREEN='settings')
# O ciclo de idioma segue o enum {EN, PT, ES, AR}: de ES, duas vezes para a direita passa pelo árabe e chega no inglês.
run('480-language-en', keys=seq(5,'RR'), XT_SCREEN='settings')
run('480-language-pt', keys=seq(5,'L'), XT_SCREEN='settings')
run('480-fill-toggle', keys=seq(12,'R'), XT_SCREEN='settings')
run('480-language', keys=seq(5), XT_SCREEN='settings')
run('480-fit-setting', keys=seq(12), XT_SCREEN='settings')
run('480-search', XT_SCREEN='search')
run('480-channel', XT_NAV='channel', XT_NAV_ID='UC_x5XG1OV2P6uZZ5FSM9Ttw')
run('480-music', XT_NAV='music', XT_MUSIC='1')
run('480-player-captions', seconds=3.5, captions=True, XT_PLAY='4p_9OPKgcDE')
run('480-player-fit', seconds=3, XT_PLAY='4p_9OPKgcDE')
run('480-player-fill', seconds=3, fill=True, XT_PLAY='4p_9OPKgcDE')
run('720-text-toggle', height=720, keys=seq(11,'R'), XT_SCREEN='settings')
run('720-default', height=720)
run('720-to480', height=720, keys=seq(9,'L'), XT_SCREEN='settings')
run('480-to720', height=480, keys=seq(9,'R'), XT_SCREEN='settings')
run('720-large-home', height=720, large=True)
run('720-large-settings', height=720, large=True, XT_SCREEN='settings')
run('720-large-player', height=720, large=True, seconds=3, XT_PLAY='4p_9OPKgcDE')
(work/'screenshots.json').write_text(json.dumps(results, indent=2))
print(json.dumps(results, indent=2))
