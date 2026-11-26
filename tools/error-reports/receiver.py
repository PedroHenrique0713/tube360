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
LEGACY_FIELDS = {'version', 'build', 'hardware', 'screen', 'error', 'log'}
FIELDS = LEGACY_FIELDS | {'category', 'installation_id'}
CATEGORIES = {'crash', 'startup', 'network', 'youtube', 'other'}


def scrub(value):
    lines = []
    for line in value.splitlines():
        private = re.search(r'bearer|ya29\.|github_pat_|sapisid|__secure-|token|cookie|authorization|cast|nest|name|nome|aparelho|conta|account|login|search|busca|query|api_key|client_secret|password|senha|oauth|email|@|ssid|wi-?fi|/home/|/users/|\\users\\', line, re.I)
        # Mesma política conservadora do launcher para IPv6 e host:porta.
        endpoints = re.findall(r'[A-Za-z0-9_.%:-]+', line)
        private = private or any(token.count(':') >= 2 or re.fullmatch(r'[A-Za-z0-9_.%-]+:[0-9]+', token) for token in endpoints)
        if private:
            lines.append('[private line removed]')
        else:
            line = re.sub(r'(?<!\d)\d{1,3}(?:\.\d{1,3}){3}(?!\d)', '[ip]', line)
            line = re.sub(r'(?<![A-Za-z0-9_-])[A-Za-z0-9_-]{11}(?![A-Za-z0-9_-])', '[video]', line)
            lines.append(re.sub(r'[A-Za-z0-9_-]{32,}', '[redacted]', line))
    return '\n'.join(lines)


def classify(data):
    """A dica do cliente não é autoridade; só o conteúdo confirma a categoria."""
    error = data['error'].lower()
    log = data['log'].lower()
    evidence = error + '\n' + log
    safe_error, safe_evidence = scrub(error), scrub(evidence)
    if re.search(r'excecao: codigo|exception code|segmentation fault|sigsegv|sigabrt|\bfault\b(?!-handler)|assertion failed|panic|gpu[ ._-]fence.*(?:fail|dead|timeout)', evidence):
        code = re.search(r'(?:excecao: codigo|exception code|fault code)\s+([0-9a-f]{8})', safe_evidence)
        point = 'fault.' + code[1] if code else 'gpu.fence' if re.search(r'gpu[ ._-]fence', evidence) else 'fault'
        pc = re.search(r'(?:endereco|address|pc)\s*(?:0x)?([0-9a-f]{8})', safe_evidence)
        if pc and point.startswith('fault'):
            point += '.pc.' + pc[1]
        return 'crash', point
    # Timeouts de handoff/partida não são timeouts de rede.
    if re.search(r'startup.*(?:timeout|20 seconds|20 segundos)|handoff.*(?:timeout|did not reach)', error):
        return 'startup', 'startup.handoff'
    def cause(text):
        if re.search(r'login_required|youtube\.auth_required', text):
            return 'youtube', 'youtube.auth_required'
        if re.search(r'unplayable|youtube\.unplayable', text):
            return 'youtube', 'youtube.unplayable'
        if '403' in text and re.search(r'youtube|player|media|link|googlevideo', text):
            return 'youtube', 'youtube.media_403'
        if re.search(r'\bdns\b|network\.dns|could not resolve', text):
            return 'network', 'network.dns'
        if re.search(r'\btls\b|\bssl\b|network\.tls|certificate', text):
            return 'network', 'network.tls'
        if re.search(r'timeout|timed out|network\.timeout', text):
            return 'network', 'network.timeout'
        if re.search(r'network\.offline|offline|no network|no connection|sem rede|sem conex|sem conexão|network unreachable', text):
            return 'network', 'network.offline'
    found = cause(error)
    if found:
        return found
    if re.search(r'startup\.fail|startup\.incomplete|missing fonts|could not start|falha de partida', error):
        checkpoint = re.search(r'last checkpoint:\s*([a-z0-9_.-]+)', safe_error)
        if checkpoint:
            return 'startup', 'startup.' + checkpoint[1]
        point = 'startup.fonts' if re.search(r'font', error) else 'startup.storage' if re.search(r'storage|writ|folder|disco', error) else 'startup.video' if re.search(r'gpu|video|display', error) else 'startup.handoff' if re.search(r'launch|handoff', error) else 'startup.fail'
        return 'startup', point
    if re.search(r'unclean session|session: unclean|\bhang\b|travamento|watchdog expired', error + '\n' + log):
        checkpoint = re.search(r'last checkpoint:\s*([a-z0-9_.-]+)', safe_error)
        return 'crash', checkpoint[1] if checkpoint else 'session.unclean'
    found = cause(log)
    if found:
        return found
    if re.search(r'startup\.fail|startup\.incomplete|missing fonts', log):
        return 'startup', 'startup.fail'
    return 'other', 'other'


