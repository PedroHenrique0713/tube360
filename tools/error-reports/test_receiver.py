import http.client
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

spec = importlib.util.spec_from_file_location('receiver', Path(__file__).with_name('receiver.py'))
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


class FakeGitHub(BaseHTTPRequestHandler):
    issues = []
    comments = {}
    fail = False
    def log_message(self, *args):
        pass
    def handle_request(self):
        assert self.headers['Authorization'] == 'Bearer disposable-test-token'
        assert self.headers['X-GitHub-Api-Version'] == '2022-11-28'
        assert self.path.startswith('/repos/fixture/tube360/issues')
        data = json.loads(self.rfile.read(int(self.headers.get('Content-Length', 0))) or b'null')
        if self.fail:
            self.send_response(503); self.end_headers(); return
        if self.command == 'GET':
            if '/comments' in self.path:
                result = self.comments.get(int(self.path.split('/')[5]), [])
            elif '?' in self.path:
                result = self.issues
            else:
                result = self.issues[int(self.path.rsplit('/', 1)[1])-1]
        elif self.path.endswith('/comments'):
            assert set(data) == {'body'}
            self.comments.setdefault(int(self.path.split('/')[5]), []).append(data)
            result = data
        else:
            assert set(data) == {'title', 'body', 'labels'}
            assert data['labels'] == ['crash-report']
            result = {**data, 'number': len(self.issues)+1}
            self.issues.append(result)
        body = json.dumps(result).encode()
        self.send_response(200 if self.command == 'GET' else 201)
        self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    do_GET = do_POST = handle_request


class ReceiverTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.now = [1800000000.0]
        FakeGitHub.issues, FakeGitHub.comments, FakeGitHub.fail = [], {}, False
        self.gh = ThreadingHTTPServer(('127.0.0.1', 0), FakeGitHub)
        self.start(self.gh)
        github = r.GitHub('disposable-test-token', 'fixture/tube360', 'http://127.0.0.1:' + str(self.gh.server_port))
        self.store = r.Store(self.temp.name, clock=lambda: self.now[0], github=github)
        self.http = r.Server(('127.0.0.1', 0), r.Handler)
        self.http.store = self.store; self.http.trusted_proxies = set()
        self.start(self.http)
        self.data = dict(version='0.3.3', build='33', hardware='Corona; exploit=unknown', screen='Player', error='gpu fence failed', log='boot marker')
    def start(self, server):
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close); self.addCleanup(server.shutdown)
    def tearDown(self):
        self.store.db.close(); self.temp.cleanup()
    def post(self, data=None, raw=None, headers=None):
        conn = http.client.HTTPConnection('127.0.0.1', self.http.server_port, timeout=3)
        conn.request('POST', '/api/report', body=raw if raw is not None else json.dumps(data or self.data).encode(), headers=headers or {})
        response = conn.getresponse(); value = json.loads(response.read()); conn.close()
        return response.status, value
    def test_delivery_grouping_and_idempotency(self):
        status, first = self.post(); self.assertEqual(status, 200)
        self.assertRegex(first['code'], r'^T360-[A-F0-9]{8}$')
        self.store.deliver(); self.assertEqual(len(FakeGitHub.issues), 1)
        self.assertEqual(self.post()[1], first)
        self.store.deliver(); self.assertEqual(FakeGitHub.comments, {})
        changed = {**self.data, 'log': 'a different backtrace'}
        self.assertEqual(self.post(changed)[0], 200)
        self.store.deliver(); self.assertEqual(len(FakeGitHub.issues), 1)
        self.assertEqual(len(FakeGitHub.comments[1]), 1)
        self.post({**self.data, 'version': '0.3.4'}); self.store.deliver()
        self.assertEqual(len(FakeGitHub.issues), 2)
    def test_restart_and_github_outage(self):
        FakeGitHub.fail = True
        first = self.post(); self.assertEqual(first[0], 200)
        self.store.deliver()
        self.assertIsNone(self.store.db.execute('SELECT issue FROM reports').fetchone()[0])
        github = self.store.github; self.store.db.close()
        self.store = r.Store(self.temp.name, clock=lambda: self.now[0], github=github)
        self.http.store = self.store
        FakeGitHub.fail = False; self.store.deliver()
        self.assertEqual(len(FakeGitHub.issues), 1)
        self.assertEqual(self.post(), first)
        # Simula resposta perdida após o GitHub criar a issue; não cria issue/comentário duplicado.
        self.store.db.execute('UPDATE reports SET issue=NULL'); self.store.db.commit(); self.store.deliver()
        self.assertEqual(len(FakeGitHub.issues), 1); self.assertEqual(FakeGitHub.comments, {})
    def test_sizes_schema_and_privacy(self):
        self.assertEqual(self.post(raw=b'x' * (r.MAX_BODY+1))[0], 413)
        self.assertEqual(self.post(raw=b'{}')[0], 400)
        self.assertEqual(self.post(raw=b'\xff')[0], 400)
        private = 'peer 203.0.113.4 video dQw4w9WgXcQ\nNest: Sala da Maria\nAuthorization: Bearer abc.secret\nya29.secret\nrefresh_token="private"\naccess_token=secret'
        self.assertEqual(self.post({**self.data, 'log': private})[0], 200)
        self.store.deliver()
        stored = (Path(self.temp.name) / 'index.sqlite3').read_bytes()
        for secret in ('203.0.113.4', 'dQw4w9WgXcQ', 'Sala da Maria', 'abc.secret', 'ya29.secret', 'refresh_token', 'access_token', '127.0.0.1'):
            self.assertNotIn(secret.encode(), stored)
            self.assertNotIn(secret, json.dumps(FakeGitHub.issues))
        self.assertTrue(all(isinstance(k, bytes) and len(k) == 32 for k in self.store.ips))
    def test_rate_limits_and_proxy_spoofing(self):
        for i in range(5):
            self.assertEqual(self.post({**self.data, 'log': str(i)}, headers={'X-Real-IP': '192.0.2.'+str(i)})[0], 200)
        self.assertEqual(self.post()[0], 429)
        self.now[0] += 3601
        self.assertEqual(self.post()[0], 200)
        self.store.db.execute('INSERT OR REPLACE INTO days VALUES (?,1000)', (int(self.now[0]//86400),)); self.store.db.commit()
        self.assertEqual(self.store.accept('192.0.2.70', json.dumps({**self.data, 'log': 'new'}))[0], 429)
        self.now[0] += 86400
        self.http.trusted_proxies = {'127.0.0.1'}
        self.assertEqual(self.post(headers={'X-Real-IP': '192.0.2.80'})[0], 200)
        self.assertEqual(self.post(headers={'X-Real-IP': 'forged, 192.0.2.80'})[0], 400)
    def test_concurrent_limits(self):
        with ThreadPoolExecutor(max_workers=10) as pool:
            statuses=list(pool.map(lambda i: self.store.accept('192.0.2.90', json.dumps({**self.data, 'log': str(i)}))[0], range(10)))
        self.assertEqual(statuses.count(200), 5)
        self.assertEqual(statuses.count(429), 5)
        self.store.db.execute('INSERT OR REPLACE INTO days VALUES (?,999)', (int(self.now[0]//86400),)); self.store.db.commit()
        with ThreadPoolExecutor(max_workers=2) as pool:
            statuses=list(pool.map(lambda i: self.store.accept('192.0.2.'+str(i), json.dumps({**self.data, 'log': 'race '+str(i)}))[0], range(2)))
        self.assertEqual(sorted(statuses), [200,429])
        self.assertEqual(self.store.db.execute('SELECT count FROM days').fetchone()[0], 1000)

    def test_age_and_disk_quota(self):
        self.post(); self.now[0] += r.RETENTION+1
        with self.store.lock: self.store.prune()
        self.assertEqual(self.store.db.execute('SELECT COUNT(*) FROM reports').fetchone()[0], 0)
        self.store.quota = 48*1024
        for i in range(5):
            self.store.accept('192.0.2.'+str(i), json.dumps({**self.data, 'log': str(i)+' x'*12500}))
            self.now[0] += 1
        self.assertLessEqual((Path(self.temp.name)/'index.sqlite3').stat().st_size, self.store.quota)
        bodies = self.store.db.execute('SELECT body FROM reports').fetchall()
        self.assertTrue(bodies); self.assertIn('4 x x', bodies[-1][0])


if __name__ == '__main__':
    unittest.main(verbosity=2)
