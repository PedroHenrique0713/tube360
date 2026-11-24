#!/usr/bin/env python3
"""Valida o kit Xbox real: arquivos, confiança, assinaturas, stage e reserva no PC."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import zipfile
ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--directory', type=Path, default=ROOT / '.work/update-console')
a = p.parse_args()
kit = a.directory.resolve()
metadata = json.loads((kit / 'kit.json').read_text())
archive = next((kit / 'install').glob('*.zip'))
subprocess.run([sys.executable, str(ROOT / 'tests/update/package-test.py'), str(archive), '--test'], check=True)
public_key = metadata['public_key'].encode()
with zipfile.ZipFile(archive) as zipped:
    installed = zipped.read('Tube360 Teste/tube360.xex')
    launcher = zipped.read('Tube360 Teste/default.xex')
for binary in (installed, launcher, *(f.read_bytes() for f in (kit / 'public').glob('*.xex'))):
    assert public_key in binary and b'BEGIN PRIVATE KEY' not in binary
assert b'0.3.2-f26-n' in installed
assert b'0.3.2-f26-good' in (kit / 'public/good.xex').read_bytes()
failed = (kit / 'public/fail.xex').read_bytes()
assert b'0.3.2-f26-fail' in failed and b'TEST boot confirmation intentionally withheld' in failed
assert b'TEST boot confirmation intentionally withheld' not in (kit / 'public/good.xex').read_bytes()
production_key = re.search(rb'publicKeyHex = "([0-9a-f]{64})"', (ROOT / 'client/src/update/public_key.hpp').read_bytes())[1]
assert public_key != production_key
build = ROOT / 'client/build'
link = shlex.split((build / 'CMakeFiles/tube360.dir/link.txt').read_text())
libs = [x if x.startswith(('/', '-')) else str(build / x)
        for x in link if x.endswith('.a') or '.so' in x or x.startswith('-l')]
flags = (build / 'CMakeFiles/tube360-update.dir/flags.make').read_text()
includes = shlex.split(flags.split('CXX_INCLUDES = ', 1)[1].splitlines()[0])
includes += ['-I' + str(ROOT / 'client/third_party/monocypher')]
with tempfile.TemporaryDirectory(prefix='tube360-real-kit-') as temporary:
    folder = Path(temporary)
    executable = folder / 'kit-proof'
    subprocess.run([link[0], '-std=c++20', '-O2', *includes,
                    '-DTUBE360_UPDATE_TEST_CONFIG="' + str(kit / 'private/test-config.hpp') + '"',
                    str(ROOT / 'tests/update/kit.cpp'), str(ROOT / 'client/src/update/common.cpp'),
                    str(ROOT / 'client/src/update/app.cpp'), *libs, '-o', str(executable)], check=True)
    (folder / 'tube360.xex').write_bytes(installed)
    subprocess.run([str(executable), str(kit / 'public'), str(folder)], check=True)
print('PASS test trust/version/failure isolation: production key unchanged, test key in all four XEX, failure hook only in failure candidate')
