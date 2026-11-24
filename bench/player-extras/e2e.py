#!/usr/bin/env python3
"""Mesmo main/player do produto, transportes públicos locais e server=off."""
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT/'.work/extras-e2e'
MEDIA = ROOT/'.work/extras-media'
WORK.mkdir(parents=True, exist_ok=True)
for f in (ROOT/'bench/player-extras/fixtures').glob('*.json'):
    shutil.copyfile(f, MEDIA/f.name)
results = json.loads((WORK/"results.json").read_text()) if (WORK/"results.json").exists() else []

def run(name, keys='', seconds=6, lang='pt', at=0, speaker=False, height=720, large=False, shutdown=False, **extra):
    assets = WORK/name
    assets.mkdir(exist_ok=True)
    for font in (ROOT/'client/assets').glob('inter-*.ttf'):
        shutil.copyfile(font, assets/font.name)
    (assets/'settings.txt').write_text(f'lang={lang}\ncols=4\nquality=720\nsound={"nest" if speaker else "tv"}\nserver=off\nvideo_mode=720p60\noutput_height={height}\nlarge_text={"on" if large else "off"}\n')
    (assets/'autoplay.txt').write_text(f'4p_9OPKgcDE {at}\n')
    env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(assets), XDG_CACHE_HOME=str(assets/'.cache'), SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', LP_NUM_THREADS='2',
               XT_CAST_DISABLE='1', XT_ASSETS=str(assets), XT_QUALITY_FIXTURES=str(MEDIA),
               XT_SHOT=str(WORK/(name+'.ppm')), XT_SHOT_FRAME='2147483647',
               XT_SHOT_MS=str(int(seconds*1000)), XT_KEYS_MS='300', XT_KEYS=keys)
    env.update(extra)
    nest=None
    if speaker:
        import sys
        sys.path.insert(0,str(ROOT/'bench/standalone'))
        from cast_test import Receiver
        cert,key=WORK/'cert.pem',WORK/'key.pem'
        if not cert.exists():
            subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=fixture',
                            '-keyout',str(key),'-out',str(cert)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        nest=Receiver(cert,key)
        (assets/'nest.txt').write_text(f'127.0.0.1 {nest.port}\nNest de bancada F21\n')
    before=resource.getrusage(resource.RUSAGE_CHILDREN)
    start=time.monotonic()
    try:
        with (WORK/(name+'.log')).open('w') as log:
            subprocess.run([str(ROOT/'client/build/tube360-extras-fixture')], cwd=ROOT, env=env,
                           stdout=log, stderr=log, timeout=seconds+30, check=True)
    finally:
        if nest:
            nest.close()
            (WORK/(name+'-cast.json')).write_text(json.dumps(nest.events,indent=2))
    elapsed=time.monotonic()-start
    after=resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime
    text=(WORK/(name+'.log')).read_text()
    assert 'primeiro quadro' in text, text[-2500:]
    assert '(desligado: só console)' in text, 'server must be off'
    assert 'unexpected fixture URL' not in text, text[-2500:]
    if shutdown:
        assert 'repetição: 1' in text and 'timer: prazo do console venceu' in text and 'build PC fecha o app' in text, text[-2500:]
        assert 'repetir: ' not in text, 'loop defeated the native end timer'
        print('PASS', name, 'native end timer overrides enabled loop and exits the PC app',flush=True)
        return text
    subprocess.run(['magick', str(WORK/(name+'.ppm')), str(WORK/(name+'.png'))], check=True)
    with (WORK/(name+'.ppm')).open('rb') as shot:
        shot.readline()
        assert shot.readline().strip()==(b'640 480' if height==480 else b'1280 720'), 'wrong framebuffer dimensions'
    if lang=='es' and 'comments' in name:
        assert '"hl":"es"' in text and '"gl":"MX"' in text, 'Spanish comments locale not sent'
    results[:]=[r for r in results if r["name"]!=name]
    results.append(dict(name=name, elapsed=elapsed, cpu_seconds=cpu, percent_one_core=cpu/elapsed*100))
    if nest:
        assert not nest.errors and nest.commands('LOAD') and nest.commands('STOP'), nest.errors
        loads=nest.commands('LOAD')
        if 'roundtrip' in name: assert len(loads)==2, 'returning to 1x did not restore Nest'
        else: assert len(loads)==1, 'changed speed incorrectly loaded Nest'
    if name in ('speed-150','nest-speed','nest-roundtrip'):
        changed=text.split('velocidade: 1.50x',1)[1].split('velocidade: 1.00x',1)[0]
        assert 'som: TV' in changed, 'changed speed left TV audio muted'
    if name=='speed-paused':
        at=float(re.search(r'velocidade: 1.50x em ([\d.]+)',text)[1])
        shown=float(re.search(r'captura: .*posição ([\d.]+)',text)[1])
        assert abs(shown-at)<.08, 'speed change resumed paused playback'
    print('PASS', name, f'CPU {cpu:.3f}s/{elapsed:.3f}s', flush=True)
    return text

