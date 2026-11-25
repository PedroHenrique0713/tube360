#!/usr/bin/env python3
"""Integra o runner ao XBDM falso por TCP real em loopback; nenhum Xbox é acessado."""
from contextlib import contextmanager
import argparse
from functools import partial
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import socket
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import urlsplit
from urllib.request import Request, urlopen
import zipfile

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / '.work/console-run-mock'
WORK.mkdir(parents=True, exist_ok=True)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def make_kit(folder):
    public = folder / 'public'
    public.mkdir(parents=True)
    private = folder / 'private'
    private.mkdir()
    key = private / 'disposable-ed25519.pem'
    sign = [sys.executable, str(ROOT / 'tools/sign-updates.py'), '--key', str(key)]
    subprocess.run(sign + ['keygen'], check=True, capture_output=True)
    pub = subprocess.check_output(['openssl', 'pkey', '-in', str(key), '-pubout', '-outform', 'DER'])[-32:].hex()
    common = b'XEX2\0TUBE360-DISPOSABLE-UPDATE-TEST\0TEST auto-download requested build=\0TEST boot healthy build=\0' + pub.encode()
    files = {'tube360.xex': common + b'0.3.2-f26-n', 'default.xex': b'XEX2launcher' + pub.encode(),
             'licenses/nested/proof.txt': b'fake license, not a real console package'}
    for weight in (300, 400, 600, 700):
        files[f'inter-{weight}.ttf'] = b'fake font for XBDM protocol test only'
    files['manifest.json'] = (json.dumps(dict(layout='launcher+app', test_only=True,
        files={name: digest(data) for name, data in files.items()}))+'\n').encode()
    install = folder / 'install'
    install.mkdir()
    with zipfile.ZipFile(install / 'fake-n.zip', 'w') as archive:
        for name, data in files.items():
            archive.writestr('Tube360 Teste/' + name, data)
    for label, build in [('good', 2602), ('fail', 2603)]:
        payload = common + ('0.3.2-f26-' + label).encode() + b'Z' * (1100*1024)
        if label == 'fail':
            payload += b'TEST boot confirmation intentionally withheld'
        target = public / (label + '.xex')
        target.write_bytes(payload)
        subprocess.run(sign + ['manifest', str(target), '--build', str(build), '--version', '0.3.2-f26-' + label,
                              '--url', 'http://127.0.0.1:8266/' + label + '.xex', '--allow-test-url',
                              '--notes-en', 'Local fake XBDM.', '--notes-pt', 'XBDM falso local.',
                              '--output', str(public / (label + '.json'))], check=True, capture_output=True)
    (public / 'scenario.txt').write_text('good\n')
    (folder / 'kit.json').write_text(json.dumps(dict(base='http://127.0.0.1:8266/', public_key=pub,
        build_n=2601, test_only=True, auto_file_trigger=True)))
    return files


