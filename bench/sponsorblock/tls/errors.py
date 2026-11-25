#!/usr/bin/env python3
"""Falhas TLS reais em loopback, com a sonda ligada ao config.h do Xbox."""
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import re

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='tube360-tls-errors-') as directory:
    root = Path(directory)
    cert, key = root/'cert.pem', root/'key.pem'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:secp384r1',
                    '-nodes', '-subj', '/CN=127.0.0.1', '-addext', 'subjectAltName=IP:127.0.0.1',
                    '-days', '1', '-keyout', str(key), '-out', str(cert)], check=True, capture_output=True)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    for name in ('untrusted-p384', 'plaintext-on-tls-port', 'stalled-handshake'):
        errors = []
        release = threading.Event()
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            listener.settimeout(10)
            def serve():
                try:
                    raw, _ = listener.accept()
                    with raw:
                        raw.settimeout(5)
                        if name == 'untrusted-p384':
                            try:
                                with context.wrap_socket(raw, server_side=True) as stream:
                                    stream.recv(1024)
                            except ssl.SSLError:
                                pass
                        elif name == 'plaintext-on-tls-port':
                            raw.recv(4096)
                            raw.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok')
                        else:
                            raw.recv(4096)
                            release.wait(8)
                except Exception as error:
                    errors.append(str(error))
            thread = threading.Thread(target=serve)
            thread.start()
            result = subprocess.run([binary, f'https://127.0.0.1:{listener.getsockname()[1]}/'],
                                    capture_output=True, text=True, timeout=15)
            release.set()
            thread.join(10)
            assert not thread.is_alive() and not errors, errors
            assert result.returncode == 1, result.stdout + result.stderr
            if name == 'stalled-handshake':
                assert 'failure=network' in result.stdout and 'curl=28' in result.stdout, result.stdout
                elapsed=float(re.search(r'seconds=([\d.]+)',result.stdout)[1])
                assert 2.0<=elapsed<4.5, result.stdout
            elif name == 'untrusted-p384':
                assert 'failure=tls' in result.stdout, result.stdout
                assert 'curl=60' in result.stdout and 'mbedtls=-0x2700' in result.stdout, result.stdout
            else:
                assert 'failure=tls' in result.stdout and 'curl=35' in result.stdout and 'mbedtls=-0x0000' not in result.stdout, result.stdout
            print('PASS', name, flush=True)
            print(result.stdout, flush=True)