if __name__=='__main__':
    import sys
    wanted=set(sys.argv[1:])
    cases={
      'comments-pt': dict(keys='....DRRRA', seconds=5),
      'comments-en': dict(keys='....DRRRA', seconds=5,lang='en'),
      'comments-es': dict(keys='....DRRRA', seconds=5,lang='es'),
      'comments-sd': dict(keys='....DRRRA', seconds=5,lang='es',height=480),
      'comments-sd-long': dict(keys='....DRRRADDDD',seconds=6,lang='es',height=480),
      'comments-sd-error': dict(keys='....DRRRA',seconds=5,lang='es',height=480,XT_EXTRAS_COMMENTS_ERROR='1'),
      'comments-large': dict(keys='....DRRRA',seconds=5,lang='es',large=True),
      'speed-es': dict(keys='....DRRRRA',seconds=5,lang='es'),
      'speed-sd': dict(keys='........DRRRRA',seconds=7,lang='es',height=480,speaker=True),
      'speed-large': dict(keys='....DRRRRA',seconds=5,lang='es',large=True),
      'comments-long': dict(keys='....DRRRADDDD',seconds=6),
      'comments-back': dict(keys='....DRRRAB',seconds=6),
      'comments-scroll': dict(keys='....DRRRADDDDDDDDDDDDDDDDDDDDDDDDDDD',seconds=12),
      'comments-page2': dict(keys='....DRRRAD'+('D'*44)+'DA',seconds=18),
      'comments-error': dict(keys='....DRRRA',seconds=5,XT_EXTRAS_COMMENTS_ERROR='1'),
      'dislikes': dict(keys='....DRR',seconds=4),
      'dislikes-offline': dict(keys='....DRR',seconds=4,XT_EXTRAS_VOTES_ERROR='1'),
      'speed-panel': dict(keys='....DRRRRA',seconds=5),
      'speed-150': dict(keys='....DRRRRARRA',seconds=12),
      'speed-paused': dict(keys='....ADRRRRARRA',seconds=8),
      'loop': dict(keys='....DRRRRRA',seconds=9,at=19),
      'loop-timer': dict(keys='....DRRRRRA',seconds=12,at=19,shutdown=True,XT_SLEEP_PATH='/api/sleep/set?end=1&what=xbox'),
      'base-100': dict(seconds=12),
      'live-disabled': dict(keys='....DRRRRA',seconds=6,XT_QUALITY_FIXTURES=str(ROOT/'.work/extras-live')),
      'nest-speed': dict(keys='........DRRRRARRA',seconds=9,speaker=True),
      'nest-roundtrip': dict(keys='........DRRRRARRA....ALLA',seconds=12,speaker=True),
    }
    for name,options in cases.items():
        if not wanted or name in wanted:
            text=run(name,**options)
            if name=='live-disabled': assert 'live 1' in text and 'velocidade:' not in text
            if name=='comments-page2': assert '"continuation":"PAGE_TWO"' in text
            if name=='speed-150': assert 'velocidade: 1.50x' in text and text.count('primeiro quadro')>=2
            if name=='speed-paused': assert 'velocidade: 1.50x' in text
            if name=='loop': assert 'repetir: 4p_9OPKgcDE' in text and text.count('primeiro quadro')>=2
    (WORK/'results.json').write_text(json.dumps(results,indent=2))
