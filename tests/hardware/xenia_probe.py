"""Lança apenas arquivos locais em Xvfb privado; registra estágio alcançado, sem comandos ao Xbox."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import selectors
import shutil
import subprocess
import time

from PIL import ImageGrab

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('evidence', type=Path)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--xvfb', type=Path, required=True)
p.add_argument('--seconds', type=int, default=15)
a = p.parse_args()
assert 1 <= a.seconds <= 30
base = a.evidence.resolve()
base.mkdir(parents=True, exist_ok=True)
xserver = None
try:
    with (base / 'xvfb.log').open('w') as xlog:
        xserver = subprocess.Popen([str(a.xvfb.resolve()), '-displayfd', '1', '-screen', '0', '1280x720x24',
                                   '-nolisten', 'tcp'], stdout=subprocess.PIPE, stderr=xlog)
        with selectors.DefaultSelector() as ready:
            ready.register(xserver.stdout, selectors.EVENT_READ)
            assert ready.select(5), 'Xvfb did not return a display'
            display = ':' + xserver.stdout.readline().decode().strip()
        assert display[1:].isdigit(), display
        for title in ('tube360', 'tube360-launcher'):
            folder = base / title
            folder.mkdir(exist_ok=True)
            for font in (ROOT / 'client/assets').glob('inter-*.ttf'):
                shutil.copy(font, folder)
            xex = folder / (title + '.xex')
            shutil.copy(ROOT / 'client/build-x360' / xex.name, xex)
            (folder / 'settings.txt').write_text('lang=pt\nsound=tv\n')
            (folder / 'video-driver.txt').write_text('safe\n')
            (folder / 'dbgprint.txt').write_text('1\n')
            home = folder / 'home'
            home.mkdir(exist_ok=True)
            env = {k: v for k, v in os.environ.items() if not k.startswith('XT_')}
            env.update(DISPLAY=display, WAYLAND_DISPLAY='', HOME=str(home),
                       XDG_CONFIG_HOME=str(home / 'config'), XDG_CACHE_HOME=str(home / 'cache'),
                       SDL_AUDIODRIVER='dummy')
            command = [str(a.binary.resolve()), '--gpu=vulkan', '--apu=nop',
                       '--allow_game_relative_writes=true',
                       '--storage_root=' + str(folder / 'storage'), str(xex)]
            with (folder / 'stdout.log').open('w') as log:
                child = subprocess.Popen(command, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                                         preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
                try:
                    deadline = time.monotonic() + a.seconds
                    while child.poll() is None and time.monotonic() < deadline:
                        time.sleep(.1)
                    ImageGrab.grab(xdisplay=display).save(folder / 'screen.png')
                    timed_out = child.poll() is None
                finally:
                    if child.poll() is None:
                        child.terminate()
                        try:
                            child.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            child.kill()
                            child.wait(timeout=3)
                summary = dict(command=command, returncode=child.returncode, timed_out=timed_out,
                               seconds=a.seconds, xex_sha256=hashlib.sha256(xex.read_bytes()).hexdigest(),
                               emulator_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest())
                (folder / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
                print(title, summary, flush=True)
finally:
    if xserver is not None:
        xserver.terminate()
        try:
            xserver.wait(timeout=3)
        except subprocess.TimeoutExpired:
            xserver.kill()
            xserver.wait(timeout=3)
