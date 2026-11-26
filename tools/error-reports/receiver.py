#!/usr/bin/env python3
"""Receptor Tube360; biblioteca padrão, sem logs de acesso ou IP persistente."""
import collections
import hashlib
import hmac
import http.server
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import sqlite3
import threading
import time
import urllib.error
import urllib.request

MAX_BODY = 64 * 1024
RETENTION = 30 * 86400
QUOTA = 200 * 1024 * 1024
FIELDS = {'version', 'build', 'hardware', 'screen', 'error', 'log'}


def scrub(value):
    # Defesa adicional: o launcher já remove esses dados antes do POST.
    lines = []
    for line in value.splitlines():
        if re.search(r'bearer|ya29\.|token|cookie|authorization|cast|nest|name|nome|aparelho|friendly.?name|device.?name|conta|account|login|search|busca|query|api_key|client_secret|password|senha|oauth|email|@', line, re.I):
            lines.append('[private line removed]')
        else:
            line = re.sub(r'(?<!\d)\d{1,3}(?:\.\d{1,3}){3}(?!\d)', '[ip]', line)
            line = re.sub(r'(?<![A-Za-z0-9_-])[A-Za-z0-9_-]{11}(?![A-Za-z0-9_-])', '[video]', line)
            line = re.sub(r'[A-Za-z0-9_-]{32,}', '[redacted]', line)
            lines.append(line)
    return '\n'.join(lines)


