#!/usr/bin/env python3
"""Bancada F26: builds Xbox descartáveis, manifestos assinados e HTTP local com Range."""
import argparse
from functools import partial
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import ipaddress
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
DEFAULT = ROOT / '.work/update-console'


def run(*args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, cwd=ROOT, **kwargs)


def configure(build, flags, number, version):
    run('cmake', '-S', ROOT / 'client/x360', '-B', build,
        '-DCMAKE_CXX_FLAGS=' + flags, '-DTUBE360_BUILD_ID=' + str(number),
        '-DTUBE360_VERSION=' + version)


def build_kit(args):
    directory = args.directory.resolve()
    if directory.exists():
        raise ValueError('Use uma pasta nova: a chave e os resultados anteriores são preservados.')
    ipaddress.IPv4Address(args.host)
    if not 1024 <= args.port <= 65535:
        raise ValueError('Porta deve estar entre 1024 e 65535')
    build = ROOT / 'client/build-x360'
    if not (build / 'CMakeCache.txt').exists():
        raise ValueError('Compile primeiro tools/build-xbox.sh com o toolchain do console.')
    private = directory / 'private'
    private.mkdir(parents=True, mode=0o700)
    public = directory / 'public'
    public.mkdir()
    key = private / 'disposable-ed25519.pem'
    run(sys.executable, ROOT / 'tools/sign-updates.py', '--key', key, 'keygen')
    pub = subprocess.check_output(['openssl', 'pkey', '-in', str(key), '-pubout', '-outform', 'DER'])[-32:].hex()
    config = private / 'test-config.hpp'
    base = f'http://{args.host}:{args.port}/'
    header = '#pragma once\n#define TUBE360_UPDATE_TEST_KEY "' + pub + '"\n#define TUBE360_UPDATE_TEST_BASE "' + base + '"\n'
    curl_include = next((ROOT / '.work/src').glob('curl-xenon-*/include'))
    flags = '-mcpu=cell -fno-pic -DTUBE360_UPDATE_TEST_CONFIG=' + shlex.quote('"' + str(config) + '"') + ' -I' + shlex.quote(str(curl_include))
    spec = importlib.util.spec_from_file_location('package_xbox', ROOT / 'tools/package-xbox.py')
    pack = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pack)
    cache = (build / 'CMakeCache.txt').read_text()
    title = re.search(r'^APP_NAME:[^=]+=(.+)$', cache, re.M)
    title = title[1] if title else 'Tube360'
    lock = Path.home() / '.cache/tube360-frota-xbox.lock'
    for offset, label in enumerate(('n', 'good', 'fail')):
        config.write_text(header + ('#define TUBE360_UPDATE_TEST_FAIL_BOOT 1\n' if label == 'fail' else ''))
        number = args.build_id + offset
        version = '0.3.2-f26-' + label
        configure(build, flags, number, version)
        run('flock', lock, 'systemd-run', '--user', '--scope', '-q', '-p', 'MemoryMax=6G', '-p', 'MemorySwapMax=1G',
            'cmake', '--build', build, '-j' + os.environ.get('JOBS', '2'))
        if label == 'n':
            install = directory / 'install'
            run(sys.executable, ROOT / 'tools/package-xbox.py', '--version', version, '--test-package',
                '--output-dir', install)
        else:
            executable, _, _ = pack.clean_xex(build, ROOT / '.work/native-display/xsdk/tools/elf2xex.py', title)
            target = public / (label + '.xex')
            target.write_bytes(executable)
            run(sys.executable, ROOT / 'tools/sign-updates.py', '--key', key, 'manifest', target,
                '--build', number, '--version', version, '--url', base + label + '.xex', '--allow-test-url',
                '--notes-en', 'F26 local test. ' + ('Boot confirmation deliberately disabled.' if label == 'fail' else 'Updated app; reopen to install.'),
                '--notes-pt', 'Teste local F26. ' + ('Confirmação de boot desativada de propósito.' if label == 'fail' else 'App atualizado; reabra para instalar.'),
                '--output', public / (label + '.json'))
    (public / 'scenario.txt').write_text('good\n')
    (directory / 'kit.json').write_text(json.dumps(dict(base=base, public_key=pub, build_n=args.build_id,
        versions=['0.3.2-f26-n', '0.3.2-f26-good', '0.3.2-f26-fail'], test_only=True,
        auto_file_trigger=True), indent=2) + '\n')
    # A chave fica somente em private/, fora do ZIP e da raiz servida.
    print('Kit pronto:', directory, flush=True)
    print('Servidor: python3 tests/update/console-kit.py serve --directory ' + shlex.quote(str(directory)), flush=True)


