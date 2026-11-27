#!/usr/bin/env python3
"""Controle pelo celular (#46) no app de PC de verdade: abre o painel pelas Configurações (captura com endereço e código), depois
pareia por HTTP com o código fixo da bancada, manda ações e confere o log do app. Uso: tests/remote/e2e.py [pasta de saída]"""
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix='remote-e2e-'))
out.mkdir(parents=True, exist_ok=True)
CODE = '246810'


def app(name, on, keys, shot_ms, extra=None):
    assets = out / name
    assets.mkdir(exist_ok=True)
    for font in (ROOT / 'client/assets').glob('*.ttf'):
        shutil.copyfile(font, assets / font.name)
    (assets / 'settings.txt').write_text(f'lang=en\nserver=off\nsound=tv\ncols=4\nphone_remote={"on" if on else "off"}\n')
    env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(assets / 'home'), XDG_CONFIG_HOME=str(assets / 'config'), XDG_CACHE_HOME=str(assets / 'cache'),
               SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy', XT_CAST_DISABLE='1', XT_ASSETS=str(assets), XT_SCREEN='settings',
               XT_REMOTE_CODE=CODE, XT_KEYS=keys, XT_KEYS_MS='180', XT_SHOT=str(assets / 'screen.ppm'), XT_SHOT_MS=str(shot_ms))
    env.update(extra or {})
    log = (assets / 'app.log').open('w')
    return subprocess.Popen([str(ROOT / 'client/build/tube360')], env=env, stdout=log, stderr=subprocess.STDOUT), assets


def call(method, path, body=None, token='pair', port=8135):
    req = urllib.request.Request(f'http://127.0.0.1:{port}{path}', data=body.encode() if body is not None else None, method=method,
                                 headers={'X-T360': token} if token else {})
    try:
        with urllib.request.urlopen(req, timeout=3) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


def wait_port(proc, tries=100):
    for _ in range(tries):
        if proc.poll() is not None:
            return False
        try:
            if call('GET', '/', token=None)[0] == 200:
                return True
        except Exception:
            time.sleep(0.1)
    return False


fails = []
def check(cond, what):
    print(('ok    ' if cond else 'FALHA ') + what)
    if not cond:
        fails.append(what)

# 1) a linha das Configurações liga a opção e abre o painel com o código
proc, assets = app('panel', False, ']]]]]DDDA', 4200)
proc.wait(timeout=60)
log = (assets / 'app.log').read_text()
check('phone_remote=on' in (assets / 'settings.txt').read_text(), 'A na linha liga a opção e grava phone_remote=on')
check((assets / 'screen.ppm').exists(), 'captura do painel gerada')
if (assets / 'screen.ppm').exists():
    try:
        from PIL import Image
        with Image.open(assets / 'screen.ppm') as im:
            im.save(out / 'painel-celular.png')
        print('captura:', out / 'painel-celular.png')
    except ImportError:
        pass

# 2) pareamento e comandos contra o app real
proc, assets = app('pair', True, '', 9000)
try:
    check(wait_port(proc), 'servidor responde a página na porta 8135 com a opção ligada')
    page = call('GET', '/', token=None)[1]
    check('Tube360' in page and 'https://' not in page, 'página sem recurso externo')
    check(call('POST', '/cmd', 'a=down', token='pair')[0] == 401, 'comando sem pareamento recusado (401)')
    check(call('POST', '/cmd', 'a=down', token=None)[0] == 400, 'POST sem cabeçalho próprio recusado (400)')
    check(call('POST', '/pair', 'code=000000')[0] == 403, 'código errado recusado (403)')
    status, body = call('POST', '/pair', f'code={CODE}')
    check(status == 200 and '"token"' in body, 'código certo pareia')
    tok = body.split('"token":"')[1][:32] if status == 200 else ''
    check(call('GET', '/state', token=tok)[0] == 200, 'estado com token')
    for a in ('down', 'right', 'ok', 'back', 'volup', 'playpause'):
        check(call('POST', '/cmd', 'a=' + a, token=tok)[0] == 200, f'ação {a} aceita')
    check(call('POST', '/cmd', 'a=search&q=lofi%20beats', token=tok)[0] == 200, 'busca aceita')
    check(call('POST', '/cmd', 'a=rm', token=tok)[0] == 400, 'ação fora do alfabeto recusada')
    time.sleep(1.5)
finally:
    proc.wait(timeout=60)
log = (assets / 'app.log').read_text()
check('celular: 1 sessao(oes) pareada(s), porta 8135' in log, 'app registrou o pareamento')
import re
actions = re.findall(r'celular: acao (\d+)', log)
check(actions == ['1', '3', '4', '5', '7', '6', '9'], f'app recebeu as ações na ordem (códigos {actions})')
(out / 'resultado.txt').write_text('\n'.join(fails) or 'ok')
print('saída em', out)
sys.exit(1 if fails else 0)