class Model:
    def __init__(self, kit, installed, fault='', alias='', existing=False):
        self.kit, self.installed, self.fault, self.alias = kit, installed, fault, alias
        self.lock, self.stop = threading.RLock(), threading.Event()
        self.files = {self.norm(r'Usb0:\Aurora\Aurora.xex'): b'XEX2fake Aurora',
                      self.norm(r'HDD:\Games\Tube360\sentinel.txt'): b'DO NOT TOUCH NORMAL INSTALL'}
        self.dirs = {self.norm(r'Usb0:\Aurora'), self.norm(r'HDD:\Games'), self.norm(r'HDD:\Games\Tube360')}
        self.root = self.norm(r'HDD:\Games\Tube360 Teste')
        self.commands, self.runs, self.workers, self.errors = [], [], [], []
        self.title, self.generation, self.sequence = 'aurora', 0, 0
        if existing:
            self.dirs.update((self.root, self.root + '\\old'))
            self.files[self.root + '\\old\\old.log'] = b'old logs preserved before replacement\n'
            self.files[self.root + '\\old\\file.txt'] = b'old test data'

    @staticmethod
    def norm(path):
        return re.sub(r'^hdd1:', 'hdd:', path.rstrip('\\').casefold())

    def get(self, name):
        return self.files.get(self.root + '\\' + name)

    def put(self, name, data):
        self.files[self.root + '\\' + name] = data

    def state(self):
        return json.loads(self.get('update-app-state.json') or b'{}')

    def save(self, state):
        self.put('update-app-state.json', json.dumps(state).encode())

    def emit(self, source, text):
        line = ('update: ' if source == 'update' else 'launcher +1ms: ') + text
        with self.lock:
            if self.fault != 'udp-only':
                name = source + '.log'
                self.put(name, (self.get(name) or b'') + line.encode() + b'\n')
            target = self.get('net_log.txt')
            if target:
                host, port = target.decode().split()
                self.sequence += 1
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                    sock.sendto(struct.pack('!I', self.sequence)+line.encode(), (host, int(port)))

    def current_build(self):
        current = self.get('tube360.xex')
        for label, build in [('good', 2602), ('fail', 2603)]:
            if current == (self.kit / f'public/{label}.xex').read_bytes():
                return build
        return 2601

    def launch(self, path):
        with self.lock:
            self.generation += 1
            token = self.generation
            self.runs.append(path)
            if self.norm(path) == self.norm(r'Usb0:\Aurora\Aurora.xex'):
                self.title = 'aurora'
                return
            assert self.norm(path) == self.root + '\\default.xex', path
            self.title = 'test'
        worker = threading.Thread(target=self.boot, args=(token,), daemon=True)
        self.workers.append(worker)
        worker.start()

    def boot(self, token):
        try:
            with self.lock:
                if self.generation != token or self.stop.is_set():
                    return
                state = self.state()
                phase = state.get('phase')
                self.emit('launcher', 'inicio')
                if phase == 'staged':
                    self.put('tube360.previous.xex', self.get('tube360.xex'))
                    self.put('tube360.xex', self.files.pop(self.root + '\\tube360.next.xex'))
                    state['phase'] = 'trial'
                    self.save(state)
                    self.emit('launcher', 'journal gravacao fase=trial ok')
                elif phase in ('trial', 'rollback') and self.fault != 'no-rollback':
                    self.emit('launcher', 'rollback inicio')
                    if self.get('tube360.previous.xex'):
                        self.put('tube360.failed.xex', self.get('tube360.xex'))
                        self.put('tube360.xex', self.files.pop(self.root + '\\tube360.previous.xex'))
                    state['phase'] = 'rollback'
                    self.save(state)
                    self.emit('launcher', 'rollback fim: tube360.xex')
                build = self.current_build()
                if self.fault == 'wrong-payload' and build == 2602:
                    self.put('tube360.xex', self.installed['tube360.xex'])
                self.emit('update', f'TEST app started build={build} version=fake')
                # Modelo de boot saudável; não simula GPU, XAM nem os 3 s de renderer.
                if build == 2603:
                    self.emit('update', 'TEST boot confirmation intentionally withheld build=2603')
                elif state.get('phase') == 'trial':
                    state['phase'] = 'good'
                    self.save(state)
                    if self.fault != 'stale-confirm':
                        self.emit('update', 'boot confirmed build=2602')
                self.emit('update', f'TEST boot healthy build={build}')
                if self.fault == 'stale-confirm' and build == 2601:
                    self.emit('update', 'boot confirmed build=2602')
                auto = self.get('update-auto.txt') is not None
                highest = state.get('highest', build)
            if not auto:
                return
            base = 'http://127.0.0.1:8266/'
            with urlopen(base+'app.json', timeout=2) as response:
                raw = response.read()
            with urlopen(base+'app.json.sig', timeout=2) as response:
                signature = response.read()
            manifest = json.loads(raw)
            if manifest['build'] <= highest:
                return
            self.emit('update', f'TEST auto-download requested build={build}')
            data = bytearray()
            for start in range(0, manifest['size'], 524288):
                end = min(manifest['size']-1, start+524287)
                request = Request(base+urlsplit(manifest['url']).path.lstrip('/'), headers={'Range': f'bytes={start}-{end}'})
                with urlopen(request, timeout=2) as response:
                    assert response.status == 206
                    assert response.headers['Content-Range'] == f'bytes {start}-{end}/{manifest["size"]}'
                    data.extend(response.read())
            assert digest(data) == manifest['sha256']
            with self.lock:
                if self.generation != token or self.stop.is_set():
                    return
                self.put('tube360.next.xex', bytes(data))
                self.save(dict(phase='staged', highest=manifest['build'], bytes=raw.decode(), signature=signature.decode()))
                self.emit('update', f'staged build={manifest["build"]} sha256={manifest["sha256"]}')
        except Exception as error:
            with self.lock:
                if not self.stop.is_set() and self.generation == token and self.title == 'test':
                    self.errors.append(repr(error))
                    self.emit('update', 'MOCK ERROR ' + repr(error))