def normalize(data):
    if not isinstance(data, dict) or set(data) not in (FIELDS, LEGACY_FIELDS) or any(not isinstance(v, str) for v in data.values()):
        raise ValueError('schema')
    if any(len(data[k]) > n for k, n in [('version', 128), ('build', 128), ('hardware', 8192), ('screen', 256), ('error', 2048)]):
        raise ValueError('field size')
    installation = data.get('installation_id', '')
    if set(data) == FIELDS and (not re.fullmatch(r'[0-9a-f]{32}', installation) or data['category'] not in CATEGORIES):
        raise ValueError('installation/category')
    category, point = classify(data)
    screens = {'Home', 'Player', 'Search', 'Settings', 'Navigation', 'startup/unknown'}
    clean = {k: (v if v in screens else 'startup/unknown') if k == 'screen' else scrub(v) for k, v in data.items() if k in LEGACY_FIELDS}
    clean['category'] = category
    # Pontos controlados preservam LOGIN_REQUIRED/TLS sem deixar credenciais no log.
    if category in ('network', 'youtube'):
        clean['error'] = point
    body = json.dumps(clean, ensure_ascii=False, sort_keys=True)
    if len(body.encode()) > MAX_BODY:
        raise ValueError('size')
    installation_hash = hashlib.sha256(installation.encode()).hexdigest() if installation else None
    identity = hashlib.sha256(((installation_hash or '') + '\n' + body).encode()).hexdigest()
    signature = hashlib.sha256((category + '\n' + point + '\n' + clean['version']).encode()).hexdigest()
    return clean, body, installation_hash, identity, signature, point


