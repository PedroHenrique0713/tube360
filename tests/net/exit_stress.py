#!/usr/bin/env python3
"""Abre o app de PC (offscreen, rede real) com HTTPS em andamento e sai pelo caminho normal (XT_SHOT_MS); conta quedas.
uso: exit_stress.py <raiz-do-repo> <pasta-de-saida> <n> <paralelo> [min_ms max_ms]  (BIN=<executavel> troca o binário; KEEP=1 guarda as execuções boas)"""
import os, random, shutil, subprocess, sys, time, glob
from pathlib import Path
W = Path(sys.argv[1]); OUT = Path(sys.argv[2]).resolve(); N = int(sys.argv[3]); PAR = int(sys.argv[4])
LO, HI = (int(sys.argv[5]), int(sys.argv[6])) if len(sys.argv) > 6 else (1500, 9000)
VIDS = ['jNQXAC9IVRw', 'dQw4w9WgXcQ', 'aqz-KE-bpKQ', '9bZkp7q19f0']
OUT.mkdir(parents=True, exist_ok=True)
random.seed(69)
def launch(i):
    a = OUT / f'r{i:04d}'
    if a.exists(): shutil.rmtree(a)
    a.mkdir(parents=True)
    for f in (W / 'client/assets').glob('*.ttf'): shutil.copyfile(f, a / f.name)
    (a / 'settings.txt').write_text('lang=pt\nsound=tv\ncols=4\nshorts=row\n')
    ms = random.randint(LO, HI)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('XT_', 'F47_'))}
    env.update(HOME=str(a / 'home'), XDG_CONFIG_HOME=str(a / 'config'), XDG_CACHE_HOME=str(a / 'cache'), SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy',
               LP_NUM_THREADS='1', XT_CAST_DISABLE='1', XT_ASSETS=str(a), XT_SHOT=str(a / 'o.ppm'), XT_SHOT_MS=str(ms), XT_SHOT_FRAME='2147483647')
    if i % 2: env['XT_PLAY'] = VIDS[i % len(VIDS)]
    log = (a / 'app.log').open('w')
    p = subprocess.Popen([os.environ.get('BIN') or str(W / 'client/build/tube360')], env=env, cwd=a, stdout=log, stderr=subprocess.STDOUT)
    return (i, ms, p, time.time(), a)
running = []; done = []; nxt = 0
while nxt < N or running:
    while nxt < N and len(running) < PAR:
        running.append(launch(nxt)); nxt += 1
    time.sleep(0.2)
    for r in list(running):
        i, ms, p, t0, a = r
        rc = p.poll()
        if rc is None and time.time() - t0 > ms / 1000 + 60:
            p.kill(); rc = p.wait(); rc = ('hang', rc)
        if rc is not None:
            running.remove(r); done.append((i, ms, rc))
            tail = (a / 'app.log').read_text(errors='replace').strip().splitlines()[-1:] 
            bad = rc not in (0,)
            print(f'run {i} ms={ms} rc={rc} {"CRASH" if bad else "ok"}', flush=True)
            if not bad and not os.environ.get('KEEP'): shutil.rmtree(a, ignore_errors=True)
bad = [d for d in done if d[2] != 0]
print(f'TOTAL {len(done)} crashes/nonzero {len(bad)} -> {bad}')
