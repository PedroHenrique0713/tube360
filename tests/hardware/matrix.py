"""Perfis × controle real do app de PC. Sem rede externa, dispositivos ou inferência de clock."""
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2] / 'tests'))
from settings_nav import seq
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
PROFILES = ('xenon', 'zephyr', 'falcon', 'opus', 'jasper', 'trinity', 'corona', 'corona-e', 'winchester', 'unknown')
p = argparse.ArgumentParser()
p.add_argument('evidence', type=Path)
p.add_argument('--app', type=Path, default=ROOT / 'client/build/tube360')
p.add_argument('--mapped-app', type=Path, default=ROOT / 'client/build/board-mapped-test')
a = p.parse_args()
base = a.evidence.resolve()
base.mkdir(parents=True, exist_ok=True)
results = []
cases = []
for profile in PROFILES:
    # Oráculo da política/saídas; não é calculado pelo mesmo helper C sob teste.
    hd = '1920x1080' if profile not in ('xenon', 'opus', 'unknown') else '1280x720'
    cases += [dict(name=profile + '-auto', profile=profile, expected=[hd, '640x480', '1280x720'] if profile in ('corona','corona-e','winchester') else ['1280x720'] * 3, native=int(profile in ('corona','corona-e','winchester'))),
              dict(name=profile + '-native', profile=profile, expected=[hd, '640x480', '1280x720'], native=1, driver='native'),
              dict(name=profile + '-sd', config=f'board={profile}\ndriver=native\nmode=480p\n',
                   expected=['640x480'] * 3, native=1),
              dict(name=profile + '-interlaced-safe', config=f'board={profile}\nmode=480i\ncable=composite\n',
                   expected=['640x480'] * 3, native=0)]
cases += [
    dict(name='safe-veto', profile='corona', config='board=corona\n', driver='native', expected=['1280x720'] * 3, native=0),
    dict(name='invalid-file', config='board=xenon\ncable=hdmi\ndriver=native\ndelay_ms=250\nfault=fonts\n',
         expected=['1280x720'] * 3, native=0, invalid=True),
    dict(name='bounded-delay', config='board=jasper\ndelay_ms=25\n', expected=['1280x720'] * 3, native=0, delay=True),
    dict(name='slow-completion', config='board=jasper\nframe_delay_ms=25\n', expected=['1280x720'] * 3, native=0, pacing=True),
    dict(name='display-fault', config='board=jasper\nfault=display-open\n', expected=[], native=0, failure='display-open'),
    dict(name='output-fault', config='board=falcon\nfault=output\n', expected=[], native=0, failure='output'),
    dict(name='fonts-fault', config='board=falcon\ndriver=native\nfault=fonts\nfault_after=1\n',
         expected=['1920x1080'], native=1, failure='fonts'),
]


def environment(folder, case):
    assets = folder / 'assets'
    assets.mkdir(parents=True, exist_ok=True)
    for font in (ROOT / 'client/assets').glob('inter-*.ttf'):
        shutil.copy(font, assets)
    for name in ('board-sim.txt', 'video-driver.txt', 'boot-app.log', 'boot-mapped-test.log'):
        (assets / name).unlink(missing_ok=True)
    (assets / 'settings.txt').write_text('lang=pt\nsound=tv\noutput_height=720\n')
    (folder / 'error.txt').write_text('network')
    if 'config' in case:
        (assets / 'board-sim.txt').write_text(case['config'])
    if 'driver' in case:
        (assets / 'video-driver.txt').write_text(case['driver'])
    env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(folder), XDG_CACHE_HOME=str(folder / 'cache'), SDL_VIDEO_DRIVER='offscreen',
               SDL_AUDIO_DRIVER='dummy', XT_CAST_DISABLE='1', XT_ASSETS=str(assets), XT_YT_REPLAY=str(folder))
    if case.get('profile'):
        env['XT_BOARD'] = case['profile']
    return env


def checkpoint_pairs(log):
    pending = {}
    for line in log.splitlines():
        m = re.search(r' (begin|end|error) (settings.output|display.fonts) ', line)
        if not m:
            continue
        phase, stage = m.groups()
        if phase == 'begin':
            assert stage not in pending, line
            pending[stage] = line
        else:
            assert stage in pending, line
            del pending[stage]
    return pending


