"""Player real com todos os degraus disponíveis; abertura transitória, recuperação e 720p60 sem 136."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
base=Path(sys.argv[1]).resolve();base.mkdir(parents=True,exist_ok=True)
source=Path(sys.argv[2]).resolve()
formats=json.loads((source/'player.json').read_text())['streamingData']['adaptiveFormats']
for height,tag in ((480,135),(360,134)):
    path=base/f'{height}.mp4'
    if not path.exists():
        subprocess.run(['ffmpeg','-v','error','-i',str(source/'720.mp4'),'-vf',f'scale={height*16//9//2*2}:{height}','-filter_threads','1','-c:v','libx264','-preset','ultrafast','-crf','23','-threads','2','-g','30','-an','-movflags','+faststart',str(path)],check=True)
    formats.append(dict(itag=tag,width=height*16//9//2*2,height=height,fps=30,bitrate=int(path.stat().st_size*8/80),mimeType='video/mp4; codecs="avc1"',url=f'https://fixture.googlevideo.com/{height}.mp4'))
for name,ms,extra in (
    ('opening',25000,dict(XT_QUALITY_SLOW_MS='2000',XT_QUALITY_RECOVER_MS='8000')),
    ('recovery',65000,dict(XT_QUALITY_SLOW_MS='2000',XT_QUALITY_RECOVER_MS='25000')),
    ('cadence',35000,dict(XT_QUALITY_RENDER_MS='25'))):
    work=base/name;assets=work/'assets';media=work/'media';assets.mkdir(parents=True,exist_ok=True);media.mkdir(exist_ok=True)
    for font in (ROOT/'client/assets').glob('inter-*.ttf'):shutil.copy(font,assets)
    # O caso 298/134 omite video_mode: reproduz o perfil padrão do relato.
    (assets/'settings.txt').write_text('lang=pt\nsound=tv\n'+('' if name=='cadence' else 'video_mode=1080p30\n'))
    selected=[f for f in formats if name!='cadence' or f['itag'] in (298,134,140)]
    for fmt in selected:
        filename=fmt['url'].rsplit('/',1)[1];origin=source/filename
        if not origin.exists():origin=base/filename
        target=media/filename
        if not target.exists():target.symlink_to(origin)
    (media/'player.json').write_text(json.dumps(dict(playabilityStatus=dict(status='OK'),videoDetails=dict(videoId='wWC8wVI4E3E' if name=='cadence' else '4p_9OPKgcDE',title='F32 synthetic ladder',lengthSeconds='80'),streamingData=dict(expiresInSeconds='3600',adaptiveFormats=selected))))
    shot=work/'screen.ppm';shot.unlink(missing_ok=True)
    env={k:v for k,v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(work),XDG_CACHE_HOME=str(work/'cache'),SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',XT_CAST_DISABLE='1',XT_ASSETS=str(assets),XT_QUALITY_FIXTURES=str(media),XT_PLAY='wWC8wVI4E3E' if name=='cadence' else '4p_9OPKgcDE',XT_SHOT=str(shot),XT_SHOT_MS=str(ms),**extra)
    run=subprocess.run([str(ROOT/'client/build/tube360-quality-fixture')],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=ms/1000+40)
    log=run.stdout.decode(errors='replace');(work/'player.log').write_text(log)
    assert run.returncode==0 and shot.exists(),(name,run.returncode,log[-3000:])
    assert 'decod: falha' not in log and 'get_buffer() failed' not in log,(name,log[-3000:])
    assert log.count('console: MP4')==1 and log.count('audio: selecionada')==1,'adaptation restarted player/AAC'
    assert not re.search(r'fonte .* -> (480|360)p',log),(name,log[-6000:])
    if name=='opening':assert 'qualidade: fonte ' not in log,log[-5000:]
    if name=='recovery':
        assert re.search(r'fonte 1080p.* -> 720p',log) and re.search(r'fonte 720p.* -> 1080p',log),log[-6000:]
    if name=='cadence':
        assert 'itags 298/140' in log and 'apresentacao 60 -> 30' in log,log[-6000:]
        assert 'VOD 720p 60.000 fps, alvo 30' in log,log[-6000:]
    with Image.open(shot) as image:image.save(work/'screen.png')
    shot.unlink();print('PASS player',name,'no 480/360 drop; one AAC/player session',flush=True)
    for line in log.splitlines():
        if 'motivo=' in line or 'folga estavel' in line:print(line,flush=True)
