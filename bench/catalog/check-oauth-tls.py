"""HTTPS real em loopback: mesmo libcurl/mbedTLS do app, só a raiz de teste é substituída."""
import http.server
import json
import pathlib
import shlex
import ssl
import subprocess
import sys
import threading

root = pathlib.Path(__file__).resolve().parents[2]
folder = pathlib.Path(sys.argv[1]).resolve()
folder.mkdir(parents=True, exist_ok=True)
fixtures = root / 'bench/catalog/fixtures'
subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
    '-subj', '/CN=127.0.0.1', '-addext', 'subjectAltName=IP:127.0.0.1',
    '-keyout', str(folder/'key.pem'), '-out', str(folder/'cert.pem')], check=True,
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
(folder/'roots.hpp').write_text('static const char ytRoots[] = R"CA('+(folder/'cert.pem').read_text()+')CA";\n')
objects = []
for source in ['bench/catalog/oauth-tls.cpp', 'client/src/yt/https.cpp']:
    obj = folder / (pathlib.Path(source).stem+'.o')
    subprocess.run(['c++', '-std=c++20', '-O1', '-DPICOJSON_USE_LOCALE=0', '-I'+str(folder),
        '-I'+str(root/'client/src'), '-I'+str(root/'client/third_party'),
        '-I'+str(next((root/'.work/src').glob('curl-xenon-*/include'))), '-c', str(root/source), '-o', str(obj)], check=True)
    objects.append(str(obj))
args = shlex.split((root/'client/build/CMakeFiles/tube360.dir/link.txt').read_text())
args = [arg for arg in args if not arg.endswith('.o') and not arg.startswith('-Wl,--dependency-file=')]
args[args.index('-o')+1] = str(folder/'oauth-tls')
args[1:1] = objects
subprocess.run(args, cwd=root/'client/build', check=True)
oracle = json.loads((fixtures/'oauth-requests.json').read_text())['requests'][0]
seen, errors = [], []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, data, cookie=None):
        self.send_response(200)
        self.send_header('Content-Length', str(len(data)))
        if cookie:
            self.send_header('Set-Cookie', cookie+'; Path=/; Secure')
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        seen.append(self.path)
        if self.path == '/seed':
            self.reply(b'{}', 'YSC=anonymous-fixture')
        elif self.path == '/sw.js_data':
            if self.headers.get('Cookie') != 'PREF=tz=UTC;':
                errors.append('anonymous cookie leaked into OAuth bootstrap')
            self.reply((fixtures/'tv-session.txt').read_bytes(), 'OAUTH_BOOTSTRAP=ignored')
        elif self.path == '/probe':
            if self.headers.get('Cookie') != 'YSC=anonymous-fixture':
                errors.append('public cookie jar lost')
            self.reply(b'{}')
        else:
            errors.append('unexpected GET '+self.path)
            self.send_error(400)

    def do_POST(self):
        seen.append(self.path)
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        headers = {k.lower(): v for k, v in self.headers.items()}
        if headers.pop('host') != 'www.youtube.com':
            errors.append('wrong Host')
        headers.pop('content-length')
        if headers.pop('authorization', None) != 'Bearer console-fixture-token':
            errors.append('wrong fake console token')
        expected = dict(oracle['headers'])
        expected.pop('authorization')
        if headers != expected:
            errors.append('wire headers differ from youtubei.js: '+repr(headers))
        if body != oracle['body']:
            errors.append('wire context/body differ from youtubei.js')
        if self.path != oracle['url'].split('www.youtube.com', 1)[1]:
            errors.append('wrong route/query')
        self.reply((fixtures/'music-home.json').read_bytes(), 'OAUTH_RESPONSE=ignored')


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
server.daemon_threads = True
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(folder/'cert.pem', folder/'key.pem')
server.socket = context.wrap_socket(server.socket, server_side=True)
thread = threading.Thread(target=server.serve_forever)
thread.start()
try:
    subprocess.run([str(folder/'oauth-tls'), str(server.server_port)], check=True, timeout=20)
finally:
    server.shutdown()
    server.server_close()
    thread.join()
assert seen == ['/seed', '/sw.js_data', '/youtubei/v1/browse?prettyPrint=false&alt=json', '/probe'], seen
assert not errors, errors
print('Wire URL, Host, headers and complete JSON equal youtubei.js; only fake token differs; no Cookie/AuthUser; public cookies retained')
