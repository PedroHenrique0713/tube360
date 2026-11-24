#!/usr/bin/env python3
"""App real, busca/seleção pelo controle simulado e receptores TLS em loopback."""
import contextlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from discovery_test import receiver

root = Path(__file__).resolve().parents[2]
work = root / '.work/f24/screenshots'
work.mkdir(parents=True, exist_ok=True)
# LEFT abre a barra lateral, seis DOWN chegam às configurações.
open_picker = 'L' + 'D' * 6 + 'A' + 'D' * 13 + 'A'
with tempfile.TemporaryDirectory(prefix='tube360-screen-') as temp:
    cert = Path(temp)
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=Cast fixture', '-keyout', str(cert / 'key.pem'), '-out', str(cert / 'cert.pem')], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def capture(name, lang='pt', network='127.77.24.1/255.255.255.0', keys=open_picker, ms=4200, large=False, expect=None):
        assets = work / (name + '-assets')
        assets.mkdir(exist_ok=True)
        for font in (root / 'client/assets').glob('inter-*.ttf'):
            shutil.copy(font, assets / font.name)
        (assets / 'settings.txt').write_text(f'lang={lang}\ncols=4\nquality=720\nsound=tv\nserver=off\nlarge_text={"on" if large else "off"}\n')
        (assets / 'nest.txt').unlink(missing_ok=True)
        env = {**os.environ, 'SDL_VIDEO_DRIVER': 'offscreen', 'SDL_AUDIO_DRIVER': 'dummy', 'XT_ASSETS': str(assets) + '/', 'XT_NEST_NETWORK': network, 'XT_YT_REPLAY': str(root / 'bench/standalone/fixtures'), 'XT_CAST_DISABLE': '1', 'XT_SHOT': str(work / (name + '.ppm')), 'XT_SHOT_MS': str(ms), 'XT_KEYS_MS': '110', 'XT_KEYS': keys}
        with (work / (name + '.log')).open('w') as log:
            subprocess.run([str(root / 'client/build/tube360')], env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=25)
        subprocess.run(['magick', str(work / (name + '.ppm')), str(work / (name + '.png'))], check=True)
        if expect is not None:
            assert (assets / 'nest.txt').read_text().splitlines()[0] == expect
        print(work / (name + '.png'), flush=True)

    with receiver(cert, '127.77.24.2', name='Sala', model='Google Nest Mini'), receiver(cert, '127.77.24.3', name='Cozinha', model='Google Nest Audio', uid='22222222-2222-2222-2222-222222222222'):
        capture('speakers-pt')
        capture('speakers-en', lang='en')
        capture('speakers-es-large', lang='es', large=True)
        # Deixa a busca terminar e escolhe a segunda caixa (Sala), reabre para conferir.
        capture('selected', keys=open_picker + '.' * 10 + 'DDA' + 'A', ms=5400, expect='127.77.24.2 8009')
        # Nenhum deve persistir vazio e escolher o som da TV.
        capture('none', keys=open_picker + '.' * 10 + 'A' + 'A', ms=5300, expect='')
    capture('empty', network='127.77.25.1/255.255.255.0')
    capture('network-error', network='invalid')
    with receiver(cert, '127.77.24.2', stall=True):
        capture('searching', ms=2750)
        capture('cancelled', keys=open_picker + 'B', ms=3400)