class Handler(BaseHTTPRequestHandler):
    def __init__(self, *args, root, delay=0, **kwargs):
        self.root, self.delay = root, delay
        super().__init__(*args, **kwargs)

    def do_GET(self):
        scenario = (self.root / 'scenario.txt').read_text().strip()
        label = 'fail' if scenario == 'fail' else 'good'
        table = {'/app.json': label + '.json', '/app.json.sig': label + '.json.sig',
                 '/good.xex': 'good.xex', '/fail.xex': 'fail.xex'}
        if self.path not in table:
            self.send_error(404)
            return
        data = (self.root / table[self.path]).read_bytes()
        if scenario == 'redirect-out' and self.path.endswith('.xex'):
            self.send_response(302)
            self.send_header('Location', 'http://127.0.0.1:9/never-request-this')
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        if scenario == 'bad-signature' and self.path == '/app.json.sig':
            data = b'0' * 128 + b'\n'
        if scenario == 'bad-hash' and self.path.endswith('.xex'):
            data = data[:4] + bytes([data[4] ^ 1]) + data[5:]
        first, last = 0, len(data) - 1
        ranged = self.headers.get('Range')
        if ranged:
            match = re.fullmatch(r'bytes=(\d+)-(\d+)', ranged)
            if not match or not 0 <= int(match[1]) <= int(match[2]) < len(data):
                self.send_error(416)
                return
            first, last = map(int, match.groups())
        self.send_response(206 if ranged else 200)
        self.send_header('Content-Length', str(last - first + 1))
        self.send_header('Cache-Control', 'no-store')
        if ranged:
            self.send_header('Content-Range', f'bytes {first}-{last}/{len(data) + (1 if scenario == "bad-range" else 0)}')
        self.end_headers()
        try:
            for at in range(first, last + 1, 32768):
                self.wfile.write(data[at:min(last + 1, at + 32768)])
                self.wfile.flush()
                if ranged and self.delay:
                    time.sleep(self.delay)
        except (BrokenPipeError, ConnectionResetError):
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('build', 'serve', 'select'))
    parser.add_argument('--directory', type=Path, default=DEFAULT)
    parser.add_argument('--host', default='192.168.101.27', help='Origem compilada no teste')
    parser.add_argument('--port', type=int, default=8266)
    parser.add_argument('--build-id', type=int, default=2601)
    parser.add_argument('--scenario', choices=('good', 'fail', 'bad-hash', 'bad-signature', 'bad-range', 'redirect-out'), default='good')
    parser.add_argument('--chunk-delay', type=float, default=0.02)
    args = parser.parse_args()
    if args.command == 'build':
        build_kit(args)
    elif args.command == 'select':
        (args.directory / 'public/scenario.txt').write_text(args.scenario + '\n')
        print('Cenário:', args.scenario)
    else:
        kit = json.loads((args.directory / 'kit.json').read_text())
        from urllib.parse import urlsplit
        port = urlsplit(kit['base']).port
        server = ThreadingHTTPServer(('0.0.0.0', port), partial(Handler, root=args.directory / 'public', delay=args.chunk_delay))
        print('HTTP local descartável:', kit['base'], 'Ctrl+C encerra. Chave privada não é servida.', flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()


if __name__ == '__main__':
    main()
