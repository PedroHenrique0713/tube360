"""App e player reais; só HTTPS e a pressão inicial vêm da fixture local."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from PIL import Image
root=Path(__file__).resolve().parents[2]
base=Path(sys.argv[1]).resolve();assets=base/'assets';assets.mkdir(parents=True,exist_ok=True)
for font in (root/'client/assets').glob('inter-*.ttf'):shutil.copy(font,assets)
(assets/'settings.txt').write_text('lang=pt\nsound=tv\nvideo_mode=1080p30\noutput_height=720\n')
proofs=base/'proofs';proofs.mkdir(exist_ok=True)
def run(name,ms,**extra):
    # Os casos compartilham assets, mas cada sessão deve testar o player desde o início.
    (assets/'local-library.json').write_text('{"version":1,"later":[],"history":[]}\n')
    env={**os.environ,'HOME':str(base),'XDG_CACHE_HOME':str(base/'cache'),'SDL_VIDEO_DRIVER':'offscreen','SDL_AUDIO_DRIVER':'dummy','XT_CAST_DISABLE':'1','XT_ASSETS':str(assets),'XT_QUALITY_FIXTURES':str(base/'media'),'XT_PLAY':'4p_9OPKgcDE','XT_SHOT':str(proofs/(name+'.ppm')),'XT_SHOT_MS':str(ms),**extra}
    for key in ('XT_YT_REPLAY','XT_CATALOG_REPLAY'):env.pop(key,None)
    result=subprocess.run([str(root/'client/build/tube360-quality-fixture')],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=ms/1000+40)
    log=result.stdout.decode(errors='replace');(proofs/(name+'.log')).write_text(log)
    assert result.returncode==0,(name,result.returncode,log[-2000:])
    image=proofs/(name+'.ppm');assert image.exists(),name
    with Image.open(image) as im:im.save(proofs/(name+'.png'))
    image.unlink()
    return log
requested=sys.argv[2:] or ['recovery','recovery-seek','preview','release','paused']
if 'recovery' in requested:
    log=run('recovery',65000,XT_QUALITY_SLOW_MS='2000',XT_QUALITY_RECOVER_MS='25000')
    down=re.search(r'fonte 1080p.* -> 720p',log);up=re.search(r'fonte 720p.* -> 1080p',log)
    assert down and up and down.start()<up.start(),'expected 1080 -> 720 -> 1080\n'+log[-6000:]
    assert log.count('console: MP4')==1,'source adaptation restarted whole playback'
    assert 'decod: falha' not in log
    print('PASS real player: 1080 -> 720 -> 1080; AAC/queue retained',flush=True)
    for line in log.splitlines():
        if 'motivo=' in line or 'folga estavel' in line:print(line,flush=True)
if 'recovery-seek' in requested:
    log=run('recovery-after-seek',65000,XT_QUALITY_SLOW_MS='2000',XT_QUALITY_RECOVER_MS='25000',XT_KEYS_MS='7000',XT_KEYS='..l..')
    assert 'busca: soltou em 0.000' in log,log[-3000:]
    assert re.search(r'fonte 720p.* -> 1080p',log),'cached seek lost original quality ceiling\n'+log[-4000:]
    print('PASS real player: cached seek at reduced quality still returns to 1080',flush=True)
if 'preview' in requested:
    log=run('seek-preview',5300,XT_KEYS_MS='700',XT_KEYS='....rrrr.')
    assert 'busca: segurando 1' in log and 'busca: soltou' not in log,log[-3000:]
    print('PASS held trigger: preview captured before release',flush=True)
if 'release' in requested:
    log=run('seek-release',9000,XT_KEYS_MS='700',XT_KEYS='....rrrr.')
    seeks=re.findall(r'busca: soltou em ([\d.]+)',log)
    assert len(seeks)==1 and float(seeks[0])>25,seeks
    print('PASS held trigger: one seek on release, target',seeks[0],flush=True)
if 'paused' in requested:
    log=run('seek-paused',9000,XT_KEYS_MS='700',XT_KEYS='...Arrrr.')
    seeks=re.findall(r'busca: soltou em ([\d.]+)',log)
    assert len(seeks)==1 and 'pause=1' in log,log[-3000:]
    capture=re.search(r'captura:.*posição ([\d.]+)',log)
    assert capture and abs(float(capture[1])-float(seeks[0]))<.2,'paused release advanced clock'
    print('PASS paused trigger: release preserves pause at',seeks[0],flush=True)
