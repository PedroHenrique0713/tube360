#!/usr/bin/env python3
"""Receptores TLS Cast locais: nenhum multicast, conta, volume ou reprodução real."""
import contextlib
import http.server
import json
import os
from pathlib import Path
import shutil
import socket
import socketserver
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / 'client/build/nest-discovery-test'
NETWORK = '127.77.24.1/255.255.255.0'


def varint(n):
    out = bytearray()
    while n >= 128:
        out.append((n & 127) | 128)
        n >>= 7
    out.append(n)
    return bytes(out)


def frame(sender, destination, namespace, message):
    out = b'\x08\0'
    for key, value in ((2, sender), (3, destination), (4, namespace), (6, json.dumps(message))):
        if key == 6:
            out += b'\x28\0'
        value = value.encode()
        out += varint(key * 8 + 2) + varint(len(value)) + value
    return struct.pack('!I', len(out)) + out


def decode(raw):
    fields = {}
    at = 0

    def number():
        nonlocal at
        value, shift = 0, 0
        while True:
            b = raw[at]
            at += 1
            value |= (b & 127) << shift
            if b < 128:
                return value
            shift += 7
    while at < len(raw):
        tag = number()
        if tag & 7 == 0:
            fields[tag >> 3] = number()
        elif tag & 7 == 2:
            size = number()
            fields[tag >> 3] = raw[at:at + size].decode()
            at += size
        else:
            raise AssertionError('invalid protobuf wire type')
    assert fields[1] == 0 and fields[5] == 0
    return fields


def exact(sock, size):
    out = b''
    while len(out) < size:
        b = sock.recv(size - len(out))
        if not b:
            raise EOFError()
        out += b
    return out


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


