"""Capturas do app real com replay local e telas de início sem TTF; sem tráfego externo."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from PIL import Image, ImageDraw
root=Path(__file__).resolve().parents[2]
base=Path(sys.argv[1]).resolve();base.mkdir(parents=True,exist_ok=True)
app=root/'client/build/tube360'
proofs=base/'proofs';proofs.mkdir(exist_ok=True)
for lang in ('pt','en','es','ar'):
    for case in ('tls_date','tls','dns','network','http','json','fonts','sdl','video','folder','launch','timeout'):
        name=f'{lang}-{case}';folder=base/name;folder.mkdir(exist_ok=True)
        assets=folder/'assets';assets.mkdir(exist_ok=True)
        (assets/'settings.txt').write_text(f'lang={lang}\nsound=tv\n')
        if case!='fonts':
            for f in (root/'client/assets').glob('*.ttf'):shutil.copy(f,assets)
        (folder/'error.txt').write_text(case)
        shot=proofs/(name+'.ppm');shot.unlink(missing_ok=True)
        env={**os.environ,'HOME':str(folder),'XDG_CACHE_HOME':str(folder/'cache'),'SDL_VIDEO_DRIVER':'offscreen','SDL_AUDIO_DRIVER':'dummy','XT_CAST_DISABLE':'1','XT_ASSETS':str(assets),'XT_YT_REPLAY':str(folder),'XT_SHOT':str(shot),'XT_SHOT_MS':'400','XT_KEYS':'B'}
        if case in ('video','launch','timeout'):env['XT_STARTUP_ERROR']=case
        if case=='sdl':env['SDL_AUDIO_DRIVER']='tube360-driver-missing'
        if case=='folder':
            # Uma pasta real sem permissão de escrita, mantendo o idioma legível.
            assets.chmod(0o555)
        try:
            result=subprocess.run([str(app)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=15)
        finally:
            assets.chmod(0o755)
        log=result.stdout.decode(errors='replace');(proofs/(name+'.log')).write_text(log)
        expected=1 if case in ('fonts','sdl','video','folder','launch','timeout') else 0
        assert result.returncode==expected,(name,result.returncode,log[-2000:])
        assert shot.exists(),(name,log)
        with Image.open(shot) as im:
            im.save(proofs/(name+'.png'))
            assert im.size==(1280,720)
        shot.unlink()
        if expected:
            # A pasta sem permissão também impede o log local; stderr continua disponível.
            assert 'startup:' in log,name
            if lang=='ar':assert 'Tube360 could not start' in log,'embedded emergency font needs readable English fallback'
            if case!='folder':assert 'B: exit' in log and 'B: exit' in (assets/'startup.log').read_text(),name
        else:
            assert 'catalogo: falha' in log,name
            if case=='tls_date':assert 'UTC' in log,name
        print('PASS screenshot',name,flush=True)
# Prova de espera real: sem XT_KEYS, tela de fontes não fecha por tempo.
waitdir=base/'wait';waitdir.mkdir(exist_ok=True)
env={**os.environ,'SDL_VIDEO_DRIVER':'offscreen','SDL_AUDIO_DRIVER':'dummy','XT_ASSETS':str(waitdir),'XT_SHOT':str(proofs/'wait.ppm')}
for key in ('XT_KEYS','XT_STARTUP_ERROR','XT_YT_REPLAY'):env.pop(key,None)
(proofs/'wait.ppm').unlink(missing_ok=True)
with (proofs/'wait.log').open('w') as log:
    p=subprocess.Popen([str(app)],env=env,stdout=log,stderr=log)
    try:
        deadline=time.monotonic()+5
        while not (proofs/'wait.ppm').exists() and time.monotonic()<deadline:time.sleep(.05)
        assert (proofs/'wait.ppm').exists()
        time.sleep(.6);assert p.poll() is None,'startup screen closed without a button'
    finally:p.terminate();p.wait(timeout=5)
print('PASS missing fonts stays open without a button',flush=True)
for group,cases in [('network',('tls_date','tls','dns','network','http','json')),('startup',('fonts','sdl','video','folder','launch','timeout'))]:
    montage=Image.new('RGB',(1704,240*len(cases)),(24,24,29));draw=ImageDraw.Draw(montage)
    for row,case in enumerate(cases):
        for col,lang in enumerate(('pt','en','es','ar')):
            with Image.open(proofs/f'{lang}-{case}.png') as im:montage.paste(im.resize((426,240)),(col*426,row*240))
    montage.save(proofs/f'{group}-montage.png')
