#!/usr/bin/env python3
"""Valida o kit Xbox real: arquivos, confiança, assinaturas, stage e reserva no PC."""
import argparse
from functools import partial
from http.server import ThreadingHTTPServer
import importlib.util
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import threading
from urllib.error import HTTPError
from urllib.request import Request, urlopen
import zipfile
ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--directory', type=Path, default=ROOT / '.work/update-console')
a = p.parse_args()
kit = a.directory.resolve()
metadata = json.loads((kit / 'kit.json').read_text())
assert metadata['auto_file_trigger'] is True
archive = next((kit / 'install').glob('*.zip'))
subprocess.run([sys.executable, str(ROOT / 'tests/update/package-test.py'), str(archive), '--test'], check=True)
public_key = metadata['public_key'].encode()
private_path = kit / 'private/disposable-ed25519.pem'
private_pem = private_path.read_bytes()
private_seed = subprocess.check_output(['openssl', 'pkey', '-in', str(private_path), '-outform', 'DER'])[-32:]
with zipfile.ZipFile(archive) as zipped:
    installed = zipped.read('Tube360 Teste/tube360.xex')
    launcher = zipped.read('Tube360 Teste/default.xex')
for binary in (installed, launcher, *(f.read_bytes() for f in (kit / 'public').glob('*.xex'))):
    # O parser PEM do mbedTLS contém o rótulo genérico; a prova procura a chave real da bancada.
    assert public_key in binary and private_pem not in binary and private_seed not in binary
for binary in (installed, *(f.read_bytes() for f in (kit / 'public').glob('*.xex'))):
    assert b'update-auto.txt' in binary and b'TEST auto-download requested build=' in binary
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
spec = importlib.util.spec_from_file_location('console_kit', ROOT / 'tests/update/console-kit.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
server = ThreadingHTTPServer(('127.0.0.1', 0), partial(module.Handler, root=kit / 'public'))
thread = threading.Thread(target=server.serve_forever)
thread.start()
base = f'http://127.0.0.1:{server.server_port}'
try:
    for label, expected_build in [('good', metadata['build_n'] + 1), ('fail', metadata['build_n'] + 2)]:
        (kit / 'public/scenario.txt').write_text(label + '\n')
        with urlopen(base + '/app.json', timeout=3) as response:
            manifest = json.load(response)
        assert manifest['build'] == expected_build
        with urlopen(base + '/app.json.sig', timeout=3) as response:
            assert len(response.read()) == 129
        request = Request(base + '/' + label + '.xex', headers={'Range': 'bytes=0-524287'})
        with urlopen(request, timeout=3) as response:
            assert response.status == 206
            assert response.headers['Content-Range'] == f'bytes 0-524287/{manifest["size"]}'
            assert response.read() == (kit / f'public/{label}.xex').read_bytes()[:524288]
    try:
        urlopen(base + '/../private/disposable-ed25519.pem', timeout=3)
        raise AssertionError('private key route must not be served')
    except HTTPError as error:
        assert error.code == 404
finally:
    server.shutdown()
    server.server_close()
    thread.join()
    (kit / 'public/scenario.txt').write_text('good\n')
print('PASS actual kit HTTP: good/fail channels, signatures, exact 512 KiB Range and private directory denied')