class Protocol(socketserver.StreamRequestHandler):
    def reply(self, data):
        self.connection.sendall(data)

    def handle(self):
        model = self.server.model
        self.reply(b'201- connected fake-local\r\n')
        while not model.stop.is_set():
            raw = self.rfile.readline()
            if not raw:
                break
            command = raw.rstrip(b'\r\n').decode('latin-1')
            if command == 'bye':
                break
            model.commands.append(command)
            match = re.search(r'(?:name|title)="([^"]+)"', command)
            path = match[1] if match else ''
            normalized = model.norm(path)
            if path.startswith('Hdd1:') and (model.alias == 'all' or model.alias == 'mkdir' and command.startswith('mkdir ')):
                self.reply(b'414- invalid drive alias\r\n')
                continue
            with model.lock:
                if command.startswith('dirlist '):
                    if normalized not in model.dirs:
                        self.reply(b'402- no such directory\r\n')
                        continue
                    found = []
                    for child in model.dirs | set(model.files):
                        if child.rsplit('\\', 1)[0] == normalized:
                            name = child.rsplit('\\', 1)[1]
                            size = len(model.files.get(child, b''))
                            found.append(f'name="{name}" sizehi=0x0 sizelo=0x{size:x}' + (' directory' if child in model.dirs else ''))
                    self.reply(('202- multiline follows\r\n'+'\r\n'.join(found)+ ('\r\n' if found else '')+'.\r\n').encode())
                elif command.startswith('mkdir '):
                    if model.fault == 'mkdir-413' and normalized == model.root:
                        self.reply(b'413- title still open\r\n')
                    elif normalized in model.dirs:
                        self.reply(b'410- already exists\r\n')
                    else:
                        assert normalized.rsplit('\\', 1)[0] in model.dirs
                        model.dirs.add(normalized)
                        self.reply(b'200- mkdir OK\r\n')
                elif command.startswith('sendfile '):
                    length = int(command.split('length=')[1], 16)
                    self.reply(b'204- send binary\r\n')
                    data = self.rfile.read(length)
                    assert len(data) == length
                    assert normalized.rsplit('\\', 1)[0] in model.dirs
                    model.files[normalized] = data
                    self.reply(b'200- file OK\r\n')
                elif command.startswith('getfile '):
                    if normalized not in model.files:
                        self.reply(b'402- no such file\r\n')
                        continue
                    data = model.files[normalized]
                    if model.fault == 'log-locked' and model.title == 'test' and normalized.endswith('.log'):
                        self.reply(b'413- log currently locked\r\n')
                        continue
                    self.reply(b'203- binary follows\r\n' + struct.pack('<I', len(data)))
                    trickle = model.fault == 'trickle' and model.title == 'test'
                    if not trickle:
                        self.reply(data)
                elif command.startswith('delete '):
                    assert normalized == model.root or normalized.startswith(model.root+'\\'), command
                    if model.fault == 'delete-denied' and normalized.endswith('proof.txt'):
                        self.reply(b'414- access denied fake\r\n')
                    elif command.endswith(' dir'):
                        assert not any(p.startswith(normalized+'\\') for p in model.dirs | set(model.files))
                        model.dirs.remove(normalized)
                        self.reply(b'200- dir deleted\r\n')
                    else:
                        del model.files[normalized]
                        self.reply(b'200- file deleted\r\n')
                elif command.startswith('magicboot '):
                    model.launch(path)
                    return  # Troca de título derruba a conexão, sem ACK.
                else:
                    raise AssertionError('comando inesperado: ' + command)
            if command.startswith('getfile ') and trickle:
                try:
                    for byte in data:
                        if model.stop.wait(0.03) or model.title != 'test':
                            break
                        self.reply(bytes([byte]))
                except OSError:
                    return


class FakeServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def handle_error(self, request, client_address):
        error = sys.exc_info()[1]
        if not isinstance(error, (BrokenPipeError, ConnectionResetError)):
            self.model.errors.append(repr(error))


@contextmanager
def xbdm_model(kit, installed, **kwargs):
    model = Model(kit, installed, **kwargs)
    server = FakeServer(('127.0.0.1', 0), Protocol)
    server.model = model
    thread = threading.Thread(target=server.serve_forever)
    thread.start()
    try:
        yield model, server.server_address[1]
    finally:
        model.stop.set()
        server.shutdown()
        server.server_close()
        thread.join()
        for worker in model.workers:
            worker.join(timeout=3)


