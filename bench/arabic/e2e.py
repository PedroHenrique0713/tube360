"""Capture production UI with closed offline transports, isolated HOME/XDG and synthetic media."""
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

ROOT=Path(__file__).resolve().parents[2]
work=Path(sys.argv[1]).resolve()
binary=work/'tube360-replay'
wanted=set(sys.argv[2:])
results=[]

def run(name, lang='ar', height=720, large=False, keys='', seconds=2.5, missing=False, plain=False, **extra):
    if wanted and name not in wanted: return
    assets=work/name
    assets.mkdir(exist_ok=True)
    for font in (ROOT/'client/assets').glob('*.ttf'):
        if not missing or font.name.startswith('inter-'): shutil.copyfile(font,assets/font.name)
    (assets/'settings.txt').write_text(f'lang={lang}\ncols=4\nsound=tv\nserver=off\noutput_height={height}\nlarge_text={"on" if large else "off"}\n')
    replay=work/('plain-replay' if plain else 'replay')
    accounts=work/('plain-accounts' if plain else 'accounts')
    env=dict(os.environ,HOME=str(work/'home'),XDG_CONFIG_HOME=str(work/'config'),XDG_CACHE_HOME=str(work/'cache'),
        SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',XT_ASSETS=str(assets),XT_CAST_DISABLE='1',
        XT_CATALOG_REPLAY=str(replay),XT_ACCOUNT_FIXTURES=str(accounts),XT_KEYS=keys,XT_KEYS_MS='180',
        XT_SHOT=str(assets/'screen.ppm'),XT_SHOT_MS=str(int(seconds*1000)),**extra)
    with (assets/'app.log').open('w') as log:
        subprocess.run([str(binary)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=65,check=True)
    text=(assets/'app.log').read_text()
    assert 'captura:' in text and 'desligado: só console' in text,text[-2000:]
    if missing: assert 'Arabic font missing:' in text
    elif plain: assert 'Arabic fonts:' not in text,'Latin-only interface must not load Noto'
    else: assert 'Arabic fonts:' in text and 'atlas full' not in text,text[-4000:]
    if 'XT_PLAY' in extra and extra['XT_PLAY']!='error000001': assert 'primeiro quadro' in text,text[-4000:]
    subprocess.run(['magick',str(assets/'screen.ppm'),str(assets/'screen.png')],check=True)
    results.append(dict(name=name,image=str(assets/'screen.png')))

run('home-ar')
run('settings-ar',XT_SCREEN='settings',keys=seq(5),seconds=3)
run('search-ar',keys='YRAAA',seconds=3)
run('search-latin-ar',keys='Y]ARA',seconds=3)
run('player-ar',XT_PLAY='zjSVzGsljwA',seconds=3.5)
run('error-ar',XT_PLAY='error000001',seconds=3)
run('titles-pt',lang='pt')
run('channel-pt',lang='pt',XT_NAV='channel',XT_NAV_ID='UC0000000000000000000001')
run('titles-en',lang='en')
run('titles-es',lang='es')
run('large-home-ar',large=True)
run('large-settings-ar',large=True,XT_SCREEN='settings',keys=seq(5),seconds=3)
run('crt-home-ar',height=480)
run('crt-settings-ar',height=480,XT_SCREEN='settings',keys=seq(5),seconds=3)
run('crt-search-ar',height=480,keys='YRAAA',seconds=3)
run('crt-error-ar',height=480,XT_PLAY='error000001',seconds=3)
run('1080-home-ar',height=1080)
run('1080-large-settings-ar',height=1080,large=True,XT_SCREEN='settings')
run('missing-font',missing=True)
run('latin-only',lang='pt',plain=True)
run('search-back-ar',keys='YRAAAB',seconds=3)
if not wanted or 'search-back-ar' in wanted:
    assert 'tela 0, vídeo' in (work/'search-back-ar/app.log').read_text()
# Language can switch at runtime, persist and switch back without changing controller directions.
run('switch-to-ar',lang='es',XT_SCREEN='settings',keys=seq(5,'R'),seconds=3.5)
if not wanted or 'switch-to-ar' in wanted:
    assert 'lang=ar' in (work/'switch-to-ar/settings.txt').read_text()
run('switch-from-ar',lang='ar',XT_SCREEN='settings',keys=seq(5,'R'),seconds=3.5)
if not wanted or 'switch-from-ar' in wanted:
    assert 'lang=en' in (work/'switch-from-ar/settings.txt').read_text()
(work/'screenshots.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
