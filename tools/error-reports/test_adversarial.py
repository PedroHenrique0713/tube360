"""Ataques locais: sockets crus e GitHub falso; nenhuma credencial de produção."""
import ipaddress
from pathlib import Path
import socket
import unittest
from unittest.mock import patch

import test_receiver as fixture

FakeGitHub, r = fixture.FakeGitHub, fixture.r


class AdversarialTests(unittest.TestCase):
    setUp = fixture.ReceiverTests.setUp
    tearDown = fixture.ReceiverTests.tearDown
    start = fixture.ReceiverTests.start
    post = fixture.ReceiverTests.post
    emit = fixture.ReceiverTests.emit
    close_day = fixture.ReceiverTests.close_day

    def wire(self, headers, body=b'', path='/api/report'):
        with socket.create_connection(('127.0.0.1', self.http.server_port), timeout=3) as sock:
            sock.sendall(b'POST ' + path.encode() + b' HTTP/1.1\r\nHost: localhost\r\n' + headers + b'\r\n' + body)
            sock.shutdown(socket.SHUT_WR)
            result = b''
            while chunk := sock.recv(65536):
                result += chunk
        self.assertTrue(result.startswith(b'HTTP/1.1 '), repr(result))
        return int(result.split(b' ', 2)[1])

    def test_framing_routes_and_schema(self):
        for headers, body, expected in [
            (b'', b'{}', 400),
            (b'Transfer-Encoding: chunked\r\n', b'2\r\n{}\r\n0\r\n\r\n', 400),
            (b'Content-Length: 2\r\nTransfer-Encoding: chunked\r\n', b'{}', 400),
            (b'Content-Length: 2\r\nContent-Length: 2\r\n', b'{}', 400),
            (b'Content-Length: -1\r\n', b'', 400),
            (b'Content-Length: 10\r\n', b'{}', 400),
            (b'Content-Length: 65537\r\n', b'', 413),
        ]:
            with self.subTest(headers=headers):
                self.assertEqual(self.wire(headers, body), expected)
        for path in ('/api/../report', '/api/report/../../escaped', '/api/%2e%2e/report', '/api/report?path=../../escaped'):
            with self.subTest(path=path):
                self.assertEqual(self.wire(b'Content-Length: 2\r\n', b'{}', path), 404)
        for data in ({**self.data, 'category': 'invented'}, {**self.data, 'installation_id': '../../escaped'},
                     {**self.data, 'version': '../../escaped', 'error': 'unclean session; last checkpoint: gpu\x1b[31m\x00'}):
            status, _ = self.post(data)
            self.assertEqual(status, 200 if data['version'] == '../../escaped' else 400)
        self.assertEqual(sorted(p.name for p in Path(self.temp.name).iterdir()), ['index.sqlite3'])
        point, = self.store.db.execute('SELECT point FROM reports').fetchone()
        self.assertTrue(all(ord(c) >= 32 for c in point), repr(point))

    def test_repository_variants_remain_closed(self):
        for name in ('REVIVE360/TUBE360', ' revive360/tube360 ', 'revive360/tube360', '../tube360'):
            with self.subTest(name=name):
                gh = r.configured_github({'REPORT_TRIAGE_REPO': name, 'GITHUB_TOKEN': 'fake'})
                with patch.object(gh, 'request', return_value={'private': False}) as request:
                    self.assertFalse(gh.is_private())
                    self.assertEqual(request.call_args.args[0], 'GET')
        for name in ('https://github.com/revive360/tube360', 'revive360 /tube360', 'revive360/tube360\n/other'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                r.configured_github({'REPORT_TRIAGE_REPO': name, 'GITHUB_TOKEN': 'fake'})
        for env in ({}, {'GITHUB_TOKEN': 'fake'}, {'GITHUB_REPO': 'revive360/tube360', 'GITHUB_TOKEN': 'fake'}):
            self.assertIsNone(r.configured_github(env))

    def test_forged_installations_reach_threshold_from_one_ip(self):
        for i in range(1, 4):
            status, _ = self.post({**self.data, 'installation_id': f'{i:032x}'})
            self.assertEqual(status, 200)
        self.store.deliver()
        self.assertEqual(len(FakeGitHub.issues), 1)
        print('LIMITATION: 3 forged installation IDs from one IP created 1 fake GitHub issue', flush=True)
        for i in range(4, 21):
            status, _ = self.post({**self.data, 'installation_id': f'{i:032x}'})
            self.assertEqual(status, 200 if i <= 5 else 429)
        self.store.deliver()
        self.assertEqual(len(FakeGitHub.issues), 1)

    def test_daily_limit_with_rotating_ids_and_ips(self):
        self.http.trusted_proxies = {'127.0.0.1'}
        for i in range(1, 1002):
            ip = str(ipaddress.ip_address('198.18.0.0') + i)
            status, _ = self.post({**self.data, 'error': 'DNS failed', 'installation_id': f'{i:032x}'},
                                  headers={'X-Real-IP': ip})
            self.assertEqual(status, 200 if i <= 1000 else 429, i)
        self.assertEqual(self.store.db.execute('SELECT count FROM days').fetchone(), (1000,))
        print('PASS HTTP flood: 1000 unique reports accepted, request 1001 rejected with 429', flush=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