def free_udp():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def check_case(kit, installed, name, *, fault='', alias='', existing=False, expected_failure='', occupied=False, interrupt=False, real_kit=False):
    output = WORK / (name + '-' + str(time.time_ns()))
    with xbdm_model(kit, installed, fault=fault, alias=alias, existing=existing) as (model, port):
        command = [sys.executable, str(ROOT / 'tests/update/console-run.py'), '--host', '127.0.0.1',
                   '--xbdm-port', str(port), '--directory', str(kit), '--log-host', '127.0.0.1',
                   '--udp-port', str(free_udp()), '--step-timeout', '30' if real_kit else '3', '--io-timeout', '2' if real_kit else '0.3',
                   '--poll', '0.03', '--install-timeout', '30' if real_kit else '15', '--cleanup-timeout', '8', '--output', str(output)]
        blocker = None
        if occupied:
            blocker = socket.socket()
            blocker.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            blocker.bind(('127.0.0.1', 8266))
            blocker.listen()
        start = time.monotonic()
        try:
            if interrupt:
                with (WORK / (name + '-stdout.log')).open('w') as handle:
                    child = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT)
                    until = time.monotonic() + 10
                    while model.title != 'test' and child.poll() is None and time.monotonic() < until:
                        time.sleep(0.02)
                    child.send_signal(signal.SIGINT)
                    result_code = child.wait(timeout=12)
            else:
                result = subprocess.run(command, capture_output=True, text=True, timeout=90 if real_kit else 25)
                result_code = result.returncode
                (WORK / (name + '-stdout.log')).write_text(result.stdout + result.stderr)
        finally:
            if blocker:
                blocker.close()
        report = json.loads((output / 'report.json').read_text())
        assert result_code == (1 if expected_failure or interrupt else 0), (name, (output / 'runner.log').read_text())
        assert report['success'] == (result_code == 0)
        assert not model.errors, (model.errors, model.commands[-8:])
        if expected_failure:
            assert any(s['name'] == expected_failure and not s['passed'] for s in report['steps']), report
        assert all(c['stopped'] for c in report['children'])
        for child in report['children']:
            try:
                os.kill(child['pid'], 0)
                raise AssertionError('child PID left running: ' + str(child['pid']))
            except ProcessLookupError:
                pass
        assert model.files[model.norm(r'HDD:\Games\Tube360\sentinel.txt')] == b'DO NOT TOUCH NORMAL INSTALL'
        if occupied:
            assert not model.runs and not any(c.startswith(('sendfile ', 'mkdir ', 'delete ')) for c in model.commands)
        else:
            assert model.norm(model.runs[-1]) == model.norm(r'Usb0:\Aurora\Aurora.xex')
            if fault != 'delete-denied':
                assert not any(p == model.root or p.startswith(model.root+'\\') for p in model.dirs | set(model.files))
            else:
                assert model.root in model.dirs and not report['folder_removed']
        if result_code == 0:
            assert len([s for s in report['steps'] if s['passed']]) == 11
            for phase in ('N-staged', 'good-confirmed', 'fail-staged', 'fail-unconfirmed', 'rollback', 'final'):
                assert (output / phase / 'update-app-state.json').exists()
            if existing:
                assert (output / 'preexisting/old/old.log').read_bytes().startswith(b'old logs')
            if fault in ('udp-only', 'log-locked'):
                assert any('UDP:' in s.get('proof', '') for s in report['steps'])
            if alias:
                assert report['remote_root'].startswith('HDD:')
        if fault == 'trickle':
            assert time.monotonic()-start < 12, 'socket deadlines must bound continuous trickle'
        print('PASS mock', name, 'exit', result_code, 'seconds', round(time.monotonic()-start, 2), 'evidence', output, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--real-kit', type=Path, help='Usa os bytes do kit Xbox real no modelo local, sem executá-los')
    args = parser.parse_args()
    if args.real_kit:
        kit = args.real_kit.resolve()
        with zipfile.ZipFile(next((kit / 'install').glob('*.zip'))) as archive:
            installed = {name.split('/', 1)[1]: archive.read(name) for name in archive.namelist()}
        check_case(kit, installed, 'real-kit-happy', real_kit=True)
        print('Real kit byte transfers passed against fake XBDM on loopback; no XEX executed.')
        return
    with tempfile.TemporaryDirectory(prefix='kit-', dir=WORK) as temporary:
        kit = Path(temporary)
        installed = make_kit(kit)
        for name, options in [
            ('happy-hdd1', {}),
            ('hdd-414-existing', dict(alias='all', existing=True)),
            ('hdd-414-mkdir', dict(alias='mkdir')),
            ('udp-only', dict(fault='udp-only')),
            ('log-sharing-lock-UDP', dict(fault='log-locked')),
            ('stale-confirm-rejected', dict(fault='stale-confirm', expected_failure='good-confirmed')),
            ('wrong-XEX-rejected', dict(fault='wrong-payload', expected_failure='good-confirmed')),
            ('missing-rollback', dict(fault='no-rollback', expected_failure='rollback')),
            ('mkdir-413-not-success', dict(fault='mkdir-413', expected_failure='instalar-N')),
            ('socket-trickle-timeout', dict(fault='trickle', expected_failure='N-staged')),
            ('delete-failure-visible', dict(fault='delete-denied', expected_failure='arquivar-limpar-Aurora')),
            ('port-busy-no-console-write', dict(occupied=True, expected_failure='servidor-e-UDP')),
            ('interrupt-cleans-up', dict(interrupt=True)),
        ]:
            check_case(kit, installed, name, **options)
    print('13 XBDM TCP mock cases passed; all connections used loopback, no physical console, GPU or XAM tested.')


if __name__ == '__main__':
    main()
