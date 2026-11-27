"""Logs do app real e sobrevivência do último marcador a _Exit/SIGKILL em arquivos locais."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
base=Path(sys.argv[1]).resolve();base.mkdir(parents=True,exist_ok=True)
for name in ('normal','missing-fonts','video-fail','sdl-fail','long-path'):
    folder=base/name
    if name=='long-path':folder=folder/('USB external with spaces '+('x'*90))/('folder '+('y'*90))
    assets=folder/'assets';assets.mkdir(parents=True,exist_ok=True)
    for old in assets.glob('boot-*.log'):old.unlink()
    (assets/'settings.txt').write_text('lang=pt\nsound=tv\n')
    if name!='missing-fonts':
        for font in (ROOT/'client/assets').glob('inter-*.ttf'):shutil.copy(font,assets)
    (folder/'error.txt').write_text('network')
    shot=folder/'screen.ppm';shot.unlink(missing_ok=True)
    env={k:v for k,v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(folder),XDG_CACHE_HOME=str(folder/'cache'),SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',XT_CAST_DISABLE='1',XT_ASSETS=str(assets),XT_YT_REPLAY=str(folder),XT_SHOT=str(shot),XT_SHOT_MS='1000',XT_KEYS='B' if name.endswith('fail') or name=='missing-fonts' else '')
    if name=='video-fail':env['XT_STARTUP_ERROR']='video'
    if name=='sdl-fail':env['SDL_AUDIO_DRIVER']='tube360-missing'
    run=subprocess.run([str(ROOT/'client/build/tube360')],cwd=folder,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=15)
    (folder/'stdout.log').write_bytes(run.stdout)
    expected=0 if name in ('normal','long-path') else 1
    assert run.returncode==expected,(name,run.returncode,run.stdout[-1500:])
    text=(assets/'boot-app.log').read_text()
    assert 'begin entry' in text and 'info main' in text and 'info storage.paths' in text,text
    if expected==0:
        stages=['settings','sdl.init','storage.writable','fonts.preflight','gpu.instance','gpu.device','gpu.swapchain','gpu.resources','gpu.white-texture','gpu.init','fonts.read','fonts.upload','fonts.atlas','icons','video.memory','audio.init','network.init','network.workers','phone.start','frame.fence','frame.acquire','frame.submit','frame.present','frame.complete','app.first-frame','app.exit']
        for stage in stages:
            end=text.find(f'end {stage} ')
            assert end>=0,(name,stage,text[-3000:])
            if stage!='app.exit':assert 0<=text.find(f'begin {stage} ')<end,(name,stage,'unordered checkpoints')
        assert text.count('begin frame.acquire ')==1,'steady-state frames should not write boot checkpoints'
        assert 'desktop recorded responses; no TLS request made' in text,text
    else:
        assert 'error startup.fail' in text,text
        if name=='missing-fonts':assert 'begin gpu.init' not in text,text
        if name=='video-fail':assert 'end gpu.cleanup' in text,text
        if name=='sdl-fail':assert 'error sdl.init' in text and 'begin fonts.preflight' not in text,text
    assert shot.exists(),name
    with Image.open(shot) as im:im.save(folder/'screen.png')
    shot.unlink()
    print('PASS app boot',name,'rows',len(text.splitlines()),flush=True)
# Mesmo logger C, sem a UI ou uma saída normal que pudesse esconder falta de flush.
exe=base/'stop'
subprocess.run(['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-I'+str(ROOT/'client/src'),str(ROOT/'tests/reddit/boot_stop.c'),str(ROOT/'client/src/boot_log.c'),str(ROOT/'client/src/boot_early.c'),str(ROOT/'client/src/video_policy.c'),str(ROOT/'client/src/board_sim.c'),'-pthread','-o',str(exe)],check=True)
for mode in ('exit','kill'):
    folder=base/('abrupt-'+mode);folder.mkdir(exist_ok=True)
    log=folder/'boot-test.log';log.unlink(missing_ok=True)
    proc=subprocess.Popen([str(exe),str(folder),mode])
    try:
        if mode=='exit':assert proc.wait(timeout=5)==23
        else:
            until=time.monotonic()+5
            while time.monotonic()<until and (not log.exists() or 'fixture.stop' not in log.read_text()):time.sleep(.01)
            assert proc.poll() is None
            proc.kill();proc.wait(timeout=5)
        assert 'begin fixture.stop' in log.read_text() and 'end fixture.stop' not in log.read_text()
    finally:
        if proc.poll() is None:proc.kill();proc.wait(timeout=5)
    print('PASS durable marker survives',mode,'without normal close',flush=True)