for case in cases:
    folder = base / case['name']
    env = environment(folder, case)
    shot = folder / 'screen.ppm'
    shot.unlink(missing_ok=True)
    # Teclas e captura no mesmo relógio de frames: uma compilação concorrente pode atrasar um atlas.
    # O último botão normal é aplicado no frame 400; a foto só sai no 520.
    env.update(XT_SCREEN='settings', XT_SHOT=str(shot), XT_SHOT_FRAME='520',
               XT_KEYS=seq(9, 'RRR', 11, 'RR') + ('B' if case.get('failure') else ''))
    began = time.monotonic()
    try:
        run = subprocess.run([str(a.app.resolve())], cwd=folder, env=env, capture_output=True, timeout=30)
        (folder / 'stdout.log').write_bytes(run.stdout + run.stderr)
        log = (folder / 'assets/boot-app.log').read_text()
        expected_exit = int(bool(case.get('failure')))
        assert run.returncode == expected_exit, (run.returncode, expected_exit, run.stderr[-1500:])
        assert f'selected={"native" if case["native"] else "safe"}' in log, log
        assert 'board.sim active=1' in log and 'synthetic=1' in log
        actual = re.findall(r'end settings.output requested=\d+ actual=(\d+x\d+)', log)
        assert actual == case['expected'], (actual, case['expected'])
        pending = checkpoint_pairs(log)
        if case.get('failure'):
            assert 'error startup.fail' in log
            if case['failure'] != 'display':
                assert 'error board.sim.inject stage=' + case['failure'] in log
            assert not pending, pending
        else:
            assert not pending, pending
            assert log.count('end display.fonts ') == (6 if case['native'] and '-sd' not in case['name'] else 3), log
            assert 'large_text=1 loaded=1' in log and 'large_text=0 loaded=1' in log
            assert log.count('begin frame.acquire ') == (4 if case['native'] and '-sd' not in case['name'] else 1)
            assert 'end app.exit ' in log
        if case.get('invalid'):
            assert 'valid=0' in log and 'board.sim.inject' not in log and 'board.sim.delay' not in log
        if case.get('delay'):
            assert 'delay_ms=25 synthetic=1' in log
        if case.get('pacing'):
            frames, duration = map(int, re.search(r'end app.exit normal shutdown frames=(\d+) run_ms=(\d+) simulated_frame_delay_ms=25', log).groups())
            assert 0 < frames * 1000 / duration <= 40.1, (frames, duration)
        assert shot.exists(), 'missing screenshot'
        with Image.open(shot) as im:
            im.save(folder / 'screen.png')
            size = im.size
        shot.unlink()
        result = dict(case, status='PASS', seconds=round(time.monotonic() - began, 3), extent=actual, screenshot_size=size)
    except Exception as error:
        result = dict(case, status='FAIL', error=repr(error))
    results.append(result)
    print(result['status'], case['name'], result.get('error', result.get('extent')), flush=True)
    (base / 'results.json').write_text(json.dumps(results, indent=2) + '\n')

mapped_cases = [
    ('mapped-native', 'board=corona\ndriver=native\n', 'mapped', 'first=1 retry=1'),
    ('mapped-safe', 'board=jasper\n', 'staging', 'enabled=0 first=0 retry=0'),
    ('mapped-off', 'board=corona\ndriver=native\nmapped=off\n', 'staging', 'enabled=0 first=0 retry=0'),
    ('mapped-quota', 'board=corona\ndriver=native\nmapped_limit_mib=1\n', 'staging', 'first=0 retry=0 bytes=0 slots=0'),
    ('mapped-fault', 'board=corona\ndriver=native\nfault=mapped-alloc\n', 'staging', 'first=0 retry=1'),
]
images = []
for name, config, expected, marker in mapped_cases:
    folder = base / name
    env = environment(folder, dict(config=config))
    shot = folder / 'screen.ppm'
    try:
        run = subprocess.run([str(a.mapped_app.resolve()), expected, str(shot)], cwd=folder, env=env,
                             capture_output=True, timeout=20)
        (folder / 'stdout.log').write_bytes(run.stdout + run.stderr)
        assert run.returncode == 0 and marker in run.stdout.decode(), (run.returncode, run.stdout[-2000:], run.stderr[-1000:])
        with Image.open(shot) as im:
            images.append(im.copy())
            im.save(folder / 'screen.png')
        result = dict(name=name, status='PASS', contract=marker)
    except Exception as error:
        result = dict(name=name, status='FAIL', error=repr(error))
    results.append(result)
    print(result['status'], name, result.get('error', marker), flush=True)
    (base / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
if len(images) == len(mapped_cases):
    # O shader recebe a mesma imagem, tanto pela superfície direta como pelo fallback real de upload.
    for im in images[1:]:
        assert im.size == images[0].size
        assert max(hi for lo, hi in ImageChops.difference(images[0], im).getextrema()) <= 2
    print('PASS mapped/staging image comparison (max channel difference <= 2)', flush=True)
assert all(r['status'] == 'PASS' for r in results), 'see results.json for every failing scenario'
print(f'PASS matrix: {len(results)} scenarios', flush=True)
