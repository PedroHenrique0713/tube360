"""App/player/decoder/Vulkan reais: escolhas manuais, oferta esparsa, pausa e aviso sem fallback."""
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
groups=set(sys.argv[3:] or ('sources','ui','warnings','automatic'))
assert groups<= {'sources','ui','warnings','automatic'},groups
base.mkdir(parents=True,exist_ok=True);media=base/'media';media.mkdir(exist_ok=True)
formats=json.loads((source/'player.json').read_text())['streamingData']['adaptiveFormats']
formats=[f for f in formats if f.get('height',1080)>480 or f['mimeType'].startswith('audio/')]
for f in formats:
    name=f['url'].rsplit('/',1)[1];target=media/name
    if not target.exists():target.symlink_to(source/name)
for h,tag in ((480,135),(360,134),(240,133),(144,160)):
    path=media/f'{h}.mp4';width=h*16//9//2*2
    if not path.exists():subprocess.run(['ffmpeg','-v','error','-i',str(source/'720.mp4'),'-vf',f'scale={width}:{h}','-filter_threads','1','-c:v','libx264','-preset','ultrafast','-crf','23','-threads','2','-g','30','-an','-movflags','+faststart',str(path)],check=True)
    formats.append(dict(itag=tag,width=width,height=h,fps=30,bitrate=int(path.stat().st_size*8/80),mimeType='video/mp4; codecs="avc1"',url=f'https://fixture.googlevideo.com/{h}.mp4'))
sparse=[f for f in formats if f['itag'] in (135,160,140)]
sparse.append(dict(itag=242,width=426,height=240,fps=30,bitrate=200000,mimeType='video/webm; codecs="vp9"',url='https://fixture.googlevideo.com/not-avc.webm'))
results=[]
def run(name,*,lang='pt',sd=False,mode='auto',selected=None,keys='',ms=4000,screen='',extra=None,missing_file=None):
    work=base/name;assets=work/'assets';files=work/'media';assets.mkdir(parents=True,exist_ok=True);files.mkdir(exist_ok=True)
    for font in (ROOT/'client/assets').glob('inter-*.ttf'):shutil.copy(font,assets)
    (assets/'settings.txt').write_text(f'lang={lang}\nsound=tv\nserver=off\nvideo_mode={mode}\noutput_height={480 if sd else 720}\n')
    for f in selected or formats:
        filename=f['url'].rsplit('/',1)[1];target=files/filename;origin=media/filename
        target.unlink(missing_ok=True)
        if origin.exists() and filename!=missing_file:target.symlink_to(origin)
    (files/'player.json').write_text(json.dumps(dict(playabilityStatus=dict(status='OK'),videoDetails=dict(videoId='4p_9OPKgcDE',title='F32 synthetic manual quality',lengthSeconds='80'),streamingData=dict(expiresInSeconds='3600',adaptiveFormats=selected or formats))))
    shot=work/'screen.ppm';shot.unlink(missing_ok=True)
    env={k:v for k,v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(work),XDG_CACHE_HOME=str(work/'cache'),SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',XT_CAST_DISABLE='1',XT_ASSETS=str(assets),XT_QUALITY_FIXTURES=str(files),XT_SHOT=str(shot),XT_SHOT_MS=str(ms),XT_KEYS=keys,XT_KEYS_MS='700')
    if screen:env['XT_SCREEN']=screen
    else:env['XT_PLAY']='4p_9OPKgcDE'
    env.update(extra or {})
    with (work/'player.log').open('w') as out:done=subprocess.run([str(ROOT/'client/build/tube360-quality-fixture')],cwd=work,env=env,stdout=out,stderr=subprocess.STDOUT,timeout=ms/1000+30)
    log=(work/'player.log').read_text(errors='replace')
    assert done.returncode==0 and shot.exists(),(name,done.returncode,log[-3500:])
    with Image.open(shot) as image:
        assert image.size==((640,480) if sd else (1280,720)),(name,image.size)
        image.save(work/'screen.png')
    shot.unlink();results.append(dict(name=name,screenshot=str(work/'screen.png'),log=str(work/'player.log')))
    return log,(assets/'settings.txt').read_text()

for h,tag in ((480,135),(360,134),(240,133),(144,160)):
    if 'sources' not in groups:break
    log,_=run('fixed-'+str(h),mode=str(h)+'p',ms=3000)
    assert f'console: MP4 {h}p, itags {tag}/140' in log and f'selecao {h}p fixa=1' in log,log[-3000:]
    assert 'qualidade: fonte ' not in log and 'reserva no servidor' not in log and 'decod: falha' not in log
    print('PASS fixed source',h,tag,flush=True)

for lang in ('pt','en','es'):
    for sd in (False,True):
        if 'ui' not in groups:break
        suffix=lang+('-sd' if sd else '-hd')
        log,settings=run('settings-'+suffix,lang=lang,sd=sd,screen='settings',keys='DDRRRRRR',ms=6600)
        assert 'video_mode=144p' in settings,(suffix,settings)
        log,settings=run('menu-'+suffix,lang=lang,sd=sd,mode='480p',selected=sparse,keys='....AYDA.Y',ms=8000)
        modes=re.findall(r'captura opcao qualidade: ([^ ]+) \|',log)
        assert modes==['auto','480p','144p'],(suffix,modes,log[-2500:])
        assert 'fonte=144 pausa=1' in log and 'video_mode=144p' in settings,log[-3500:]
        starts=re.findall(r'console: MP4 (\d+)p, itags (\d+)/140, início ([\d.]+)',log)
        assert len(starts)==2 and starts[0][:2]==('480','135') and starts[1][:2]==('144','160') and 1<float(starts[1][2])<6,starts
        print('PASS controller settings/menu',suffix,'only auto/480/144; source 160, pause/position retained',flush=True)
    if 'warnings' not in groups:continue
    log,_=run('unavailable-'+lang,lang=lang,sd=True,mode='240p',selected=sparse,ms=2500)
    assert 'erro=manual_quality_unavailable' in log and 'fonte=0' in log and 'console: MP4' not in log and 'reserva no servidor' not in log,log[-2500:]
    log,_=run('pressure-'+lang,lang=lang,sd=True,mode='480p',extra={'XT_QUALITY_DECODE_720_MS':'44'},ms=18000)
    assert 'manual 480p itag 135 sob pressao sustentada' in log and 'aviso=1' in log,log[-5000:]
    assert log.count('console: MP4')==1 and 'qualidade: fonte ' not in log and 'reserva no servidor' not in log,log[-4000:]
    print('PASS translated unavailable and manual overload warning',lang,flush=True)

if 'warnings' in groups:
    log,_=run('failed-source',mode='480p',missing_file='480.mp4',ms=4000)
    assert 'erro=manual_quality_failed' in log,log[-3500:]
    assert 'abertura direta' not in log and 'reserva no servidor' not in log and log.count('console: MP4')==1,log[-3500:]
if 'automatic' in groups:
    log,settings=run('return-auto',mode='480p',keys='....YUUUA',ms=35000,extra={'XT_QUALITY_SLOW_MS':'2000','XT_QUALITY_RECOVER_MS':'25000'})
    assert 'video_mode=auto' in settings and 'selecao auto fixa=0' in log,log[-3500:]
    assert re.search(r'fonte 1080p.* -> 720p',log),log[-5000:]
    print('PASS manual -> Automatic restores original adaptive ladder',flush=True)
suffix='' if len(groups)==4 else '-'+ '-'.join(sorted(groups))
(base/('results'+suffix+'.json')).write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')
print('PASS manual quality E2E:',len(results),'captures',flush=True)
