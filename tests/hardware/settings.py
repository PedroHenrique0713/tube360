"""Exercita saída e fontes pelo controle no app real, com respostas locais e HOME isolado."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
folder = Path(sys.argv[1]).resolve()
assets = folder / 'assets'
assets.mkdir(parents=True, exist_ok=True)
for old in assets.glob('boot-*.log'):
    old.unlink()
for font in (ROOT / 'client/assets').glob('inter-*.ttf'):
    shutil.copy(font, assets)
(assets / 'settings.txt').write_text('lang=pt\nserver=off\nsound=tv\noutput_height=720\n')
(folder / 'error.txt').write_text('network')
shot = folder / 'screen.ppm'
env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
env.update(HOME=str(folder), XDG_CACHE_HOME=str(folder / 'cache'),
           SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy',
           XT_CAST_DISABLE='1', XT_ASSETS=str(assets), XT_YT_REPLAY=str(folder),
           XT_SCREEN='settings', XT_SHOT=str(shot), XT_SHOT_MS='5000',
           XT_KEYS='D' * 9 + 'RRR' + 'DD' + 'RR', XT_KEYS_MS='100')
run = subprocess.run([str(ROOT / 'client/build/tube360')], cwd=folder, env=env,
                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
(folder / 'stdout.log').write_bytes(run.stdout)
assert run.returncode == 0, run.stdout[-3000:]
log = (assets / 'boot-app.log').read_text()
outputs = re.findall(r'end settings.output requested=(\d+) actual=(\d+x\d+)', log)
assert outputs == [('1080', '1920x1080'), ('480', '640x480'), ('720', '1280x720')], outputs
# Cada troca retorna, reconstrói fontes e volta a desenhar; o teste usa o renderer Vulkan do PC.
pending = {}
for line in log.splitlines():
    match = re.search(r' (begin|end|error) (settings.output|display.fonts) ', line)
    if not match:
        continue
    phase, stage = match.groups()
    if phase == 'begin':
        assert stage not in pending, line
        pending[stage] = line
    else:
        assert phase == 'end' and stage in pending, line
        del pending[stage]
assert not pending, pending
assert log.count('end display.fonts ') == 6, log
assert 'large_text=1 loaded=1' in log and 'large_text=0 loaded=1' in log
assert log.count('begin frame.acquire ') == 1, 'checkpoints de frame continuam limitados à abertura'
assert 'end app.exit ' in log
with Image.open(shot) as im:
    assert im.size == (1280, 720), im.size
    im.save(folder / 'screen.png')
shot.unlink()
print('PASS real settings: 720 -> 1080 -> 480 -> 720; large text on/off; 6 font rebuilds; paired durable checkpoints')