class Store:
    def __init__(self, directory, clock=time.time, quota=QUOTA, github=None):
        self.path = Path(directory)
        self.path.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.clock, self.quota, self.github = clock, quota, github
        self.lock, self.delivery_lock = threading.Lock(), threading.Lock()
        self.salt, self.ips = secrets.token_bytes(32), {}
        self.db = sqlite3.connect(self.path / 'index.sqlite3', check_same_thread=False)
        self.db.execute('PRAGMA journal_mode=DELETE')
        self.db.execute('CREATE TABLE IF NOT EXISTS reports (id TEXT PRIMARY KEY, signature TEXT, code TEXT, created REAL, body TEXT, issue INTEGER)')
        self.db.execute('CREATE TABLE IF NOT EXISTS days (day INTEGER PRIMARY KEY, count INTEGER)')
        self.db.execute('CREATE TABLE IF NOT EXISTS counts (day INTEGER, category TEXT, version TEXT, total INTEGER, PRIMARY KEY(day,category,version))')
        self.db.execute('CREATE TABLE IF NOT EXISTS triage (repo TEXT, signature TEXT, revision TEXT, issue INTEGER, PRIMARY KEY(repo,signature))')
        self.db.execute('CREATE TABLE IF NOT EXISTS summary (repo TEXT PRIMARY KEY, day INTEGER, issue INTEGER)')
        columns = {row[1] for row in self.db.execute('PRAGMA table_info(reports)')}
        for name, kind in [('installation', 'TEXT'), ('category', 'TEXT'), ('point', 'TEXT'), ('version', 'TEXT'), ('day', 'INTEGER')]:
            if name not in columns:
                self.db.execute('ALTER TABLE reports ADD COLUMN ' + name + ' ' + kind)
        # Relatórios legados não têm ID: contam no resumo, nunca como instalação distinta.
        for identity, created, raw in self.db.execute('SELECT id,created,body FROM reports WHERE category IS NULL').fetchall():
            try:
                data, body, _, _, signature, point = normalize(json.loads(raw))
            except (ValueError, TypeError):
                self.db.execute('DELETE FROM reports WHERE id=?', (identity,))
                continue
            day = int(created // 86400)
            retained = body if data['category'] in ('crash', 'startup') else ''
            self.db.execute('UPDATE reports SET signature=?,body=?,installation=NULL,category=?,point=?,version=?,day=?,issue=NULL WHERE id=?',
                            (signature, retained, data['category'], point, data['version'], day, identity))
            self.count(day, data['category'], data['version'])
        self.db.commit()
        with self.lock:
            self.prune()

    def count(self, day, category, version):
        self.db.execute('INSERT INTO counts VALUES (?,?,?,1) ON CONFLICT(day,category,version) DO UPDATE SET total=total+1', (day, category, version))

    def prune(self):
        now = self.clock()
        self.db.execute('DELETE FROM reports WHERE created < ?', (now - RETENTION,))
        self.db.execute('DELETE FROM counts WHERE day < ?', (int(now // 86400) - 30,))
        self.db.execute('DELETE FROM days WHERE day < ?', (int(now // 86400) - 2,))
        self.db.execute('DELETE FROM triage WHERE NOT EXISTS (SELECT 1 FROM reports WHERE reports.signature=triage.signature)')
        self.db.commit()
        self.db.execute('VACUUM')
        while (self.path / 'index.sqlite3').stat().st_size > self.quota:
            row = self.db.execute('SELECT id FROM reports ORDER BY created, rowid LIMIT 1').fetchone()
            if row is None:
                break
            self.db.execute('DELETE FROM reports WHERE id=?', row)
            self.db.commit()
            self.db.execute('VACUUM')

    def accept(self, ip, raw):
        if len(raw.encode() if isinstance(raw, str) else raw) > MAX_BODY:
            raise ValueError('size')
        data, body, installation, identity, signature, point = normalize(json.loads(raw))
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
            eligible = data['category'] in ('crash', 'startup')
            self.db.execute('INSERT INTO reports VALUES (?,?,?,?,?,NULL,?,?,?,?,?)',
                            (identity, signature, code, now, body if eligible else '', installation if eligible else None,
                             data['category'], point, data['version'], day))
            self.count(day, data['category'], data['version'])
            self.db.commit()
            self.prune()
            return 200, {'code': code}

    def deliver(self):
        if self.github is None:
            return
        # Um worker serializa todas as mutações; nenhuma rede segura a trava do HTTP/banco.
        with self.delivery_lock:
            try:
                if not self.github.is_private():
                    return
                self.deliver_triage()
                self.deliver_summary()
            except (OSError, ValueError, urllib.error.URLError):
                return

    def deliver_triage(self):
        with self.lock:
            self.prune()
            groups = self.db.execute("SELECT signature,COUNT(*),COUNT(DISTINCT installation) FROM reports WHERE category IN ('crash','startup') GROUP BY signature HAVING COUNT(DISTINCT installation)>=3 OR signature IN (SELECT signature FROM triage WHERE repo=?)", (self.github.repo,)).fetchall()
        for signature, count, installations in groups:
            with self.lock:
                row = self.db.execute('SELECT code,body,point FROM reports WHERE signature=? ORDER BY created DESC,rowid DESC LIMIT 1', (signature,)).fetchone()
                prior = self.db.execute('SELECT revision FROM triage WHERE repo=? AND signature=?', (self.github.repo, signature)).fetchone()
                count, installations = self.db.execute('SELECT COUNT(*),COUNT(DISTINCT installation) FROM reports WHERE signature=?', (signature,)).fetchone()
                # Um recebimento pode ter podado o grupo enquanto a rede estava fora da trava.
                if row is None or (prior is None and installations < 3):
                    continue
            code, raw, point = row
            data = json.loads(raw)
            body = '<!-- tube360-signature:' + signature + ' -->\n'
            body += f"Reports (30 days): {count}\nDistinct installations (30 days): {installations}\nLatest receipt: {code}\nPoint: {point}\n\n"
            # Campos e linhas são dados literais, inclusive quebras/Markdown enviados pelo cliente.
            metadata = '\n'.join(k + ': ' + data[k] for k in ('category', 'version', 'build', 'hardware', 'screen', 'error'))
            body += '\n'.join('    ' + line for line in metadata.splitlines())
            body += '\n\nLatest sanitized log:\n' + '\n'.join('    ' + line for line in data['log'].splitlines())
            body = body[:60000]
            revision = hashlib.sha256(body.encode()).hexdigest()
            if prior and prior[0] == revision:
                continue
            try:
                version = re.sub(r'[^A-Za-z0-9_.+-]', '?', data['version'])
                issue = self.github.upsert('tube360-signature:' + signature, (data['category'].title() + ' ' + version + ': ' + point)[:160], body)
            except (OSError, ValueError, urllib.error.URLError):
                continue
            with self.lock:
                self.db.execute('INSERT INTO triage VALUES (?,?,?,?) ON CONFLICT(repo,signature) DO UPDATE SET revision=excluded.revision,issue=excluded.issue', (self.github.repo, signature, revision, issue))
                self.db.commit()

    def deliver_summary(self):
        day = int(self.clock() // 86400) - 1
        with self.lock:
            prior = self.db.execute('SELECT day FROM summary WHERE repo=?', (self.github.repo,)).fetchone()
            if prior and prior[0] >= day:
                return
            rows = self.db.execute('SELECT day,category,version,total FROM counts WHERE day<=? ORDER BY day,category,version', (day,)).fetchall()
        if not rows:
            return
        through = time.strftime('%Y-%m-%d', time.gmtime(day * 86400))
        body = '<!-- tube360-daily-summary -->\n<!-- tube360-summary-through:' + through + ' -->\n'
        body += 'Completed UTC days through ' + through + '\n\nDate | Category | Version | Reports\n--- | --- | --- | ---\n'
        # Cada versão é texto, sem Markdown/menções vindas do cliente.
        for date, category, version, total in rows:
            version = re.sub(r'[^A-Za-z0-9_.+-]', '?', version)
            body += f"{time.strftime('%Y-%m-%d', time.gmtime(date * 86400))} | {category} | {version} | {total}\n"
        # O corpo do GitHub é limitado: prioriza dias recentes sem recortar uma linha.
        lines = body.splitlines()
        header, recent, used = lines[:6], [], len('\n'.join(lines[:6]))
        for line in reversed(lines[6:]):
            if used + len(line) + 1 > 60000:
                break
            recent.append(line); used += len(line) + 1
        lines = header + list(reversed(recent))
        issue = self.github.upsert('tube360-daily-summary', 'Tube360 daily error summary', '\n'.join(lines))
        with self.lock:
            self.db.execute('INSERT INTO summary VALUES (?,?,?) ON CONFLICT(repo) DO UPDATE SET day=excluded.day,issue=excluded.issue', (self.github.repo, day, issue))
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
            raise ValueError('REPORT_TRIAGE_REPO must be owner/repository')
        self.token, self.repo, self.base = token, repo, base

    def request(self, method, path, data=None):
        raw = None if data is None else json.dumps(data).encode()
        req = urllib.request.Request(self.base + path, data=raw, method=method, headers={
            'Authorization': 'Bearer ' + self.token, 'Accept': 'application/vnd.github+json',
            'X-GitHub-Api-Version': '2022-11-28', 'User-Agent': 'Tube360-error-receiver',
            'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=8) as response:
            return json.load(response)

    def is_private(self):
        return self.request('GET', '/repos/' + self.repo).get('private') is True

    def upsert(self, identifier, title, body):
        marker = '<!-- ' + identifier + ' -->'
        prefix = '/repos/' + self.repo + '/issues'
        page = 1
        while True:
            rows = self.request('GET', prefix + '?state=all&labels=crash-report&per_page=100&page=' + str(page))
            for row in rows:
                if (row.get('body') or '').splitlines()[:1] == [marker] and 'pull_request' not in row:
                    # PATCH idempotente substitui o log anterior, sem um comentário por relatório.
                    if row.get('body') != body or row.get('title') != title:
                        self.request('PATCH', prefix + '/' + str(row['number']), {'title': title, 'body': body})
                    return row['number']
            if len(rows) < 100:
                break
            page += 1
        return self.request('POST', prefix, {'title': title, 'body': body, 'labels': ['crash-report']})['number']


def configured_github(environ=None):
    env = os.environ if environ is None else environ
    repo, token = env.get('REPORT_TRIAGE_REPO', '').strip(), env.get('GITHUB_TOKEN', '').strip()
    return GitHub(token, repo) if repo and token else None


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
    github = configured_github()
    server = Server((os.environ.get('REPORT_BIND', '127.0.0.1'), int(os.environ.get('REPORT_PORT', '8080'))), Handler)
    server.trusted_proxies = set(filter(None, os.environ.get('REPORT_TRUSTED_PROXIES', '').split(',')))
    server.store = Store(os.environ.get('REPORT_DATA', './data'), github=github)
    threading.Thread(target=server.store.work, daemon=True).start()
    server.serve_forever()


if __name__ == '__main__':
    main()
