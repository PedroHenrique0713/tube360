#!/usr/bin/env python3
"""Compile/run the launcher contracts using this worktree's PC archives; no network."""
from pathlib import Path
import subprocess
import argparse
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('build',nargs='?',type=Path,default=ROOT/'client/build')
parser.add_argument('--real-log',type=Path)
parser.add_argument('--write-fixture',action='store_true')
a=parser.parse_args();build=a.build.resolve()
with tempfile.TemporaryDirectory(prefix='tube360-f54-') as temporary:
    base = Path(temporary)
    libraries = []
    for name in ('libtube360-update-core.a', 'libtube360-boot.a', 'libmbedcrypto.a'):
        matches = list(build.rglob(name))
        assert len(matches)==1, (name,matches)
        libraries.append(str(matches[0]))
    binary = base/'reports'
    subprocess.run(['c++','-std=c++20','-O2','-I'+str(ROOT/'client/src'),'-I'+str(ROOT/'client/third_party'),
                    str(ROOT/'tests/update/error-reports.cpp'),str(ROOT/'client/src/update/launcher.cpp'),
                    '-Wl,--start-group',*libraries,'-Wl,--end-group','-pthread','-o',str(binary)],check=True)
    # Log real já existente, só leitura. Nada bruto é copiado para o repositório ou enviado.
    real = a.real_log or ROOT/'tests/update/fixtures/error-report-console.txt'
    if a.write_fixture:
        assert a.real_log
        cleaned=subprocess.check_output([str(binary),'--scrub',str(a.real_log)])
        (ROOT/'tests/update/fixtures').mkdir(exist_ok=True)
        (ROOT/'tests/update/fixtures/error-report-console.txt').write_bytes(cleaned)
    if not real.exists():
        raise SystemExit('Pass --real-log /path/to/local.log or use the committed sanitized fixture')
    subprocess.run([str(binary),str(base),str(real)],check=True,timeout=15)
