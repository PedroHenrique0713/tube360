#!/usr/bin/env python3
"""Testa o worker real contra HTTP local, sem chave de produção, conta, YouTube ou console."""
import argparse
from functools import partial
from http.server import ThreadingHTTPServer
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(*args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)


def compile_probe(build, folder, header):
    link = shlex.split((build / 'CMakeFiles/tube360.dir/link.txt').read_text())
    libs = [x if x.startswith(('/', '-')) else str(build / x)
            for x in link if x.endswith('.a') or '.so' in x or x.startswith('-l')]
    flags = (build / 'CMakeFiles/tube360-update.dir/flags.make').read_text()
    includes = shlex.split(flags.split('CXX_INCLUDES = ', 1)[1].splitlines()[0])
    includes += ['-I' + str(ROOT / 'client/third_party/monocypher'),
                 '-I' + str(next((ROOT / '.work/src').glob('curl-xenon-*/include')))]
    objects = []
    for name in ('common', 'app', 'service'):
        output = folder / (name + '.o')
        run(link[0], '-std=c++20', '-O2', '-DTUBE360_UPDATE_TEST_CONFIG="' + str(header) + '"',
            *includes, '-c', ROOT / f'client/src/update/{name}.cpp', '-o', output)
        objects.append(output)
    binary = folder / 'service-probe'
    run(link[0], '-std=c++20', '-O2', *includes, '-DTUBE360_UPDATE_TEST_CONFIG="' + str(header) + '"',
        ROOT / 'tests/update/service.cpp', *objects, *libs, '-o', binary)
    # O renderer e os objetos de produção permanecem intactos; só este binário usa a chave descartável.
    run(link[0], build / 'CMakeFiles/tube360-main.dir/src/main.cpp.o', *objects, *libs,
        '-o', folder / 'tube360-ui')
    return binary


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, default=ROOT / 'client/build')
    p.add_argument('--output', type=Path, default=ROOT / '.work/update-service')
    a = p.parse_args()
    folder = a.output.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    key = folder / 'test-key.pem'
    sign = [sys.executable, str(ROOT / 'tools/sign-updates.py'), '--key', str(key)]
    if not key.exists():
        run(*sign, 'keygen')
    public = subprocess.check_output(['openssl', 'pkey', '-in', str(key), '-pubout', '-outform', 'DER'])[-32:].hex()
    kit = load('console_kit', ROOT / 'tests/update/console-kit.py')
    server = ThreadingHTTPServer(('127.0.0.1', 0), partial(kit.Handler, root=folder, delay=0.03))
    server.daemon_threads = True
    base = f'http://127.0.0.1:{server.server_port}/'
    header = folder / 'test-config.hpp'
    header.write_text('#define TUBE360_UPDATE_TEST_KEY "' + public + '"\n#define TUBE360_UPDATE_TEST_BASE "' + base + '"\n')
    binary = compile_probe(a.build.resolve(), folder, header)
    payload = folder / 'good.xex'
    payload.write_bytes(b'XEX2' + b'candidate' * (250*1024))
    run(*sign, 'manifest', payload, '--build', 6000, '--version', '0.3.2-test', '--url', base + 'good.xex',
        '--allow-test-url', '--notes-en', 'HTTP fixture.', '--notes-pt', 'Bancada HTTP.', '--output', folder / 'good.json')
    thread = threading.Thread(target=server.serve_forever)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='scenarios-', dir=folder) as run_folder:
            for scenario, mode in [('good', 'download'), ('good', 'cancel'), ('bad-hash', 'error'), ('bad-range', 'error'), ('redirect-out', 'error'),
                                   ('bad-signature', 'reject'), ('good', 'legacy'), ('good', 'corrupt')]:
                (folder / 'scenario.txt').write_text(scenario)
                target = Path(run_folder) / (scenario + '-' + mode)
                target.mkdir()
                if mode != 'legacy':
                    (target / 'tube360.xex').write_bytes(b'XEX2original')
                if mode == 'corrupt':
                    (target / 'update-app-state.json').write_text('broken journal')
                run(binary, target, mode, timeout=20)
                print('PASS HTTP service:', scenario, mode, flush=True)
        # A build pública continua sem a chave e sem a origem HTTP compiladas na bancada.
        production = (a.build / 'tube360').read_bytes()
        assert public.encode() not in production
        assert b'TUBE360-DISPOSABLE-UPDATE-TEST' not in production
        assert base.encode() not in production
        print('PASS production trust isolation; 8 worker HTTP scenarios', flush=True)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    # Fixture visual assinada com a mesma chave descartável. HTTP será servido por um processo separado no QA.
    run(sys.executable, ROOT / 'tests/update/ui-fixture.py', folder / 'ui', '--key', key,
        '--build', 6000, '--version', '0.3.2-f26-candidate')
    print('Visual binary:', folder / 'tube360-ui')
    print('Visual origin:', base)


if __name__ == '__main__':
    main()