@contextlib.contextmanager
def receiver(folder, host, name='Sala', uid='11111111-1111-1111-1111-111111111111', model='Google Nest Mini', valid=True, stall=False, eureka=True, tls_info=False):
    commands = []
    failures = []
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(folder / 'cert.pem', folder / 'key.pem')

    class Cast(socketserver.BaseRequestHandler):
        def handle(self):
            try:
                self.request.settimeout(2)
                if stall:
                    time.sleep(1)
                    return
                with context.wrap_socket(self.request, server_side=True) as conn:
                    connected = False
                    while True:
                        size, = struct.unpack('!I', exact(conn, 4))
                        assert 0 < size < 65536
                        f = decode(exact(conn, size))
                        message = json.loads(f[6])
                        kind = message['type']
                        commands.append(kind)
                        assert f[3] == 'receiver-0'
                        if kind == 'CONNECT':
                            assert f[4] == 'urn:x-cast:com.google.cast.tp.connection'
                            connected = True
                        elif kind == 'GET_STATUS':
                            assert connected and f[4] == 'urn:x-cast:com.google.cast.receiver'
                            # RECEIVER_STATUS real ocioso pode omitir applications.
                            reply = {'type': 'RECEIVER_STATUS' if valid else 'NOT_CAST', 'requestId': message['requestId'], 'status': {'volume': {'level': .13, 'muted': False}}}
                            # Quadro partido: exercita o leitor de produção, sem parser paralelo no teste.
                            raw = frame('receiver-0', f[2], f[4], reply)
                            conn.sendall(raw[:3])
                            conn.sendall(raw[3:])
                        elif kind != 'PONG':
                            raise AssertionError(f'non-read-only command: {kind}')
            except (EOFError, ConnectionError, TimeoutError, ssl.SSLError, OSError):
                pass
            except Exception as error:
                failures.append(repr(error))

    class Info(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if not self.path.startswith('/setup/eureka_info'):
                self.send_error(404)
                return
            payload = json.dumps({'name': name, 'device_info': {'model_name': model, 'ssdp_udn': uid}}).encode()
            self.send_response(200)
            self.send_header('Content-Length', str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        def log_message(self, *args):
            pass

    cast = Server((host, 8009), Cast)
    info = http.server.ThreadingHTTPServer((host, 8443 if tls_info else 8008), Info) if eureka else None
    if info and tls_info:
        info.socket = context.wrap_socket(info.socket, server_side=True)
    servers = [cast] + ([info] if info else [])
    threads = [threading.Thread(target=s.serve_forever, daemon=True) for s in servers]
    for t in threads:
        t.start()
    try:
        yield commands
    finally:
        for s in servers:
            s.shutdown()
            s.server_close()
        for t in threads:
            t.join()
        assert not failures, failures
        assert set(commands) <= {'CONNECT', 'GET_STATUS', 'PONG'}, commands


class DiscoveryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='tube360-nest-')
        cls.folder = Path(cls.temp.name)
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=Cast fixture', '-keyout', str(cls.folder / 'key.pem'), '-out', str(cls.folder / 'cert.pem')], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_probe(self, *args, network=NETWORK):
        result = subprocess.run([str(BINARY), *map(str, args)], check=True, capture_output=True, text=True, timeout=12, env={**os.environ, 'XT_NEST_NETWORK': network})
        return json.loads(result.stdout.splitlines()[-1])

    def test_multiple_and_false_positive(self):
        with receiver(self.folder, '127.77.24.2'), receiver(self.folder, '127.77.24.3', name='Cozinha', uid='22222222-2222-2222-2222-222222222222'), receiver(self.folder, '127.77.24.4', valid=False):
            result = self.run_probe('scan')
        self.assertEqual(result['state'], 2)
        self.assertEqual([d['name'] for d in result['devices']], ['Cozinha', 'Sala'])
        self.assertEqual(result['devices'][1]['uuid'], '1' * 32)
        self.assertEqual(result['devices'][1]['model'], 'Google Nest Mini')
        self.assertLess(result['ms'], 5000)
        print('multiple devices:', result['ms'], 'ms')

    def test_ip_changed_and_old_ip_reused(self):
        config = self.folder / 'moved'
        config.mkdir(exist_ok=True)
        (config / 'nest.txt').write_text('127.77.24.3 8009\nSala\nuuid=' + '1' * 32 + '\n')
        with receiver(self.folder, '127.77.24.2'), receiver(self.folder, '127.77.24.3', name='Sala', uid='22222222-2222-2222-2222-222222222222'):
            result = self.run_probe('recover', config)
        self.assertEqual(result['host'], '127.77.24.2')
        self.assertEqual(result['uuid'], '1' * 32)
        self.assertTrue((config / 'nest.txt').read_text().startswith('127.77.24.2 8009\n'))
        print('UUID recovery:', result['ms'], 'ms')

    def test_legacy_reachable_and_name_recovery(self):
        config = self.folder / 'legacy'
        config.mkdir(exist_ok=True)
        with receiver(self.folder, '127.77.24.2'):
            for host in ('127.77.24.2', '127.77.24.9'):
                (config / 'nest.txt').write_text(host + ' 8009\nSala\n')
                result = self.run_probe('recover', config)
                self.assertEqual(result['host'], '127.77.24.2')
                self.assertIn('uuid=' + '1' * 32, (config / 'nest.txt').read_text())

    def test_duplicate_names_do_not_guess(self):
        config = self.folder / 'ambiguous'
        config.mkdir(exist_ok=True)
        (config / 'nest.txt').write_text('127.77.24.9\nSala\n')
        with receiver(self.folder, '127.77.24.2'), receiver(self.folder, '127.77.24.3', uid='22222222-2222-2222-2222-222222222222'):
            result = self.run_probe('recover', config)
        self.assertEqual(result['host'], '127.77.24.9')

    def test_no_devices(self):
        result = self.run_probe('scan', network='127.77.25.1/255.255.255.0')
        self.assertEqual(result['state'], 2)
        self.assertEqual(result['devices'], [])
        print('empty /24:', result['ms'], 'ms')

    def test_cancel_stalled_tls(self):
        with receiver(self.folder, '127.77.24.2', stall=True):
            result = self.run_probe('cancel')
        self.assertEqual(result['state'], 3)
        self.assertLess(result['ms'], 900)
        print('cancel TLS:', result['ms'], 'ms')

    def test_cast_without_eureka(self):
        with receiver(self.folder, '127.77.24.2', eureka=False):
            result = self.run_probe('scan')
        self.assertEqual(len(result['devices']), 1)
        self.assertEqual(result['devices'][0]['model'], 'Google Cast')
        self.assertEqual(result['devices'][0]['uuid'], '')

    def test_modern_firmware_https_metadata(self):
        with receiver(self.folder, '127.77.24.2', tls_info=True):
            result = self.run_probe('scan')
        self.assertEqual(result['devices'][0]['uuid'], '1' * 32)
        self.assertEqual(result['devices'][0]['name'], 'Sala')
        self.assertEqual(result['devices'][0]['model'], 'Google Nest Mini')

    def test_choose_none_and_manual_search_during_startup(self):
        config = self.folder / 'lifecycle'
        config.mkdir(exist_ok=True)
        (config / 'nest.txt').write_text('127.77.24.2 8009\nSala\n')
        with receiver(self.folder, '127.77.24.2'), receiver(self.folder, '127.77.24.3', name='Cozinha', uid='22222222-2222-2222-2222-222222222222'):
            subprocess.run([str(BINARY), 'lifecycle', str(config)], check=True, timeout=9, env={**os.environ, 'XT_NEST_NETWORK': NETWORK})

    def test_contracts(self):
        subprocess.run([str(BINARY), 'contracts', str(self.folder)], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main(verbosity=2)
