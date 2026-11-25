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
        '-I'+str(root/'client/build/yt'),
        '-I'+str(next((root/'.work/src').glob('mbedtls-*/include'))),
        '-I'+str(next((root/'.work/src').glob('curl-xenon-*/include'))), '-c', str(root/source), '-o', str(obj)], check=True)
    objects.append(str(obj))
args = shlex.split((root/'client/build/CMakeFiles/tube360.dir/link.txt').read_text())
args = [arg for arg in args if not arg.endswith('.o') and not arg.startswith('-Wl,--dependency-file=')]
args[args.index('-o')+1] = str(folder/'oauth-tls')
args[1:1] = objects
subprocess.run(args, cwd=root/'client/build', check=True)

seen, errors = [], []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, data, cookie=None, status=200):
        self.send_response(status)
        self.send_header('Content-Length', str(len(data)))
        if cookie:
            self.send_header('Set-Cookie', cookie+'; Path=/; Secure')
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        seen.append(self.path)
        if self.path == '/seed':
            self.reply(b'{}', 'YSC=anonymous-fixture')
        elif self.path in ('/', '/tv'):
            if self.headers.get('Cookie') is not None or self.headers.get('Authorization') is not None:
                errors.append('cookie or Bearer leaked into bootstrap')
            name='tv-context.html' if self.path=='/tv' else 'web-context.html' if self.headers.get('Host')=='www.youtube.com' else 'music-context.html'
            self.reply((fixtures/name).read_bytes(), 'OAUTH_BOOTSTRAP=ignored')
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
        client = body['context']['client']
        if client['clientName']=='WEB':
            if self.path!='/youtubei/v1/search?prettyPrint=false' or body.get('params')!='CAM%3D':
                errors.append('public search lost route/filters')
            if any(self.headers.get(name) is not None for name in ('Authorization','Cookie','X-Goog-AuthUser')):
                errors.append('public search leaked auth or cookies')
            if self.headers.get('Host')!='www.youtube.com' or self.headers.get('Origin')!='https://www.youtube.com':
                errors.append('public search wrong Host/Origin')
            if self.headers.get('X-Youtube-Client-Name')!='1' or self.headers.get('X-Youtube-Client-Version')!=client['clientVersion']:
                errors.append('public search inconsistent client headers')
            self.reply((root/'bench/search/fixtures/search-public.json').read_bytes())
            return
        tv = client['clientName'] == 'TVHTML5'
        expected_host = 'www.youtube.com' if tv else 'music.youtube.com'
        if self.headers.get('Host') != expected_host or self.headers.get('Origin') != 'https://' + expected_host:
            errors.append('wrong Host/Origin')
        if self.headers.get('Cookie') is not None or self.headers.get('X-Goog-AuthUser') is not None:
            errors.append('cookie or cookie account selector leaked')
        if self.headers.get('Authorization') != 'Bearer console-fixture-token':
            errors.append('wrong fake token')
        if self.headers.get('X-Youtube-Client-Name') != ('7' if tv else '67'):
            errors.append('wrong numeric client')
        if self.headers.get('X-Youtube-Client-Version') != client['clientVersion']:
            errors.append('version header/context mismatch')
        import time
        if abs(int(self.headers.get('X-Goog-Request-Time', '0')) - time.time()) > 10:
            errors.append('request time missing or not in seconds')
        if body.get('browseId') != ('FEtopics_music' if tv else 'FEmusic_home'):
            errors.append('wrong browse ID for client')
        if self.path != '/youtubei/v1/browse?prettyPrint=false&alt=json':
            errors.append('wrong route/query')
        self.reply((fixtures/('tv-music-public.json' if tv else 'music-invalid-bearer.json')).read_bytes(), 'OAUTH_RESPONSE=ignored', status=200 if tv else 401)



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
assert seen == ['/seed', '/', '/youtubei/v1/browse?prettyPrint=false&alt=json', '/tv', '/youtubei/v1/browse?prettyPrint=false&alt=json', '/', '/youtubei/v1/search?prettyPrint=false', '/probe'], seen
assert not errors, errors
print('WEB_REMIX 401 -> TVHTML5/FEtopics_music 200 through production TLS; Host/Origin/client/time checked; no Cookie/AuthUser; public cookie jar retained; WEB search with account remains anonymous')
