"""Player/FFmpeg reais; atrasos só na fronteira de decode do executável de bancada."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
base=Path(sys.argv[1]).resolve();source=Path(sys.argv[2]).resolve()
assets=base/'assets';media=base/'media'
assets.mkdir(parents=True,exist_ok=True);media.mkdir(exist_ok=True)
formats=json.loads((source/'player.json').read_text())['streamingData']['adaptiveFormats']
formats=[f for f in formats if f['itag'] in (137,136,140)]
assert {f['itag'] for f in formats}=={137,136,140}
for f in formats:
    name=f['url'].rsplit('/',1)[1];target=media/name
    if not target.exists():
        subprocess.run(['ffmpeg','-v','error','-stream_loop','2','-i',str(source/name),'-t','240','-c','copy','-movflags','+faststart',str(target)],check=True)
(media/'player.json').write_text(json.dumps(dict(playabilityStatus=dict(status='OK'),videoDetails=dict(videoId='4p_9OPKgcDE',title='F32 synthetic persistent CPU pressure',lengthSeconds='240'),streamingData=dict(expiresInSeconds='3600',adaptiveFormats=formats))))
for font in (ROOT/'client/assets').glob('inter-*.ttf'):shutil.copy(font,assets)
(assets/'settings.txt').write_text('lang=pt\nsound=tv\nvideo_mode=1080p30\n')
shot=base/'screen.ppm';shot.unlink(missing_ok=True)
env={k:v for k,v in os.environ.items() if not k.startswith('XT_')}
env.update(HOME=str(base),XDG_CACHE_HOME=str(base/'cache'),SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',XT_CAST_DISABLE='1',XT_ASSETS=str(assets),XT_QUALITY_FIXTURES=str(media),XT_PLAY='4p_9OPKgcDE',XT_QUALITY_DECODE_1080_MS='44',XT_QUALITY_DECODE_720_MS='19',XT_SHOT=str(shot),XT_SHOT_MS='170000',XT_KEYS_MS='700',XT_KEYS='.'*70+'l.')
with (base/'player.log').open('w') as output:
    run=subprocess.run([str(ROOT/'client/build/tube360-quality-fixture')],cwd=base,env=env,stdout=output,stderr=subprocess.STDOUT,timeout=210)
log=(base/'player.log').read_text(errors='replace')
assert run.returncode==0 and shot.exists(),(run.returncode,log[-4000:])
assert 'decod: falha' not in log and 'get_buffer() failed' not in log,log[-4000:]
assert len(re.findall(r'fonte 1080p.* -> 720p',log))==1,log[-6000:]
assert not re.search(r'fonte 720p.* -> 1080p',log),log[-6000:]
assert 'historico preservado na sessao, 1 degraus rejeitados' in log
assert len(re.findall('busca: soltou',log))==1 and log.count('console: MP4')==2,'only explicit seek may restart player'
capture=re.search(r'captura:.*tela (\d+),.*posição ([\d.]+)',log)
assert capture and capture[1]=='2' and 100<float(capture[2])<210,'capture must still show the player, not an ended seek'
assert 'aguardar degrau que falhou' in log
# Mesmo depois do backoff, a projeção precisa continuar impedindo a subida.
predictions=re.findall(r'subida sem folga de CPU.*decode medido ([\d.]+).*projetado ([\d.]+)%.*bloqueio 0 ms',log)
assert predictions and any(float(ms)>=19 and float(load)>100 for ms,load in predictions),log[-6000:]
with Image.open(shot) as image:image.save(base/'screen.png')
shot.unlink()
print('PASS real player 170s: 1080/44ms -> stable 720/19ms, no re-upgrade after cooldown; explicit seek retains failed rung',flush=True)
for line in log.splitlines():
    if 'degrau rejeitado' in line or 'historico preservado' in line or 'qualidade: fonte' in line:print(line,flush=True)
print('Projection after cooldown:',predictions[-1],flush=True)
