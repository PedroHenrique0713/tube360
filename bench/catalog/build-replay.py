"""Religa os objetos do app de PC trocando apenas o transporte HTTPS por gravações."""
import pathlib
import shlex
import subprocess
import sys

repo = pathlib.Path(__file__).resolve().parents[2]
build = repo / 'client/build'
dest = pathlib.Path(sys.argv[1]).resolve()
dest.mkdir(parents=True, exist_ok=True)
obj = dest / 'replay_https.o'
subprocess.run(['c++', '-std=c++20', '-O1', '-I' + str(repo / 'client/src'),
                '-I' + str(repo / 'client/third_party'), '-c',
                str(repo / 'bench/catalog/replay_https.cpp'), '-o', str(obj)], check=True)
args = shlex.split((build / 'CMakeFiles/tube360.dir/link.txt').read_text())
args[args.index('-o') + 1] = str(dest / 'tube360-replay')
args.extend([str(obj), '-Wl,--wrap=_ZN2yt5httpsEPKSt6atomicIbE'])
subprocess.run(args, cwd=build, check=True)
print(dest / 'tube360-replay')
