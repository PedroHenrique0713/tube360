"""A raiz TLS de teste exige substituir, não duplicar, o objeto HTTPS do produto."""
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
work = ROOT / 'tests/f52/results/oauth-tls-fixed'
work.mkdir(parents=True, exist_ok=True)
library = work / 'libtube360-yt-fixture.a'
shutil.copyfile(ROOT / 'client/build/yt/libtube360-yt.a', library)
subprocess.run(['ar', 'd', str(library), 'https.cpp.o'], check=True)
script = ROOT / 'bench/catalog/check-oauth-tls.py'
source = script.read_text()
anchor = 'args[1:1] = objects'
assert source.count(anchor) == 1
source = source.replace(anchor, anchor + '\nargs = [' + repr(str(library)) +
                        ' if arg == "yt/libtube360-yt.a" else arg for arg in args]')
sys.argv = [str(script), str(work)]
exec(compile(source, str(script), 'exec'), {'__name__': '__main__', '__file__': str(script)})
