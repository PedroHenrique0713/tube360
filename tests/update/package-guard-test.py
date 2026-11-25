#!/usr/bin/env python3
"""Calibra os dois gates com pacote público, bancada real e mutantes com hashes válidos.

Requer dependências locais do empacotador (como package-xbox.py). Todos os artefatos
de entrada são só leitura; builds/pacotes mutados ficam em diretório temporário.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('public_zip', type=Path)
p.add_argument('benchmark_build', type=Path)
p.add_argument('benchmark_zip', type=Path)
a = p.parse_args()
checks = 0


def run(label, command, reject=''):
    global checks
    result = subprocess.run([sys.executable, *map(str, command)], capture_output=True, text=True)
    output = result.stdout + result.stderr
    if (result.returncode != 0) != bool(reject) or (reject and reject not in output):
        raise AssertionError(f'{label}: exit={result.returncode}, expected rejection={reject!r}\n{output}')
    checks += 1
    print(f'PASS {label}: ' + (f'refused ({reject})' if reject else 'accepted'), flush=True)


def verify(path, **kwargs):
    run('archive ' + kwargs.pop('label'), [ROOT/'tests/update/package-test.py', path], **kwargs)


with zipfile.ZipFile(a.public_zip) as archive:
    files = {name.split('/', 1)[1]: archive.read(name) for name in archive.namelist()}
version = json.loads(files['manifest.json'])['version']
verify(a.public_zip.resolve(), label='real public')
with tempfile.TemporaryDirectory(prefix='tube360-package-guard-') as temporary:
    base = Path(temporary)
    build = base/'build'
    build.mkdir()
    output = base/'output'

    def package(directory, label, reject=''):
        run('packager ' + label, [ROOT/'tools/package-xbox.py', '--version', version,
            '--build-dir', directory, '--output-dir', output], reject=reject)

    package(a.benchmark_build.resolve(), 'real F40', 'TUBE360_F40_BENCH')
    verify(a.benchmark_zip.resolve(), label='real F40', reject='benchmark/mute marker')
    cache = (a.benchmark_build/'CMakeCache.txt').read_text()
    for option in ('TUBE360_F33_BENCH', 'TUBE360_F40_BENCH'):
        cache, count = re.subn(rf'^{option}:[^=\n]+=.*$', f'{option}:BOOL=OFF', cache, flags=re.M)
        assert count == 1, (option, count)

    def stage(app, launcher, config=cache):
        (build/'CMakeCache.txt').write_text(config)
        (build/'tube360.xex').write_bytes(app)
        (build/'tube360-launcher.xex').write_bytes(launcher)

    app, launcher = files['tube360.xex'], files['default.xex']
    with zipfile.ZipFile(a.benchmark_zip) as archive:
        benchmark = {name.split('/', 1)[1]: archive.read(name) for name in archive.namelist()}
    stage(benchmark['tube360.xex'], benchmark['default.xex'])
    package(build, 'real F40/stale OFF cache', 'benchmark/mute marker')
    stage(app, launcher)
    package(build, 'public XEX/OFF')
    verify(output/f'tube360-v{version}-xbox360.zip', label='repackaged public')
    for option in ('TUBE360_F33_BENCH', 'TUBE360_F40_BENCH'):
        stage(app, launcher, cache.replace(f'{option}:BOOL=OFF', f'{option}:BOOL=ON'))
        package(build, option, option)

    mutant = base/'mutant.zip'

    def mutated_archive(name, data):
        changed = dict(files)
        changed[name] = data
        manifest = json.loads(changed['manifest.json'])
        manifest['files'][name] = hashlib.sha256(data).hexdigest()
        field = 'packaged_xex_sha256' if name == 'tube360.xex' else 'packaged_launcher_sha256'
        manifest[field] = hashlib.sha256(data).hexdigest()
        changed['manifest.json'] = json.dumps(manifest).encode()
        with zipfile.ZipFile(mutant, 'w') as archive:
            for member, content in changed.items():
                archive.writestr('Tube360/' + member, content)
        (base/'tube360.xex').write_bytes(changed['tube360.xex'])

    for name in ('tube360.xex', 'default.xex'):
        for marker in (b'f40-playback.txt', b'f33-playback.txt', b'F40 APP', b'mudo.txt', b'bancada: mudo'):
            data = files[name] + b'\0' + marker
            stage(data if name == 'tube360.xex' else app, data if name == 'default.xex' else launcher)
            package(build, name + '/' + marker.decode(), 'benchmark/mute marker')
            mutated_archive(name, data)
            verify(mutant, label=name + '/' + marker.decode(), reject='benchmark/mute marker')

    key = re.search(rb'publicKeyHex = "([0-9a-f]{64})"',
                    (ROOT/'client/src/update/public_key.hpp').read_bytes())[1]
    for name in ('tube360.xex', 'default.xex'):
        assert key in files[name]
        data = files[name].replace(key, b'0'*64)
        stage(data if name == 'tube360.xex' else app, data if name == 'default.xex' else launcher)
        package(build, name + '/wrong key', 'production public key')
        mutated_archive(name, data)
        verify(mutant, label=name + '/wrong key', reject='production public key')

print(f'PASS package guards: {checks} checks; public accepted, real F40/cache/markers/trust rejected')
