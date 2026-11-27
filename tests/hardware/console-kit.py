"""Prepara pastas descartáveis para o coordenador; nenhuma conexão ou operação XBDM."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('output', type=Path)
a = p.parse_args()
base = a.output.resolve()
base.mkdir(parents=True, exist_ok=True)
scenarios = {
    '00-native-baseline': None,
    '01-safe-owner': 'board=unknown\ndriver=safe\nmode=1080p\ncable=hdmi\n',
    '02-safe-jasper': 'board=jasper\ndriver=safe\n',
    '03-safe-falcon': 'board=falcon\ndriver=safe\n',
    '04-native-staging': 'board=unknown\ndriver=native\nmode=1080p\ncable=hdmi\nmapped=off\n',
    '05-native-mapped-failure': 'board=unknown\ndriver=native\nmode=1080p\ncable=hdmi\nfault=mapped-alloc\n',
    '06-safe-output-failure': 'board=falcon\nfault=output\n',
    '07-safe-fonts-failure': 'board=falcon\nfault=fonts\nfault_after=1\n',
    '08-safe-display-failure': 'board=jasper\nfault=display-open\n',
    '09-safe-delay': 'board=jasper\ndelay_ms=50\nframe_delay_ms=25\n',
}
manifest = {}
for name, config in scenarios.items():
    folder = base / name
    # Nunca sobrescreve uma pasta já usada no console ou contendo logs.
    folder.mkdir()
    for source, target in [('tube360.xex', 'tube360.xex'), ('tube360-launcher.xex', 'default.xex')]:
        shutil.copy(ROOT / 'client/build-x360' / source, folder / target)
    for font in (ROOT / 'client/assets').glob('inter-*.ttf'):
        shutil.copy(font, folder)
    (folder / 'settings.txt').write_text('lang=pt\nsound=tv\noutput_height=1080\nvideo_mode=1080p30\n')
    # Mantém o uso do dono em native; o board-sim safe tem precedência sobre este arquivo.
    (folder / 'video-driver.txt').write_text('native\n')
    if config is not None:
        (folder / 'board-sim.txt').write_text(config)
    manifest[name] = {file.name: hashlib.sha256(file.read_bytes()).hexdigest() for file in folder.iterdir() if file.is_file()}
(base / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'PASS console kit: {len(scenarios)} isolated scenarios, native baseline retained; no console accessed')
