#!/usr/bin/env python3
"""Run product size/offset boundaries in PPC64 registers with MSR.SF=0.

The oracle uses O0, subjects use the Xbox O2/O3 flags, and FFmpeg/curl parsing
come from the actual Xbox archives. No network, decoder or Xbox is exercised.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('opening_ppc', ROOT/'bench/opening/ppc.py')
ppc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ppc)


def flags(path, key):
    return shlex.split(path.read_text().split(key+' = ', 1)[1].splitlines()[0])


def exercise(elf, args):
    nm = subprocess.check_output([str(args.cc.with_name(args.cc.name.removesuffix('gcc')+'nm')), str(elf)], text=True)
    address = int(re.search(r'^([0-9a-f]+) B results$', nm, re.M)[1], 16)
    old = Path.cwd()
    qmp = None
    with (elf.parent/'qemu.log').open('w') as log:
        process = subprocess.Popen([str(args.qemu), '-M', 'ppce500', '-cpu', 'e6500', '-m', '512M',
            '-bios', str(elf), '-nographic', '-nodefaults', '-S', '-qmp', 'unix:qmp.sock,server=on,wait=off'],
            cwd=elf.parent, stdin=subprocess.DEVNULL, stdout=log, stderr=log)
        try:
            os.chdir(elf.parent)
            deadline = time.monotonic()+30
            while not Path('qmp.sock').exists():
                if process.poll() is not None or time.monotonic()>deadline:
                    raise RuntimeError('emulator failed to start')
                time.sleep(.02)
            qmp = ppc.QMP()
            qmp.command('cont')
            while True:
                words = qmp.words(address)
                if words[9] == 0x463436:
                    break
                if time.monotonic()>deadline:
                    raise RuntimeError(f'emulator timeout: {words}')
                time.sleep(.05)
            expected = {'product-O2': 87, 'product-O3': 87, 'ffmpeg': 100, 'curl': 53}[elf.parent.name]
            result = dict(case=elf.parent.name, checks=words[0], expected_checks=expected, failures=words[1],
                last_actual=words[2], last_expected=words[3], unexpected_workers=words[8],
                passed=words[0]==expected and words[1]==0 and words[8]==0,
                architecture='PPC64 registers, MSR.SF=0')
            (elf.parent/'result.json').write_text(json.dumps(result, indent=2)+'\n')
            print(json.dumps(result), flush=True)
            return result
        finally:
            try:
                if qmp and process.poll() is None:
                    qmp.command('quit')
            finally:
                if qmp:
                    qmp.file.close()
                    qmp.socket.close()
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                os.chdir(old)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc', type=Path, required=True)
    p.add_argument('--qemu', type=Path, required=True)
    p.add_argument('--prefix', type=Path, default=ROOT/'.work/ffmpeg-xbox')
    p.add_argument('--build-dir', type=Path, default=ROOT/'client/build-x360')
    p.add_argument('--work', type=Path, default=ROOT/'.work/opening-limits')
    args = p.parse_args()
    for name in ('cc', 'qemu', 'prefix', 'build_dir', 'work'):
        setattr(args, name, getattr(args, name).resolve())
    args.work.mkdir(parents=True, exist_ok=True)
    cc = str(args.cc)
    cxx = str(args.cc.with_name(args.cc.name.removesuffix('gcc')+'g++'))
    sources = ROOT/'bench/opening'
    common = ['-mcpu=cell', '-nostdlib', '-static', '-Wl,--gc-sections', '-Wl,-e,_start', '-Wl,-Ttext=0x10010000']
    results = []
    commands = []

    def run(cmd, log):
        commands.append(cmd)
        with log.open('a') as output:
            subprocess.run(cmd, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT, check=True)

    for level in ('O2', 'O3'):
        folder = args.work/('product-'+level)
        folder.mkdir(exist_ok=True)
        log = folder/'build.log'
        f = args.build_dir/'CMakeFiles/tube360.dir/flags.make'
        includes = flags(f, 'CXX_INCLUDES')+['-I'+str(args.prefix/'include')]
        definitions = flags(f, 'CXX_DEFINES')
        for stem, source, opts in (
            ('subject', sources/'limits-subject.cpp', ['-'+level]),
            ('main', sources/'limits.cpp', ['-O0']),
            ('app', ROOT/'client/src/update/app.cpp', ['-O3']),
            ('common', ROOT/'client/src/update/common.cpp', ['-O3']),
        ):
            run([cxx, '-std=c++20', '-mcpu=cell', '-ffunction-sections', '-fdata-sections', *opts,
                *definitions, *includes, '-I'+str(ROOT/'client/third_party/monocypher'),
                '-c', str(source), '-o', str(folder/(stem+'.o'))], log)
        run([cc, '-O0', '-mcpu=cell', '-c', str(sources/'ppc-stubs.c'), '-o', str(folder/'stubs.o')], log)
        elf = folder/'probe.elf'
        run([cxx, *common, *[str(folder/(n+'.o')) for n in ('main', 'subject', 'app', 'common', 'stubs')],
            str(sources/'ppc-start.S'), '-Wl,--start-group', '-lstdc++', '-lc', '-lm', '-lgcc', '-Wl,--end-group', '-o', str(elf)], log)
        results.append(exercise(elf, args))

    folder = args.work/'ffmpeg'
    folder.mkdir(exist_ok=True)
    elf = folder/'probe.elf'
    run([cc, '-O0', *common, '-I'+str(args.prefix/'include'), str(sources/'limits.c'),
        str(sources/'ppc-stubs.c'), str(sources/'ppc-start.S'), '-Wl,--start-group',
        *map(str, sorted((args.prefix/'lib').glob('*.a'))), '-lc', '-lm', '-lgcc', '-Wl,--end-group', '-o', str(elf)], folder/'build.log')
    results.append(exercise(elf, args))

    folder = args.work/'curl'
    folder.mkdir(exist_ok=True)
    f = args.build_dir/'yt/curl/lib/CMakeFiles/libcurl_static.dir/flags.make'
    definitions, includes = flags(f, 'C_DEFINES'), flags(f, 'C_INCLUDES')
    curl = next((ROOT/'.work/src').glob('curl-xenon-*'))
    # Function sections only remove unrelated network code from this demux-style probe.
    # Parser bytes are linked directly from the actual Xbox libcurl archive.
    run([cc, '-O3', '-mcpu=cell', '-ffunction-sections', '-fdata-sections', *definitions, *includes,
        '-c', str(curl/'lib/progress.c'), '-o', str(folder/'progress.o')], folder/'build.log')
    elf = folder/'probe.elf'
    run([cc, '-O0', *common, *definitions, *includes, str(sources/'curl-limits.c'),
        str(sources/'ppc-stubs.c'), str(sources/'ppc-start.S'), str(folder/'progress.o'),
        str(args.build_dir/'yt/curl/lib/libcurl.a'), '-lc', '-lm', '-lgcc', '-o', str(elf)], folder/'build.log')
    results.append(exercise(elf, args))
    (args.work/'commands.json').write_text(json.dumps(commands, indent=2)+'\n')
    (args.work/'results.json').write_text(json.dumps(results, indent=2)+'\n')
    if not all(r['passed'] for r in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
