"""Kernel/arquivos reais da política, com export de flags simulado; não valida hardware."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('evidence', type=Path)
p.add_argument('--policy-source', type=Path, default=ROOT / 'client/src/video_policy.c')
a = p.parse_args()
base = a.evidence.resolve()
base.mkdir(parents=True, exist_ok=True)
cases = [
    ('corona-auto', '50000001', 0, None, None, 1),
    ('winchester-auto', '60000000', 0, None, None, 0),
    ('xenon-auto', '00000000', 0, None, None, 0),
    ('missing', '50000000', 1, None, None, 0),
    ('unknown', 'f0000000', 0, None, None, 0),
    ('explicit-safe', '50000000', 0, b'safe\n', None, 0),
    ('bad-driver', '50000000', 0, b'nonsense', None, 0),
    ('explicit-native', '30000000', 0, b'native', None, 1),
    ('sim-veto-native', '50000000', 0, b'native', b'board=jasper\n', 0),
    ('sim-native-explicit', '50000000', 0, b'safe', b'board=corona-e\ndriver=native\n', 1),
    ('sim-unknown', '50000000', 0, None, b'board=unknown\n', 0),
    ('sim-mode', '50000000', 0, None, b'board=opus\nmode=480p\ncable=component\n', 0),
    ('bad-cable', '50000000', 0, None, b'board=xenon\ncable=hdmi\ndriver=native\n', 0),
    ('bad-mode', '50000000', 0, None, b'board=jasper\nmode=1080p\ncable=composite\n', 0),
    ('bad-budget', '50000000', 0, None, b'board=corona\nmapped_limit_mib=0\n', 0),
    ('duplicate', '50000000', 0, None, b'board=jasper\nboard=corona\ndriver=native\n', 0),
    ('oversize', '50000000', 0, None, b'board=jasper\n' + b' ' * 1100, 0),
    ('nul', '50000000', 0, None, b'board=jasper\0\ndriver=native', 0),
    ('no-board', '50000000', 0, None, b'driver=native\n', 0),
    ('unbounded-delay', '50000000', 0, None, b'board=jasper\ndelay_ms=251\n', 0),
    ('overflow', '50000000', 0, None, b'board=jasper\ndelay_ms=999999999999999999999999\n', 0),
    ('unknown-key', '50000000', 0, None, b'board=jasper\nmagic=1\n', 0),
]
with tempfile.TemporaryDirectory(prefix='tube360-board-policy-') as tmp:
    exe = Path(tmp) / 'test'
    for family in (5, 6, -1):
        subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-D__XSDK__',
                        f'-DTUBE360_NATIVE_BOARD_ID={family}', '-I' + str(ROOT / 'client/src'),
                        '-I' + str(ROOT / '.work/native-display/xsdk/include'),
                        str(ROOT / 'tests/hardware/policy.c'), str(a.policy_source.resolve()),
                        str(ROOT / 'client/src/board_sim.c'), '-o', str(exe)], check=True)
        selected = cases if family == 5 else [(f'family-{family}-corona', '50000000', 0, None, None, 0),
                                            (f'family-{family}-winchester', '60000000', 0, None, None, int(family == 6))]
        for name, flags, absent, driver, sim, expected in selected:
            folder = base / name
            folder.mkdir(exist_ok=True)
            for filename, data in [('video-driver.txt', driver), ('board-sim.txt', sim)]:
                path = folder / filename
                path.unlink(missing_ok=True)
                if data is not None:
                    path.write_bytes(data)
            # XT_BOARD é exclusivamente desktop; não pode falsificar a família real neste binário __XSDK__.
            run = subprocess.run([str(exe), flags, str(absent), str(expected)], cwd=folder,
                                 env={**os.environ, 'XT_BOARD': 'corona'}, capture_output=True, text=True)
            (folder / 'result.log').write_text(run.stdout + run.stderr)
            assert run.returncode == 0, (name, run.stdout, run.stderr)
            if sim is not None:
                assert 'board.sim active=1' in run.stdout
            if name == 'sim-veto-native':
                assert 'model=Corona;' in run.stdout and 'profile=jasper family=3' in run.stdout
            print('PASS policy', name, 'native' if expected else 'safe', flush=True)
