#!/usr/bin/env python3
"""Mapa único de botões (#40): compila e roda controls_test.cpp, confere o auto-teste do settings_nav e acusa
`Input::` cru (botões que o mapa governa) nos arquivos já convertidos para `ctl::`.
Uso: tests/controls/run.py   (sem argumentos; o binário vai para uma pasta temporária)"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# Arquivos que já falam pelo mapa. Quem converter uma tela acrescenta o arquivo aqui.
CONVERTED = ['client/src/ui_settings.hpp', 'client/src/ui_shell.hpp', 'client/src/update/ui.hpp', 'client/src/nest_picker.hpp']
# Direções, A e B são iguais em todo contexto; os demais botões só podem ser lidos pelo mapa.
# Arquivos do player ainda em conversão: o número de `Input::` cru só pode cair (nunca subir). Quem converter baixa o teto;
# em 0, mova o arquivo para CONVERTED.
WATCHED = {'client/src/ui_player.hpp': 2, 'client/src/ui_shorts.hpp': 1}
RAW = re.compile(r'Input::(X|Y|LB|RB|LT|RT|START|BACK)\b')

with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / 'controls_test'
    subprocess.run(['g++', '-std=c++20', '-Wall', '-I' + str(ROOT / 'client/src'), str(ROOT / 'tests/controls/controls_test.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
subprocess.run([sys.executable, str(ROOT / 'tests/settings_nav.py')], check=True)

bad = []
for name in CONVERTED:
    for number, line in enumerate((ROOT / name).read_text().splitlines(), 1):
        if RAW.search(line.split('//')[0]):
            bad.append(f'{name}:{number}: {line.strip()}')
for name, ceiling in WATCHED.items():
    found = [line.strip() for line in (ROOT / name).read_text().splitlines() if RAW.search(line.split('//')[0])]
    if len(found) > ceiling:
        bad.append(f'{name}: {len(found)} Input:: crus (teto {ceiling})')
if bad:
    print('Input:: cru em arquivo convertido (use ctl::hit / ctl::inputIndex):')
    print('\n'.join(bad))
    sys.exit(1)
print(f'controls: nenhum Input:: cru em {len(CONVERTED)} arquivos convertidos; {len(WATCHED)} do player dentro do teto')
