"""Contratos ligados às bibliotecas do produto, sem transporte externo."""
from pathlib import Path
import os
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
build = ROOT / 'client/build'
work = ROOT / 'tests/f52/results/contracts'
work.mkdir(parents=True, exist_ok=True)
flags = (build / 'CMakeFiles/tube360-main.dir/flags.make').read_text()
defs = shlex.split(flags.split('CXX_DEFINES = ', 1)[1].splitlines()[0])
includes = shlex.split(flags.split('CXX_INCLUDES = ', 1)[1].splitlines()[0])
link = shlex.split((build / 'CMakeFiles/tube360.dir/link.txt').read_text())
source_object = 'CMakeFiles/tube360-main.dir/src/main.cpp.o'


def run(name, source, *args):
    output = work / name
    obj = output.with_suffix('.o')
    print('BUILD', name, flush=True)
    subprocess.run([link[0], '-std=c++20', '-O0' if name == 'f52' else '-O1', '-DNDEBUG', *defs, *includes,
                    '-c', str(ROOT / source), '-o', str(obj)], check=True)
    command = list(link)
    command[command.index(source_object)] = str(obj)
    command[command.index('-o') + 1] = str(output)
    subprocess.run(command, cwd=build, check=True)
    print('RUN', name, flush=True)
    if name == 'h264-policy':
        for height, size in ((1080, '1920x1080'), (720, '1280x720')):
            media = work / f'{height}.mp4'
            if not media.exists():
                subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi',
                                '-i', f'testsrc2=size={size}:rate=30', '-t', '2.1', '-an',
                                '-c:v', 'libx264', '-threads', '2', '-preset', 'ultrafast',
                                '-g', '30', str(media)], check=True)
    environment = dict(os.environ, SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy')
    result = subprocess.run([str(output), *map(str, args)], cwd=ROOT, env=environment)
    print('EXIT', name, result.returncode, flush=True)
    return result.returncode == 0


cases = [
    ('f52', 'tests/f52/contracts.cpp', work / 'settings'),
    ('search', 'bench/search/test.cpp', ROOT / 'bench/search/fixtures'),
    ('search-worker', 'bench/search/worker.cpp'),
    ('catalog', 'bench/catalog/test.cpp', ROOT / 'bench/catalog/fixtures'),
    ('catalog-oauth', 'bench/catalog/oauth-test.cpp', ROOT / 'bench/catalog/fixtures'),
    ('catalog-membership', 'bench/catalog/membership-test.cpp', ROOT / 'bench/catalog/fixtures'),
    ('account', 'bench/account/test.cpp', work),
    ('search-account', 'bench/search-account/test.cpp'),
    ('channel-art', 'bench/local-library/channel-test.cpp', ROOT / 'bench/local-library'),
    ('display', 'bench/display/test.cpp', ROOT / 'bench'),
    ('arabic', 'bench/arabic/test.cpp'),
    ('sponsorblock', 'bench/sponsorblock/test.cpp', ROOT / 'bench/sponsorblock/fixtures/public-prefix.json'),
    ('manual-quality', 'tests/reddit/manual_quality.cpp', work / 'quality'),
    ('reddit-contract', 'tests/reddit/contracts.cpp'),
    ('arabic-render', 'bench/arabic/render_test.cpp', ROOT / 'client/assets'),
    ('h264-policy', 'bench/1080p-app/policy_test.cpp', work / '1080.mp4', work / '720.mp4', str(work)+'/'),
    ('standalone', 'bench/standalone/test.cpp', ROOT / 'bench/standalone/fixtures'),
    ('player-extras', 'bench/player-extras/test.cpp'),
]
passed = 0
if not sys.argv[1:]:
    cases = cases[:1]
elif '--all' not in sys.argv[1:]:
    cases = [case for case in cases if case[0] in sys.argv[1:]]
    if not cases:
        raise SystemExit('No matching tests')
for case in cases:
    passed += run(*case)
print(f'{passed}/{len(cases)} executables passed', flush=True)
raise SystemExit(passed != len(cases))
