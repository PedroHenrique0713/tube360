"""Configurações reais por controle: saída, idioma, atlas/texto e fit; Vulkan desktop real."""
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2] / 'tests'))
from settings_nav import seq
import os
from pathlib import Path
import shutil
import subprocess
import sys
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
base=Path(sys.argv[1]).resolve();base.mkdir(parents=True,exist_ok=True)
sd=len(sys.argv)>2 and sys.argv[2]=='480'
keys=seq(5,'RRRR',9,'RRR',11,'R',12,'R')
for lang in ('pt','en','es'):
    for fixed in (False,True):
        name=f'{lang}-'+('fixed' if fixed else 'resize');work=base/name;assets=work/'assets';assets.mkdir(parents=True,exist_ok=True)
        for font in (ROOT/'client/assets').glob('*.ttf'):shutil.copy(font,assets)
        (assets/'settings.txt').write_text(f'lang={lang}\nsound=tv\noutput_height={480 if sd else 720}\n')
        (work/'error.txt').write_text('network');shot=work/'screen.ppm';shot.unlink(missing_ok=True)
        boot=assets/'boot-app.log';boot.unlink(missing_ok=True)
        env={k:v for k,v in os.environ.items() if not k.startswith('XT_')}
        env.update(HOME=str(work),XDG_CACHE_HOME=str(work/'cache'),SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',XT_CAST_DISABLE='1',XT_ASSETS=str(assets),XT_YT_REPLAY=str(work),XT_SCREEN='settings',XT_KEYS=keys,XT_KEYS_MS='100',XT_SHOT=str(shot),XT_SHOT_MS='6000',XT_TEST_FIXED_OUTPUT='1' if fixed else '0')
        run=subprocess.run([str(ROOT/'client/build/tube360')],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20)
        log=run.stdout.decode(errors='replace');(work/'stdout.log').write_text(log)
        assert run.returncode==0 and shot.exists(),(name,run.returncode,log[-2500:])
        events=boot.read_text();assert events.count('info settings.language')==4 and 'info settings.fit fill=1' in events,events[-2500:]
        assert events.count('begin settings.output ')==3,events[-2500:]
        expected=2 if fixed else 5
        assert events.count('end fonts.retire ')==expected,(name,events)
        assert events.count('begin settings.output-retire ')==(0 if fixed else 3),events
        assert ('fixed: display, framebuffer, pipelines and atlas unchanged' in events)==fixed,events
        # Retirar só depois de upload concluído, em cada substituição real do atlas.
        parts=events.split('begin fonts.pack ')[1:]
        for part in parts:
            assert part.index('end settings.gpu-idle')<part.index('end settings.commands-reset')<part.index('begin fonts.upload')<part.index('end fonts.upload')<part.index('begin fonts.retire'),part
        if not fixed:assert events.count('end app.first-frame ')==4,events[-3000:]
        settings=(assets/'settings.txt').read_text();assert f'lang={lang}' in settings and 'large_text=on' in settings and 'video_fit=fill' in settings,settings
        with Image.open(shot) as image:
            assert image.size==((640,480) if sd else (1280,720));image.save(work/'screen.png')
        shot.unlink();print('PASS settings',name,'outputs=3 atlases='+str(expected)+' lang=4 fit=1; resource order checked',flush=True)
