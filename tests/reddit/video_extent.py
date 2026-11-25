"""Framebuffer real: as barras ciano devem chegar à borda direita em SD/720/1080."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
base=Path(sys.argv[1]).resolve();source=Path(sys.argv[2]).resolve()
app=Path(sys.argv[3]).resolve() if len(sys.argv)>3 else ROOT/'client/build/tube360-quality-fixture'
base.mkdir(parents=True,exist_ok=True);results=[]
for height,width in ((480,640),(720,1280),(1080,1920)):
    folder=base/str(height);assets=folder/'assets';assets.mkdir(parents=True,exist_ok=True)
    for font in (ROOT/'client/assets').glob('inter-*.ttf'):shutil.copy(font,assets)
    (assets/'settings.txt').write_text(f'lang=pt\nserver=off\nsound=tv\nsponsorblock=off\nvideo_mode=480p\noutput_height={height}\n')
    shot=folder/'screen.ppm';shot.unlink(missing_ok=True)
    env={k:v for k,v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(folder),XDG_CACHE_HOME=str(folder/'cache'),SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',
               LP_NUM_THREADS='2',XT_CAST_DISABLE='1',XT_ASSETS=str(assets),XT_QUALITY_FIXTURES=str(source),
               XT_PLAY='4p_9OPKgcDE',XT_SHOT=str(shot),XT_SHOT_MS='7000')
    with (folder/'player.log').open('w') as log:
        run=subprocess.run([str(app)],cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=25)
    assert run.returncode==0 and shot.exists(),(height,run.returncode)
    with Image.open(shot) as image:
        assert image.size==(width,height),image.size
        rgb=image.convert('RGB')
        # A faixa direita da mídia sintética é ciano, com pontos/linha móveis esparsos.
        pixels=[rgb.getpixel((int(width*x),int(height*y))) for x in (.86,.90,.94) for y in (.25,.35,.45)]
        cyan=sum(r<90 and g>120 and b>120 for r,g,b in pixels)
        image.save(folder/'screen.png')
    shot.unlink()
    results.append(dict(height=height,width=width,cyan=cyan,pixels=pixels,passed=cyan>=7))
    print(('PASS' if cyan>=7 else 'FAIL'),f'physical video {width}x{height}: cyan right edge {cyan}/9',flush=True)
(base/'results.json').write_text(json.dumps(results,indent=2)+'\n')
assert all(r['passed'] for r in results),'video scaled/cropped with the UI transform; see results.json'
