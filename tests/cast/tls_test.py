#!/usr/bin/env python3
"""O leitor HTTPS de produção, com raiz sintética exclusiva e TLS em loopback."""
import http.server
from pathlib import Path
import shlex
import ssl
import subprocess
import sys
import threading
import time

root = Path(__file__).resolve().parents[2]
folder = Path(sys.argv[1]).resolve()
folder.mkdir(parents=True, exist_ok=True)
subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
    '-subj', '/CN=127.0.0.1', '-addext', 'subjectAltName=IP:127.0.0.1',
    '-keyout', str(folder/'key.pem'), '-out', str(folder/'cert.pem')], check=True,
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
(folder/'roots.hpp').write_text('static const char ytRoots[] = R"CA('+ (folder/'cert.pem').read_text()+')CA";\n')
objects = []
for source in ('tests/cast/tls_probe.cpp', 'client/src/cast/stream.cpp', 'client/src/cast/lounge.cpp'):
    obj = folder/(Path(source).stem+'.o')
    subprocess.run(['c++', '-std=c++17', '-O1', '-DPICOJSON_USE_LOCALE=0', '-I'+str(folder),
        '-I'+str(root/'client/src'), '-I'+str(root/'client/third_party'),
        '-I'+str(next((root/'.work/src').glob('curl-xenon-*/include'))), '-c', str(root/source), '-o', str(obj)], check=True)
    objects.append(str(obj))
args = shlex.split((root/'client/build/CMakeFiles/nest-test.dir/link.txt').read_text())
args = [arg for arg in args if not arg.endswith('.o') and arg != 'libtube360-cast.a'
        and not arg.startswith('-Wl,--dependency-file=')]
args[args.index('-o')+1] = str(folder/'check')
args[1:1] = objects
subprocess.run(args, cwd=root/'client/build', check=True)


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        try:
            self.send_response(400 if self.path == '/expired' else 200)
            self.send_header('Connection', 'close')
            if self.path == '/frames':
                self.send_header('Content-Length', '8')
            self.end_headers()
            if self.path == '/frames':
                for part in (b'2', b'\n[', b']2\n', b'[]'):
                    self.wfile.write(part)
                    self.wfile.flush()
                    time.sleep(.02)
            elif self.path == '/idle':
                time.sleep(3)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
server.daemon_threads = True
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(folder/'cert.pem', folder/'key.pem')
server.socket = context.wrap_socket(server.socket, server_side=True)
thread = threading.Thread(target=server.serve_forever)
thread.start()
try:
    subprocess.run([str(folder/'check'), str(server.server_port)], check=True, timeout=15)
finally:
    server.shutdown()
    server.server_close()
    thread.join()
