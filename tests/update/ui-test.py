#!/usr/bin/env python3
"""Captura o app/renderer real com o worker HTTP de teste e catálogo gravado isolado."""
import argparse
import sys as _sys
_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[2] / 'tests'))
from settings_nav import seq
from functools import partial
from http.server import ThreadingHTTPServer
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--directory', type=Path, default=ROOT / '.work/update-final')
p.add_argument('--only', nargs='*', help='Recaptura apenas estes nomes de cenário')
a = p.parse_args()
folder = a.directory.resolve()
spec = importlib.util.spec_from_file_location('console_kit', ROOT / 'tests/update/console-kit.py')
kit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kit)
base = re.search(r'TUBE360_UPDATE_TEST_BASE "([^"]+)"', (folder / 'test-config.hpp').read_text())[1]
server = ThreadingHTTPServer(('127.0.0.1', urlsplit(base).port), partial(kit.Handler, root=folder, delay=0.02))
server.daemon_threads = True
thread = threading.Thread(target=server.serve_forever)
thread.start()
try:
    for name, lang, large, height, screen, keys, shot_ms, scenario in [
        ('home-pt', 'pt', False, 720, '', '', 4000, 'good'),
        ('home-en-large', 'en', True, 720, '', '', 4000, 'good'),
        ('home-es-sd-large', 'es', True, 480, '', '', 4000, 'good'),
        ('home-ar', 'ar', False, 720, '', '', 4000, 'good'),
        ('home-ar-sd-large', 'ar', True, 480, '', '', 4000, 'good'),
        ('settings-pt', 'pt', False, 720, 'settings', seq(14), 4000, 'good'),
        ('offer-pt', 'pt', False, 720, 'settings', seq(14,'A'), 4200, 'good'),
        ('offer-en', 'en', False, 720, 'settings', seq(14,'A'), 4200, 'good'),
        ('offer-es-sd-large', 'es', True, 480, 'settings', seq(14,'A'), 4200, 'good'),
        ('downloading-pt', 'pt', False, 720, 'settings', ' '*25+seq(14,'A')+' '*12+'A', 5200, 'good'),
        ('ready-pt', 'pt', False, 720, 'settings', ' '*25+seq(14,'A')+' '*12+'A', 7500, 'good'),
        ('cancelled-pt', 'pt', False, 720, 'settings', ' '*25+seq(14,'A')+' '*12+'A B', 5400, 'good'),
        ('error-pt', 'pt', False, 720, 'settings', ' '*25+seq(14,'A')+' '*12+'A', 7500, 'bad-hash'),
        ('auto-file-pt', 'pt', False, 720, '', '', 6500, 'good'),
    ]:
        if a.only and name not in a.only:
            continue
        (folder / 'scenario.txt').write_text(scenario)
        assets = folder / ('visual-' + name)
        assets.mkdir(exist_ok=True)
        for source in (folder / 'ui').iterdir():
            if source.is_file() and source.suffix not in ('.log',) and not source.name.startswith('update-'):
                shutil.copyfile(source, assets / source.name)
        if lang == 'ar':  # sem a fonte Noto o texto árabe não desenha e a captura não prova nada
            for font in (ROOT / 'client/assets').glob('noto-sans-arabic-*.ttf'):
                shutil.copyfile(font, assets / font.name)
        for filename in ('update-app-state.json', 'tube360.next.xex', 'tube360.next.xex.part'):
            (assets / filename).unlink(missing_ok=True)
        (assets / 'update-auto.txt').unlink(missing_ok=True)
        if name == 'auto-file-pt':
            (assets / 'update-auto.txt').write_text('on\n')
        (assets / 'settings.txt').write_text(f'lang={lang}\nsound=tv\nlarge_text={"on" if large else "off"}\noutput_height={height}\n')
        env = {**os.environ, 'HOME': str(folder / 'home'), 'XDG_CACHE_HOME': str(folder / 'cache'),
               'SDL_VIDEO_DRIVER': 'offscreen', 'SDL_AUDIO_DRIVER': 'dummy', 'XT_CAST_DISABLE': '1',
               'XT_ASSETS': str(assets), 'XT_YT_REPLAY': str(assets), 'XT_SCREEN': screen,
               'XT_SHOT': str(folder / (name + '.ppm')), 'XT_SHOT_MS': str(shot_ms),
               'XT_SHOT_FRAME': '100000000', 'XT_KEYS': keys, 'XT_KEYS_MS': '100'}
        with (folder / (name + '.log')).open('w') as log:
            subprocess.run([str(folder / 'tube360-ui')], env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=25)
        # O 2º A só baixa se o painel já estiver em "disponível": confere no log do serviço, não só na imagem.
        expected = {'downloading-pt': ['download inicio'], 'ready-pt': ['download inicio', 'staged build='],
                    'cancelled-pt': ['download inicio', 'download cancelado'], 'error-pt': ['download inicio', 'download fim erro=']}
        if name in expected:
            service_log = (assets / 'update.log').read_text()
            for needle in expected[name]:
                assert needle in service_log, (name, needle, service_log[-1500:])
        if name == 'auto-file-pt':
            import json
            assert json.loads((assets / 'update-app-state.json').read_text())['phase'] == 'staged'
            assert 'TEST auto-download requested build=' in (assets / 'update.log').read_text()
        from PIL import Image
        Image.open(folder / (name + '.ppm')).save(folder / (name + '.png'))
        print('Captured', name, flush=True)
finally:
    server.shutdown()
    server.server_close()
    thread.join()
