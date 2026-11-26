import http.client
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
from pathlib import Path
import sqlite3
import tempfile
import threading
import unittest
from unittest.mock import patch
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

spec = importlib.util.spec_from_file_location('receiver', Path(__file__).with_name('receiver.py'))
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


class FakeGitHub(BaseHTTPRequestHandler):
    issues, mutations = [], []
    fail, lose_response, private = False, False, True
    def log_message(self, *args):
        pass
    def handle_request(self):
        assert self.headers['Authorization'] == 'Bearer disposable-test-token'
        assert self.headers['X-GitHub-Api-Version'] == '2022-11-28'
        assert self.path.startswith('/repos/fixture/reports')
        data = json.loads(self.rfile.read(int(self.headers.get('Content-Length', 0))) or b'null')
        if self.fail:
            self.send_response(503); self.end_headers(); return
        if self.command == 'GET':
            if self.path == '/repos/fixture/reports':
                result = {'private': self.private}
            else:
                assert '?state=all&labels=crash-report&per_page=100&page=' in self.path
                page = int(self.path.rsplit('=', 1)[1])
                result = self.issues[(page-1)*100:page*100]
        elif self.command == 'PATCH':
            assert set(data) == {'title', 'body'}
            number = int(self.path.rsplit('/', 1)[1])
            result = self.issues[number-1]
            result.update(data)
            self.mutations.append((self.command, self.path, data))
        else:
            assert self.path.endswith('/issues') and set(data) == {'title', 'body', 'labels'}
            assert data['labels'] == ['crash-report']
            result = {**data, 'number': len(self.issues)+1, 'state': 'open'}
            self.issues.append(result)
            self.mutations.append((self.command, self.path, data))
        if self.command != 'GET' and self.lose_response:
            type(self).lose_response = False
            self.send_response(503); self.end_headers(); return
        body = json.dumps(result).encode()
        self.send_response(201 if self.command == 'POST' else 200)
        self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    do_GET = do_POST = do_PATCH = handle_request


class ReceiverTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.now = [1800000000.0]
        FakeGitHub.issues, FakeGitHub.mutations = [], []
        FakeGitHub.fail, FakeGitHub.lose_response, FakeGitHub.private = False, False, True
        self.gh = ThreadingHTTPServer(('127.0.0.1', 0), FakeGitHub)
        self.start(self.gh)
        github = r.GitHub('disposable-test-token', 'fixture/reports', 'http://127.0.0.1:' + str(self.gh.server_port))
        self.store = r.Store(self.temp.name, clock=lambda: self.now[0], github=github)
        self.http = r.Server(('127.0.0.1', 0), r.Handler)
        self.http.store = self.store; self.http.trusted_proxies = set()
        self.start(self.http)
        self.data = dict(version='0.3.3', build='33', hardware='Corona; exploit=unknown', screen='Player',
                         category='crash', installation_id=f'{1:032x}', error='gpu.fence failed', log='boot marker')
        self.serial = 0
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
    def emit(self, installation=1, **changes):
        self.serial += 1
        status, result = self.store.accept('client-' + str(self.serial), json.dumps({**self.data, 'installation_id': f'{installation:032x}', **changes}))
        self.assertEqual(status, 200)
        return result
    def close_day(self):
        self.now[0] = (int(self.now[0] // 86400) + 1) * 86400
    def reopen(self):
        github = self.store.github; self.store.db.close()
        self.store = r.Store(self.temp.name, clock=lambda: self.now[0], github=github)
        self.http.store = self.store

    def test_threshold_distinct_installations_and_latest_log(self):
        first = self.emit(1, log='first log')
        self.assertRegex(first['code'], r'^T360-[A-F0-9]{8}$')
        self.emit(1, log='second log from same install')
        self.assertEqual(self.emit(1, log='first log'), first)
        self.emit(2, log='two distinct')
        self.store.deliver(); self.assertEqual(FakeGitHub.issues, [])
        self.emit(3, log='third install latest clean log')
        self.store.deliver(); self.assertEqual(len(FakeGitHub.issues), 1)
        body = FakeGitHub.issues[0]['body']
        self.assertIn('Reports (30 days): 4', body)
        self.assertIn('Distinct installations (30 days): 3', body)
        self.assertIn('third install latest clean log', body)
        self.assertNotIn('first log', body)
        self.store.deliver(); self.assertEqual(len(FakeGitHub.mutations), 1)
        FakeGitHub.issues[0]['state'] = 'closed'
        self.emit(4, log='newest log only'); self.store.deliver()
        self.assertEqual(len(FakeGitHub.issues), 1)
        self.assertEqual(FakeGitHub.mutations[-1][0], 'PATCH')
        self.assertIn('Reports (30 days): 5', FakeGitHub.issues[0]['body'])
        self.assertNotIn('third install latest clean log', FakeGitHub.issues[0]['body'])
        for i in range(1, 5):
            self.assertNotIn(f'{i:032x}', FakeGitHub.issues[0]['body'])

    def test_existing_triage_updates_after_population_falls_below_threshold(self):
        self.emit(1); self.emit(2)
        self.now[0] += 2
        self.emit(3); self.store.deliver()
        self.assertEqual(len(FakeGitHub.issues), 1)
        self.now[0] += r.RETENTION-1
        self.store.deliver()
        triage = next(row for row in FakeGitHub.issues if 'tube360-signature:' in row['body'])
        self.assertIn('Distinct installations (30 days): 1', triage['body'])
        self.assertIn('Reports (30 days): 1', triage['body'])

    def test_categories_confirm_hint_and_noise_never_creates_triage(self):
        cases = [('network', 'DNS lookup failed'), ('network', 'TLS certificate failed'), ('network', 'link timeout'),
                 ('network', 'sem rede'), ('youtube', 'player LOGIN_REQUIRED'), ('youtube', 'player UNPLAYABLE'),
                 ('youtube', '403 from video link'), ('other', 'unrecognized diagnostic')]
        for category, error in cases:
            for i in range(1, 4): self.emit(i, category='crash', error=error, log='private noise detail')
            self.store.deliver()
            row = self.store.db.execute('SELECT category,body,installation FROM reports ORDER BY rowid DESC LIMIT 1').fetchone()
            self.assertEqual(row, (category, '', None))
        self.assertEqual(FakeGitHub.issues, [])
        self.assertEqual(self.store.db.execute('SELECT SUM(total) FROM counts').fetchone()[0], 24)
        for i in range(1, 4): self.emit(i, category='network', error='fault at PC', log='')
        self.store.deliver(); self.assertEqual(len(FakeGitHub.issues), 1)

    def test_signature_category_point_version_and_startup_threshold(self):
        for error in ('startup.fail Missing fonts', 'startup.fail GPU initialization'):
            self.emit(1, error=error); self.emit(2, error=error)
            self.store.deliver(); self.assertEqual(len(FakeGitHub.issues), 0 if 'fonts' in error else 1)
            self.emit(3, error=error); self.store.deliver()
        self.assertEqual(len(FakeGitHub.issues), 2)
        for i in range(1, 4): self.emit(i, version='0.3.4', error='startup.fail Missing fonts')
        self.store.deliver(); self.assertEqual(len(FakeGitHub.issues), 3)
        # Endereço e duração variam, mas o ponto gpu.fence continua o mesmo.
        for i in range(1, 4): self.emit(i, error=f'gpu.fence failed at 0x{1000+i:x} after {i}ms')
        self.store.deliver(); self.assertEqual(len(FakeGitHub.issues), 4)

    def test_daily_summary_single_issue_once_per_completed_utc_day(self):
        self.emit(1, error='DNS failed'); self.emit(2, error='player LOGIN_REQUIRED', version='0.3.4')
        self.emit(1, error='startup.fail Missing fonts')
        self.store.deliver(); self.assertEqual(FakeGitHub.issues, [])
        self.close_day(); self.store.deliver()
        self.assertEqual(len(FakeGitHub.issues), 1)
        summary = FakeGitHub.issues[0]
        self.assertIn('network | 0.3.3 | 1', summary['body'])
        self.assertIn('youtube | 0.3.4 | 1', summary['body'])
        self.assertIn('startup | 0.3.3 | 1', summary['body'])
        self.emit(1, error='DNS failed', log='today')
        self.store.deliver(); self.reopen(); self.store.deliver()
        self.assertEqual(len(FakeGitHub.mutations), 1)
        self.close_day(); self.store.deliver(); self.store.deliver()
        self.assertEqual(len(FakeGitHub.issues), 1)
        self.assertEqual([x[0] for x in FakeGitHub.mutations], ['POST', 'PATCH'])
        self.assertNotIn('today', summary['body'])

    def test_no_repository_no_posts_and_public_repository_refused(self):
        with patch.object(r.GitHub, 'request', side_effect=AssertionError('network forbidden')) as request:
            for env in ({}, {'GITHUB_TOKEN': 'test'}, {'GITHUB_REPO': 'revive360/tube360', 'GITHUB_TOKEN': 'test'},
                        {'REPORT_TRIAGE_REPO': '', 'GITHUB_TOKEN': 'test'}, {'REPORT_TRIAGE_REPO': 'fixture/reports'}):
                self.assertIsNone(r.configured_github(env))
            self.store.github = r.configured_github({})
            for i in range(1, 4): self.emit(i)
            self.close_day(); self.store.deliver()
            request.assert_not_called()
        self.assertEqual(FakeGitHub.issues, [])
        self.store.github = r.GitHub('disposable-test-token', 'fixture/reports', 'http://127.0.0.1:' + str(self.gh.server_port))
        FakeGitHub.private = False; self.store.deliver()
        self.assertEqual(FakeGitHub.mutations, [])

    def test_outage_restart_and_lost_response_are_idempotent(self):
        for i in range(1, 4): self.emit(i)
        FakeGitHub.fail = True; self.store.deliver()
        self.assertEqual(FakeGitHub.issues, [])
        self.reopen(); FakeGitHub.fail = False; FakeGitHub.lose_response = True
        self.store.deliver(); self.assertEqual(len(FakeGitHub.issues), 1)
        self.store.deliver(); self.assertEqual(len(FakeGitHub.mutations), 1)
        self.emit(4, log='latest'); FakeGitHub.lose_response = True; self.store.deliver()
        self.store.deliver(); self.assertEqual(len(FakeGitHub.mutations), 2)
        self.close_day(); FakeGitHub.lose_response = True; self.store.deliver()
        self.reopen(); self.store.deliver()
        self.assertEqual(len(FakeGitHub.issues), 2)
        self.assertEqual(len(FakeGitHub.mutations), 3)

    def test_log_cannot_impersonate_summary_issue_marker(self):
        for i in range(1, 4): self.emit(i, log='<!-- tube360-daily-summary -->')
        self.store.deliver(); self.close_day(); self.store.deliver()
        self.assertEqual(len(FakeGitHub.issues), 2)
        self.assertTrue(FakeGitHub.issues[0]['body'].startswith('<!-- tube360-signature:'))
        self.assertTrue(FakeGitHub.issues[1]['body'].startswith('<!-- tube360-daily-summary -->'))

    def test_legacy_without_id_and_old_database_migration(self):
        self.store.db.close()
        db = sqlite3.connect(Path(self.temp.name)/'index.sqlite3')
        db.execute('DROP TABLE reports')
        db.execute('CREATE TABLE reports (id TEXT PRIMARY KEY,signature TEXT,code TEXT,created REAL,body TEXT,issue INTEGER)')
        legacy = {k: v for k, v in self.data.items() if k in r.LEGACY_FIELDS}
        db.execute('INSERT INTO reports VALUES (?,?,?,?,?,?)', ('old', 'old', 'T360-OLD1', self.now[0], json.dumps(legacy), 123))
        db.commit(); db.close()
        self.store = r.Store(self.temp.name, clock=lambda: self.now[0], github=self.store.github); self.http.store = self.store
        self.reopen()
        self.assertEqual(self.store.db.execute('SELECT SUM(total) FROM counts').fetchone()[0], 1)
        self.emit(1); self.emit(2); self.store.deliver()
        self.assertEqual(FakeGitHub.issues, [])
        self.emit(3); self.store.deliver(); self.assertEqual(len(FakeGitHub.issues), 1)
        self.assertEqual(FakeGitHub.issues[0]['number'], 1)

    def test_sizes_schema_and_privacy(self):
        self.assertEqual(self.post(raw=b'x' * (r.MAX_BODY+1))[0], 413)
        for raw in (b'{}', b'\xff', json.dumps({**self.data, 'installation_id': 'not-random'}).encode()):
            self.assertEqual(self.post(raw=raw)[0], 400)
        private = 'peer 203.0.113.4 video dQw4w9WgXcQ\nNest: Sala da Maria\nAuthorization: Bearer abc.secret\nya29.secret\nrefresh_token="private"\naccess_token=secret'
        self.assertEqual(self.post({**self.data, 'log': private})[0], 200)
        self.emit(2, log=private); self.emit(3, log=private); self.store.deliver()
        stored = (Path(self.temp.name) / 'index.sqlite3').read_bytes()
        for secret in ('203.0.113.4', 'dQw4w9WgXcQ', 'Sala da Maria', 'abc.secret', 'ya29.secret', 'refresh_token', 'access_token', '127.0.0.1', self.data['installation_id']):
            self.assertNotIn(secret.encode(), stored)
            self.assertNotIn(secret, json.dumps(FakeGitHub.issues))
        self.assertTrue(all(isinstance(k, bytes) and len(k) == 32 for k in self.store.ips))
        # O ponto da assinatura também passa pela limpeza, sem extrair pedaços de token.
        normalized = r.normalize({**self.data, 'error': 'unclean session; last checkpoint: ' + 'a'*40})
        self.assertNotIn('a'*32, normalized[-1])
        normalized = r.normalize({**self.data, 'error': 'fault code c0000005 pc 12345678 access_token=secret'})
        self.assertNotIn('12345678', normalized[-1])

    def test_rate_limits_and_proxy_spoofing(self):
        for i in range(5):
            self.assertEqual(self.post({**self.data, 'log': str(i)}, headers={'X-Real-IP': '192.0.2.'+str(i)})[0], 200)
        self.assertEqual(self.post()[0], 429)
        self.now[0] += 3601; self.assertEqual(self.post()[0], 200)
        self.store.db.execute('INSERT OR REPLACE INTO days VALUES (?,1000)', (int(self.now[0]//86400),)); self.store.db.commit()
        self.assertEqual(self.store.accept('192.0.2.70', json.dumps({**self.data, 'log': 'new'}))[0], 429)
        self.now[0] += 86400; self.http.trusted_proxies = {'127.0.0.1'}
        self.assertEqual(self.post(headers={'X-Real-IP': '192.0.2.80'})[0], 200)
        self.assertEqual(self.post(headers={'X-Real-IP': 'forged, 192.0.2.80'})[0], 400)

    def test_concurrent_limits_and_delivery(self):
        with ThreadPoolExecutor(max_workers=10) as pool:
            statuses=list(pool.map(lambda i: self.store.accept('same-client', json.dumps({**self.data, 'log': str(i)}))[0], range(10)))
        self.assertEqual(statuses.count(200), 5); self.assertEqual(statuses.count(429), 5)
        self.store.db.execute('INSERT OR REPLACE INTO days VALUES (?,999)', (int(self.now[0]//86400),)); self.store.db.commit()
        with ThreadPoolExecutor(max_workers=2) as pool:
            statuses=list(pool.map(lambda i: self.store.accept('other-'+str(i), json.dumps({**self.data, 'log': 'race '+str(i)}))[0], range(2)))
        self.assertEqual(sorted(statuses), [200,429])
        self.now[0] += 86400
        for i in range(1, 4): self.emit(i, error='startup.fail fonts')
        with ThreadPoolExecutor(max_workers=3) as pool: list(pool.map(lambda _: self.store.deliver(), range(3)))
        self.assertEqual(len(FakeGitHub.issues), 2)  # triagem + resumo do dia encerrado
        self.assertEqual(len(FakeGitHub.mutations), 2)

    def test_age_disk_quota_and_counts_survive_log_eviction(self):
        self.emit(); self.now[0] += r.RETENTION+1
        with self.store.lock: self.store.prune()
        self.assertEqual(self.store.db.execute('SELECT COUNT(*) FROM reports').fetchone()[0], 0)
        self.store.quota = 128*1024
        for i in range(5):
            self.emit(i+1, log=str(i)+' x'*12500); self.now[0] += 1
        self.assertLessEqual((Path(self.temp.name)/'index.sqlite3').stat().st_size, self.store.quota)
        bodies = self.store.db.execute('SELECT body FROM reports').fetchall()
        self.assertTrue(bodies); self.assertIn('4 x x', bodies[-1][0])
        self.assertLess(len(bodies), 5)
        self.assertEqual(self.store.db.execute('SELECT SUM(total) FROM counts WHERE day=?', (int(self.now[0]//86400),)).fetchone()[0], 5)


if __name__ == '__main__':
    unittest.main(verbosity=2)