class Store:
    def __init__(self, directory, clock=time.time, quota=QUOTA, github=None):
        self.path = Path(directory)
        self.path.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.clock, self.quota, self.github = clock, quota, github
        self.lock = threading.Lock()
        self.salt = secrets.token_bytes(32)
        self.ips = {}
        self.db = sqlite3.connect(self.path / 'index.sqlite3', check_same_thread=False)
        self.db.execute('PRAGMA journal_mode=DELETE')
        self.db.execute('CREATE TABLE IF NOT EXISTS reports (id TEXT PRIMARY KEY, signature TEXT, code TEXT, created REAL, body TEXT, issue INTEGER)')
        self.db.execute('CREATE TABLE IF NOT EXISTS days (day INTEGER PRIMARY KEY, count INTEGER)')
        self.db.commit()
        with self.lock:
            self.prune()

    def prune(self):
        now = self.clock()
        self.db.execute('DELETE FROM reports WHERE created < ?', (now - RETENTION,))
        # Inclui o índice SQLite no teto; VACUUM também libera páginas de relatórios apagados.
        self.db.commit()
        self.db.execute('VACUUM')
        while (self.path / 'index.sqlite3').stat().st_size > self.quota:
            row = self.db.execute('SELECT id FROM reports ORDER BY created, rowid LIMIT 1').fetchone()
            if row is None:
                break
            self.db.execute('DELETE FROM reports WHERE id=?', row)
            self.db.commit()
            self.db.execute('VACUUM')
        self.db.execute('DELETE FROM days WHERE day < ?', (int(now // 86400) - 2,))
        self.db.commit()

    def accept(self, ip, raw):
        data = json.loads(raw)
        if not isinstance(data, dict) or set(data) != FIELDS or any(not isinstance(v, str) for v in data.values()):
            raise ValueError('schema')
        if any(len(data[k]) > n for k, n in [('version', 128), ('build', 128), ('hardware', 8192), ('screen', 256), ('error', 2048)]):
            raise ValueError('field size')
        screens = {'Home', 'Player', 'Search', 'Settings', 'Navigation', 'startup/unknown'}
        data = {k: (v if v in screens else 'startup/unknown') if k == 'screen' else scrub(v) for k, v in data.items()}
        body = json.dumps(data, ensure_ascii=False, sort_keys=True)
        if len(body.encode()) > MAX_BODY:
            raise ValueError('size')
        identity = hashlib.sha256(body.encode()).hexdigest()
        signature = hashlib.sha256((data['version'] + '\n' + data['error']).encode()).hexdigest()
        now = self.clock()
        with self.lock:
            self.ips = {key: times for key, times in self.ips.items() if times and times[-1] > now - 3600}
            key = hmac.new(self.salt, ip.encode(), hashlib.sha256).digest()
            times = self.ips.setdefault(key, collections.deque())
            while times and times[0] <= now - 3600:
                times.popleft()
            if len(times) >= 5:
                return 429, {'error': 'hourly limit'}
            times.append(now)
            self.prune()
            old = self.db.execute('SELECT code FROM reports WHERE id=?', (identity,)).fetchone()
            if old:
                return 200, {'code': old[0]}
            day = int(now // 86400)
            count = self.db.execute('SELECT count FROM days WHERE day=?', (day,)).fetchone()
            if count and count[0] >= 1000:
                return 429, {'error': 'daily limit'}
            code = 'T360-' + secrets.token_hex(4).upper()
            self.db.execute('INSERT INTO days VALUES (?,1) ON CONFLICT(day) DO UPDATE SET count=count+1', (day,))
            self.db.execute('INSERT INTO reports VALUES (?,?,?,?,?,NULL)', (identity, signature, code, now, body))
            self.db.commit()
            self.prune()
            return 200, {'code': code}

    def deliver(self):
        # Um único worker; rede fora da trava do disco/HTTP. O outbox sobrevive a reinícios.
        with self.lock:
            rows = self.db.execute('SELECT id, signature, code, body FROM reports WHERE issue IS NULL ORDER BY created LIMIT 25').fetchall()
        for identity, signature, code, body in rows:
            try:
                issue = self.github.publish(signature, code, json.loads(body))
            except (OSError, ValueError, urllib.error.URLError):
                continue
            with self.lock:
                self.db.execute('UPDATE reports SET issue=? WHERE id=?', (issue, identity))
                self.db.commit()

    def work(self):
        while True:
            self.deliver()
            with self.lock:
                self.prune()
            time.sleep(30)



class GitHub:
    def __init__(self, token, repo, base='https://api.github.com'):
        if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo):
            raise ValueError('GITHUB_REPO must be owner/repository')
        self.token, self.repo, self.base = token, repo, base

    def request(self, method, path, data=None):
        raw = None if data is None else json.dumps(data).encode()
        req = urllib.request.Request(self.base + path, data=raw, method=method, headers={
            'Authorization': 'Bearer ' + self.token, 'Accept': 'application/vnd.github+json',
            'X-GitHub-Api-Version': '2022-11-28', 'User-Agent': 'Tube360-error-receiver',
            'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=8) as response:
            return json.load(response)

    def publish(self, signature, code, data):
        marker = '<!-- tube360-signature:' + signature + ' -->'
        prefix = '/repos/' + self.repo + '/issues'
        # Consulta por rótulo evita o atraso do índice /search e inclui issues fechadas.
        issue = None
        page = 1
        while True:
            rows = self.request('GET', prefix + '?state=all&labels=crash-report&per_page=100&page=' + str(page))
            for row in rows:
                if marker in (row.get('body') or '') and 'pull_request' not in row:
                    issue = row['number']
                    break
            if issue or len(rows) < 100:
                break
            page += 1
        body = marker + '\nReport ' + code + '\n\n' + '\n'.join(k + ': ' + data[k] for k in ('version', 'build', 'hardware', 'screen', 'error'))
        # Sem Markdown interpretado dentro do log, inclusive se houver crases no dado.
        body += '\n\nLog (sanitized):\n' + '\n'.join('    ' + line for line in data['log'].splitlines())
        body = body[:60000]
        delivery = '<!-- tube360-report:' + code + ' -->'
        body = delivery + '\n' + body
        if issue:
            page = 1
            while True:
                comments = self.request('GET', prefix + '/' + str(issue) + '/comments?per_page=100&page=' + str(page))
                if any(delivery in (comment.get('body') or '') for comment in comments):
                    return issue
                if len(comments) < 100:
                    break
                page += 1
            # O primeiro relatório já está no corpo da issue.
            original = self.request('GET', prefix + '/' + str(issue))
            if delivery not in (original.get('body') or ''):
                self.request('POST', prefix + '/' + str(issue) + '/comments', {'body': body})
        else:
            issue = self.request('POST', prefix, {'title': ('Crash ' + data['version'] + ': ' + data['error'])[:160],
                                                 'body': body, 'labels': ['crash-report']})['number']
        return issue


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    def handle_error(self, request, client_address):
        pass  # O handler padrão inclui o endereço do cliente no stderr.


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args):
        pass

    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def reply(self, status, data):
        raw = json.dumps(data).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(raw)))
        self.send_header('Connection', 'close')
        self.end_headers()
        self.wfile.write(raw)
        self.close_connection = True

    def do_POST(self):
        if self.path != '/api/report':
            return self.reply(404, {'error': 'route'})
        lengths = self.headers.get_all('Content-Length', [])
        if len(lengths) != 1 or not lengths[0].isdigit() or self.headers.get('Transfer-Encoding'):
            return self.reply(400, {'error': 'length'})
        length = int(lengths[0])
        if length > MAX_BODY:
            return self.reply(413, {'error': 'size'})
        try:
            ip = self.client_address[0]
            # Só o peer explicitamente autorizado pode fornecer UM endereço, sobrescrito pelo proxy.
            if ip in self.server.trusted_proxies:
                ip = str(ipaddress.ip_address(self.headers.get('X-Real-IP', '')))
            raw = self.rfile.read(length)
            if len(raw) != length:
                raise ValueError('short body')
            status, result = self.server.store.accept(ip, raw)
        except (ValueError, UnicodeError):
            return self.reply(400, {'error': 'invalid report'})
        except (OSError, sqlite3.Error):
            return self.reply(503, {'error': 'storage unavailable'})
        self.reply(status, result)


def main():
    os.umask(0o077)
    github = GitHub(os.environ['GITHUB_TOKEN'], os.environ['GITHUB_REPO'])
    server = Server((os.environ.get('REPORT_BIND', '127.0.0.1'), int(os.environ.get('REPORT_PORT', '8080'))), Handler)
    server.trusted_proxies = set(filter(None, os.environ.get('REPORT_TRUSTED_PROXIES', '').split(',')))
    server.store = Store(os.environ.get('REPORT_DATA', './data'), github=github)
    threading.Thread(target=server.store.work, daemon=True).start()
    server.serve_forever()


if __name__ == '__main__':
    main()
