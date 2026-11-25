#!/usr/bin/env python3
"""Prova F26 por XBDM, sem controle: usa e limpa apenas Games/Tube360 Teste; nunca reinicia o console."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import ipaddress
import json
import os
from pathlib import Path, PurePosixPath
import re
import signal
import socket
import subprocess
import sys
import time
from urllib.parse import urlsplit
from urllib.request import urlopen
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[2]
TOOL = Path('/home/pedrohenriquequadro/Documentos/VS/forcepush-360/tools/xbdm.py')
AURORA = r'Usb0:\Aurora\Aurora.xex'
TEST_FOLDER = 'Tube360 Teste'


def remaining(deadline):
    value = deadline - time.monotonic()
    if value <= 0:
        raise TimeoutError('prazo da etapa esgotado')
    return value


def digest(data):
    return hashlib.sha256(data).hexdigest()


def entries(lines):
    out = []
    for line in lines:
        match = re.search(r'(?:^|\s)name="([^"]+)"', line)
        if not match:
            raise ValueError('dirlist ilegível: ' + line)
        name = match[1]
        if name in ('.', '..') or any(c in name for c in '\\/:"') or any(ord(c) < 32 for c in name):
            raise ValueError('nome recusado em dirlist: ' + repr(name))
        out.append((name, bool(re.search(r'(?:^|\s)directory(?:\s|$)', line))))
    return out


class DeadlineSocket:
    def __init__(self, sock, deadline):
        self.sock, self.deadline = sock, deadline

    def recv(self, count):
        self.sock.settimeout(remaining(self.deadline))
        return self.sock.recv(count)

    def sendall(self, data):
        self.sock.settimeout(remaining(self.deadline))
        return self.sock.sendall(data)

    def close(self):
        self.sock.close()


class Remote:
    def __init__(self, args, output):
        spec = importlib.util.spec_from_file_location('f26_xbdm', args.xbdm_tool)
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.module.PORT = args.xbdm_port
        self.host, self.timeout, self.output = args.host, args.io_timeout, output

    def call(self, label, operation, deadline):
        limit = time.monotonic() + min(self.timeout, remaining(deadline))
        sock = socket.create_connection((self.host, self.module.PORT), timeout=remaining(limit))
        client = self.module.Xbdm.__new__(self.module.Xbdm)
        client.s = DeadlineSocket(sock, limit)
        try:
            code, text = client._status()
            if code != 201:
                raise RuntimeError(f'saudação XBDM: {code} {text}')
            result = operation(client)
            with (self.output / 'xbdm-io.log').open('a') as log:
                log.write(f'{datetime.now(timezone.utc).isoformat()} OK {label}\n')
            return result
        finally:
            client.close()

    def ls(self, path, deadline):
        return entries(self.call('dirlist ' + path, lambda x: x.dirlist(path), deadline))

    def cmd(self, text, deadline):
        code, result = self.call(text, lambda x: x.cmd(text), deadline)
        if code != 200:
            raise RuntimeError(f'{text}: resposta inesperada {code} {result}')
        return code, result

    def get(self, path, deadline):
        # O limite é local ao leitor, sem mudar o módulo compartilhado do XBDM.
        def get(client):
            original = client._exact
            def bounded(size):
                if size > 64 * 1024 * 1024:
                    raise ValueError('arquivo XBDM excede 64 MiB: ' + path)
                return original(size)
            client._exact = bounded
            return client.getfile(path)
        return self.call('getfile ' + path, get, deadline)

    def put(self, path, data, deadline):
        self.call('sendfile ' + path + ' bytes=' + str(len(data)), lambda x: x.sendfile(path, data), deadline)

    def launch(self, path, deadline):
        self.call('magicboot ' + path, lambda x: x.magicboot(path), deadline)

    def missing(self, error):
        return isinstance(error, self.module.XbdmError) and re.search(r'\b(?:402|404)\b', str(error)) is not None


class Runner:
    def __init__(self, args):
        self.args = args
        self.output = args.output.resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.log = (self.output / 'runner.log').open('w', buffering=1)
        self.report = dict(success=False, steps=[], children=[], remote_root=None, console_touched=False,
                           folder_removed=False, aurora_command_sent=False, evidence=str(self.output),
                           xbdm_host=args.host, xbdm_port=args.xbdm_port,
                           started=datetime.now(timezone.utc).isoformat())
        self.children, self.handles = [], []
        self.remote = Remote(args, self.output)
        self.root, self.owned = None, False
        self.previous_scenario = None
        self.last, self.base, self.udp_offset = {}, {}, 0
        self.active = 'preflight'

    def emit(self, text):
        print(text, flush=True)
        self.log.write(text + '\n')

    def step(self, name, timeout, function):
        self.active = name
        self.emit(f'INICIO [{name}] timeout={timeout:g}s')
        start = time.monotonic()
        try:
            proof = function(start + timeout)
        except (Exception, KeyboardInterrupt) as error:
            self.report['steps'].append(dict(name=name, passed=False, error=str(error), seconds=time.monotonic()-start))
            self.emit(f'FALHOU [{name}] {type(error).__name__}: {error}')
            raise
        self.report['steps'].append(dict(name=name, passed=True, proof=proof, seconds=time.monotonic()-start))
        self.emit(f'PASSOU [{name}] {proof}')
        return proof

    def local_preflight(self, deadline):
        kit = self.args.directory.resolve()
        self.kit = kit
        self.meta = json.loads((kit / 'kit.json').read_text())
        origin = urlsplit(self.meta['base'])
        if not self.meta.get('test_only') or not self.meta.get('auto_file_trigger') or self.meta['build_n'] != 2601:
            raise ValueError('é necessário o kit F26 automático #2601/#2602/#2603 regenerado')
        if origin.scheme != 'http' or origin.port != 8266 or not origin.hostname:
            raise ValueError('o kit precisa da origem HTTP de teste na porta 8266')
        self.log_host = self.args.log_host or origin.hostname
        ipaddress.IPv4Address(self.log_host)
        with zipfile.ZipFile(next((kit / 'install').glob('*.zip'))) as archive:
            files = {}
            for name in archive.namelist():
                parts = PurePosixPath(name).parts
                if len(parts) < 2 or parts[0] != TEST_FOLDER or any(p in ('.', '..') or any(c in p for c in '\\:"') for p in parts):
                    raise ValueError('membro de ZIP fora da pasta de teste: ' + name)
                files['/'.join(parts[1:])] = archive.read(name)
        manifest = json.loads(files['manifest.json'])
        if not manifest['test_only'] or manifest['layout'] != 'launcher+app' or set(files) != set(manifest['files']) | {'manifest.json'}:
            raise ValueError('pacote não é o layout de teste esperado')
        for name, expected in manifest['files'].items():
            if digest(files[name]) != expected:
                raise ValueError('hash incorreto no ZIP: ' + name)
        self.payloads, self.documents, self.signatures, self.manifests = {}, {}, {}, {}
        key = self.meta['public_key'].encode()
        for label, build in [('good', 2602), ('fail', 2603)]:
            raw = (kit / f'public/{label}.json').read_bytes()
            sig = (kit / f'public/{label}.json.sig').read_bytes()
            payload = (kit / f'public/{label}.xex').read_bytes()
            doc = json.loads(raw)
            if (doc['build'] != build or doc['size'] != len(payload) or doc['sha256'] != digest(payload)
                    or doc['url'] != self.meta['base'] + label + '.xex' or not re.fullmatch(rb'[0-9a-f]{128}\n?', sig)):
                raise ValueError('candidato/manifesto inconsistente: ' + label)
            self.payloads[label], self.documents[label], self.signatures[label], self.manifests[label] = payload, raw, sig, doc
        for payload in [files['tube360.xex'], *self.payloads.values()]:
            if key not in payload or b'TEST auto-download requested build=' not in payload:
                raise ValueError('XEX sem chave/gatilho descartável')
        if key not in files['default.xex']:
            raise ValueError('lançador usa outra chave')
        files.update({'settings.txt': b'lang=pt\nserver=off\nsound=tv\n',
                      'net_log.txt': f'{self.log_host} {self.args.udp_port}\n'.encode(),
                      'update-auto.txt': b'on\n'})
        self.files = files
        for name, data in files.items():
            destination = self.output / 'upload' / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        remaining(deadline)
        return f'kit test_only; N=2601, good=2602, fail=2603; {len(files)} arquivos/hash conferidos; update-auto.txt presente'

    def remote_preflight(self, deadline):
        aurora = self.remote.ls(r'Usb0:\Aurora', deadline)
        if not any(n.casefold() == 'aurora.xex' and not d for n, d in aurora):
            raise RuntimeError('Aurora.xex não encontrado; nenhum título será lançado')
        # Hdd1: lista mas recusa criar arquivo (413/414) neste console; HDD: é o mesmo disco e aceita.
        games = self.remote.ls('HDD:\\Games', deadline)
        prefix = 'HDD:'
        self.root = prefix + '\\Games\\' + TEST_FOLDER
        self.existing = any(n.casefold() == TEST_FOLDER.casefold() and d for n, d in games)
        self.report['remote_root'] = self.root
        return f'dirlist confirmou Aurora.xex e {prefix}\\Games; pasta temporária={self.root}; preexistente={self.existing}'

    def start_child(self, name, command, ready, deadline):
        path = self.output / (name + '.log')
        handle = path.open('wb')
        self.handles.append(handle)
        env = {**os.environ, 'HOME': str(self.output / 'home'), 'XDG_CACHE_HOME': str(self.output / 'cache'),
               'PYTHONUNBUFFERED': '1', 'PYTHONUSERBASE': str(Path.home() / '.local')}
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=handle, stderr=subprocess.STDOUT, start_new_session=True)
        record = dict(name=name, pid=process.pid, stopped=False, log=str(path))
        self.children.append((process, record))
        self.report['children'].append(record)
        while ready not in path.read_text(errors='replace'):
            if process.poll() is not None:
                raise RuntimeError(f'{name} PID {process.pid} terminou: ' + path.read_text(errors='replace')[-1000:])
            time.sleep(min(0.05, remaining(deadline)))
        if process.poll() is not None:
            raise RuntimeError(f'{name} encerrou após anunciar início')
        return f'PID={process.pid}; ' + next(line for line in path.read_text(errors='replace').splitlines() if ready in line)

    def children_start(self, deadline):
        server = self.start_child('server', [sys.executable, '-u', str(ROOT / 'tests/update/console-kit.py'), 'serve',
                                  '--directory', str(self.kit)], 'HTTP local descartável:', deadline)
        udp = self.start_child('udp', [sys.executable, '-u', str(ROOT / 'tests/update/udp-log.py'), '--port', str(self.args.udp_port)],
                               'Ctrl+C encerra', deadline)
        self.previous_scenario = (self.kit / 'public/scenario.txt').read_bytes()
        self.select('good')
        with urlopen('http://127.0.0.1:8266/app.json', timeout=min(3, remaining(deadline))) as response:
            if response.read() != self.documents['good']:
                raise RuntimeError('servidor local não devolveu o manifesto good exato')
        return server + ' | ' + udp + ' | HTTP app.json = good assinado do kit'

    def select(self, scenario):
        path = self.kit / 'public/scenario.txt'
        temporary = path.with_name('scenario.runner-' + self.output.name + '.tmp')
        temporary.write_text(scenario + '\n')
        os.replace(temporary, path)

    def confined(self, path):
        root = self.root.casefold()
        if path.casefold() != root and not path.casefold().startswith(root + '\\'):
            raise ValueError('operação fora da pasta de teste: ' + path)

    def walk(self, directory, deadline):
        self.confined(directory)
        for name, directory_flag in self.remote.ls(directory, deadline):
            full = directory + '\\' + name
            yield full, name, directory_flag
            if directory_flag:
                yield from self.walk(full, deadline)

    def remove_tree(self, directory, deadline):
        self.confined(directory)
        for name, is_directory in self.remote.ls(directory, deadline):
            child = directory + '\\' + name
            self.confined(child)
            if is_directory:
                self.remove_tree(child, deadline)
            else:
                self.remote.cmd(f'delete name="{child}"', deadline)
        self.remote.cmd(f'delete name="{directory}" dir', deadline)

    def snapshot(self, label, deadline, strict=True):
        destination = self.output / label
        destination.mkdir(exist_ok=True)
        errors, saved = [], []
        for remote, name, is_directory in self.walk(self.root, deadline):
            if is_directory or (not name.lower().endswith('.log') and name != 'update-app-state.json'):
                continue
            relative = remote[len(self.root)+1:].replace('\\', '/')
            try:
                data = self.remote.get(remote, deadline)
                path = destination / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
                saved.append(relative)
            except Exception as error:
                errors.append(f'{relative}: {error}')
        (destination / 'snapshot.json').write_text(json.dumps(dict(saved=saved, errors=errors), indent=2)+'\n')
        if strict and errors:
            raise RuntimeError('não foi possível arquivar todos os logs: ' + '; '.join(errors))
        return ', '.join(saved) or 'nenhum log existente'

    def aurora(self, deadline):
        self.report['console_touched'] = True
        self.remote.launch(AURORA, deadline)
        self.report['aurora_command_sent'] = True
        while True:
            try:
                if any(n.casefold() == 'aurora.xex' and not d for n, d in self.remote.ls(r'Usb0:\Aurora', deadline)):
                    return f'magicboot title="{AURORA}" enviado; XBDM reconectado (201), Aurora.xex listado'
            except Exception:
                remaining(deadline)
            time.sleep(min(self.args.poll, remaining(deadline)))

    def install(self, deadline):
        self.aurora(deadline)
        if self.existing:
            archived = self.snapshot('preexisting', deadline)
            self.remove_tree(self.root, deadline)
            self.emit('INFO pasta de teste anterior removida após arquivar: ' + archived)
        # Xbdm.mkdir ignora 413; aqui a resposta precisa ser sucesso real.
        try:
            self.remote.cmd(f'mkdir name="{self.root}"', deadline)
        except self.remote.module.XbdmError as error:
            if not self.root.startswith('Hdd1:') or not re.search(r'\b414\b', str(error)):
                raise
            self.root = 'HDD:\\Games\\' + TEST_FOLDER
            self.report['remote_root'] = self.root
            self.emit('INFO mkdir Hdd1 retornou 414; usando ' + self.root)
            games = self.remote.ls('HDD:\\Games', deadline)
            if any(n.casefold() == TEST_FOLDER.casefold() and d for n, d in games):
                self.snapshot('preexisting-HDD', deadline)
                self.remove_tree(self.root, deadline)
            self.remote.cmd(f'mkdir name="{self.root}"', deadline)
        self.owned = True
        folders = sorted({str(parent) for name in self.files for parent in PurePosixPath(name).parents if str(parent) != '.'},
                         key=lambda x: (x.count('/'), x))
        for directory in folders:
            self.remote.cmd(f'mkdir name="{self.root}\\{directory.replace(chr(47), chr(92))}"', deadline)
        for name, data in self.files.items():
            self.remote.put(self.root + '\\' + name.replace('/', '\\'), data, deadline)
        for name in ('default.xex', 'tube360.xex'):
            if digest(self.remote.get(self.root + '\\' + name, deadline)) != digest(self.files[name]):
                raise RuntimeError('leitura após put divergiu: ' + name)
        return f'sendfile OK para {len(self.files)} arquivos; getfile/SHA256 dos dois XEX = ZIP N; settings/net_log/update-auto gravados'

    def optional_get(self, name, deadline):
        try:
            return self.remote.get(self.root + '\\' + name, deadline)
        except Exception as error:
            if self.remote.missing(error):
                return b''
            raise

    def launch_test(self, deadline):
        self.base = {}
        for name in ('update.log', 'launcher.log'):
            try:
                self.base[name] = self.optional_get(name, deadline)
            except (OSError, self.remote.module.XbdmError):
                # Sem baseline confiável, só o UDP novo poderá provar eventos dessa origem.
                self.base[name] = None
        self.udp_offset = (self.output / 'udp.log').stat().st_size
        self.remote.launch(self.root + '\\default.xex', deadline)
        self.report['console_touched'] = True

    def observe(self, deadline):
        observation = {}
        for name in ('update.log', 'launcher.log'):
            before = self.base.get(name, b'')
            observation[name] = ''
            if before is None:
                continue
            try:
                data = self.optional_get(name, deadline)
            except (OSError, self.remote.module.XbdmError) as error:
                observation['network_error'] = str(error)
                continue
            if before and not data.startswith(before):
                raise RuntimeError(name + ' foi truncado/substituído; não aceito evidência antiga')
            observation[name] = data[len(before):].decode('utf-8', errors='replace')
        try:
            raw = self.optional_get('update-app-state.json', deadline)
        except (OSError, self.remote.module.XbdmError) as error:
            raw = b''
            observation['network_error'] = str(error)
        observation['journal_raw'] = raw.decode('utf-8', errors='replace')
        try:
            observation['journal'] = json.loads(raw)
        except (ValueError, UnicodeError):
            observation['journal'] = {}
        udp = (self.output / 'udp.log').read_bytes()[self.udp_offset:].decode('utf-8', errors='replace')
        observation['UDP'] = '\n'.join(line for line in udp.splitlines() if line.startswith(self.args.host + ' '))
        self.last = observation
        return observation

    def event(self, observation, text):
        pattern = re.compile(re.escape(text) + r'(?=\s|\x00|$)')
        for source in ('update.log', 'launcher.log', 'UDP'):
            for line in observation[source].splitlines():
                if pattern.search(line):
                    return source + ': ' + line
        return ''

    def journal(self, observation, phase, label):
        state = observation['journal']
        return (state.get('phase') == phase and state.get('highest') == self.manifests[label]['build']
                and state.get('bytes', '').encode() == self.documents[label]
                and state.get('signature', '').encode() == self.signatures[label])

    def wait_phase(self, phase, deadline):
        while True:
            try:
                current = self.observe(deadline)
                e = lambda text: self.event(current, text)
                proof = ''
                if phase == 'N-staged':
                    if self.journal(current, 'staged', 'good') and e('TEST auto-download requested build=2601'):
                        proof = e('staged build=2602')
                elif phase == 'good-confirmed':
                    if self.journal(current, 'good', 'good') and e('TEST app started build=2602') and e('journal gravacao fase=trial ok'):
                        proof = e('boot confirmed build=2602')
                elif phase == 'fail-staged':
                    if self.journal(current, 'staged', 'fail') and e('TEST auto-download requested build=2602'):
                        proof = e('staged build=2603')
                elif phase == 'fail-unconfirmed':
                    if e('boot confirmed build=2603'):
                        raise RuntimeError('o candidato de falha confirmou o boot indevidamente')
                    if self.journal(current, 'trial', 'fail') and e('TEST app started build=2603'):
                        proof = e('TEST boot confirmation intentionally withheld build=2603')
                elif phase == 'rollback':
                    if self.journal(current, 'rollback', 'fail') and e('TEST app started build=2602') and e('TEST boot healthy build=2602'):
                        proof = e('rollback fim: ' + 'tube360.xex')
                if proof:
                    target = {'good-confirmed': 'good', 'fail-unconfirmed': 'fail', 'rollback': 'good'}.get(phase)
                    if target and digest(self.remote.get(self.root + '\\tube360.xex', deadline)) != self.manifests[target]['sha256']:
                        raise RuntimeError('XEX instalado diverge do hash de ' + target)
                    self.snapshot(phase, deadline, strict=False)
                    state = current['journal']
                    return proof + f' | journal phase={state["phase"]} highest={state["highest"]}' + (' | SHA256 instalado=' + self.manifests[target]['sha256'] if target else '')
            except (OSError, self.remote.module.XbdmError) as error:
                self.last['network_error'] = str(error)
            if time.monotonic() >= deadline:
                (self.output / ('timeout-' + phase + '.json')).write_text(json.dumps(self.last, ensure_ascii=False, indent=2)+'\n')
                tail = '\n'.join((self.last.get('update.log', '') + self.last.get('launcher.log', '') + self.last.get('UDP', '')).splitlines()[-8:])
                raise TimeoutError('sem prova nova para ' + phase + '; último erro=' + self.last.get('network_error', 'nenhum') + '\n' + tail)
            time.sleep(min(self.args.poll, remaining(deadline)))

    def phase(self, name, deadline, scenario=None):
        if scenario:
            # O app antigo ainda pode estar consultando após confirmar; libere-o antes de trocar o canal.
            self.aurora(deadline)
            self.select(scenario)
        self.launch_test(deadline)
        return self.wait_phase(name, deadline)

    def console_cleanup(self, deadline):
        proof = self.aurora(deadline)
        if self.owned:
            archived = self.snapshot('final', deadline)
            self.remove_tree(self.root, deadline)
            if any(n.casefold() == TEST_FOLDER.casefold() and d for n, d in self.remote.ls(self.root.rsplit('\\', 1)[0], deadline)):
                raise RuntimeError('a pasta de teste ainda existe depois de delete dir')
            self.report['folder_removed'] = True
            proof += ' | logs arquivados: ' + archived + ' | delete dir confirmado por dirlist'
        # O título já foi liberado antes dos deletes; envia também o retorno final solicitado.
        return proof + ' | ' + self.aurora(deadline)

    def stop_children(self, deadline):
        proof, errors = [], []
        for process, record in reversed(self.children):
            try:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=min(2, remaining(deadline)))
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=remaining(deadline))
            except Exception as error:
                errors.append(f'{record["name"]}: {error}')
                if process.poll() is None:
                    process.kill()
                    try:
                        process.wait(timeout=0.5)
                    except subprocess.TimeoutExpired:
                        pass
            record.update(stopped=process.poll() is not None, returncode=process.returncode)
            proof.append(f'{record["name"]} PID={process.pid} encerrado rc={process.returncode}')
        if self.previous_scenario is not None:
            (self.kit / 'public/scenario.txt').write_bytes(self.previous_scenario)
        if errors or any(not record['stopped'] for _, record in self.children):
            raise RuntimeError('encerramento incompleto: ' + '; '.join(errors + proof))
        return ' | '.join(proof) or 'nenhum processo próprio iniciado'

    def run(self):
        ok = False
        try:
            self.step('kit-local', 15, self.local_preflight)
            self.step('XBDM-preflight', self.args.step_timeout, self.remote_preflight)
            self.step('servidor-e-UDP', 15, self.children_start)
            self.step('instalar-N', self.args.install_timeout, self.install)
            for phase in ('N-staged', 'good-confirmed', 'fail-staged', 'fail-unconfirmed', 'rollback'):
                self.step(phase, self.args.step_timeout,
                          lambda deadline, phase=phase: self.phase(phase, deadline, 'fail' if phase == 'fail-staged' else None))
            ok = True
        except (Exception, KeyboardInterrupt):
            pass
        finally:
            if self.report['console_touched']:
                try:
                    self.step('arquivar-limpar-Aurora', self.args.cleanup_timeout, self.console_cleanup)
                except (Exception, KeyboardInterrupt):
                    ok = False
            try:
                self.step('encerrar-PIDs-proprios', 8, self.stop_children)
            except (Exception, KeyboardInterrupt):
                ok = False
            for handle in self.handles:
                handle.close()
            self.report['success'] = ok
            (self.output / 'report.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2)+'\n')
            self.emit(('PASSOU' if ok else 'FALHOU') + ' [resultado] evidências=' + str(self.output))
            self.log.close()
        return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, help='IPv4 do XBDM; nunca faz descoberta de rede')
    parser.add_argument('--xbdm-port', type=int, default=730)
    parser.add_argument('--xbdm-tool', type=Path, default=TOOL)
    parser.add_argument('--directory', type=Path, default=ROOT / '.work/update-console')
    parser.add_argument('--udp-port', type=int, default=7466)
    parser.add_argument('--log-host', help='IP local para UDP; padrão = notebook na origem do kit')
    parser.add_argument('--step-timeout', type=float, default=120)
    parser.add_argument('--install-timeout', type=float, default=240)
    parser.add_argument('--cleanup-timeout', type=float, default=90)
    parser.add_argument('--io-timeout', type=float, default=8)
    parser.add_argument('--poll', type=float, default=1)
    parser.add_argument('--output', type=Path, default=ROOT / '.work/console-runs' / (datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6]))
    args = parser.parse_args()
    try:
        ipaddress.IPv4Address(args.host)
    except ValueError:
        parser.error('--host precisa ser um IPv4 explícito')
    if not all(0 < n <= 3600 for n in (args.step_timeout, args.install_timeout, args.cleanup_timeout, args.io_timeout, args.poll)):
        parser.error('timeouts e poll devem ser positivos e de no máximo 3600 segundos')
    def interrupted(signum, frame):
        raise InterruptedError('interrompido pelo sinal ' + str(signum))
    signal.signal(signal.SIGTERM, interrupted)
    try:
        return Runner(args).run()
    except Exception as error:
        print(f'FALHOU [inicializacao] {type(error).__name__}: {error}', flush=True)
        return 1


if __name__ == '__main__':
    sys.exit(main())
