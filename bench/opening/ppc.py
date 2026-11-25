#!/usr/bin/env python3
"""Exercise the Xbox FFmpeg archive in QEMU with the console's 32-bit MSR mode."""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


class QMP:
    def __init__(self):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(3)
        self.socket.connect('qmp.sock')
        self.file = self.socket.makefile('rwb', buffering=0)
        self.file.readline()
        self.command('qmp_capabilities')

    def command(self, execute, **arguments):
        request = dict(execute=execute)
        if arguments:
            request['arguments'] = arguments
        self.file.write((json.dumps(request) + '\n').encode())
        while True:
            reply = json.loads(self.file.readline())
            if 'error' in reply:
                raise RuntimeError(reply)
            if 'return' in reply:
                return reply['return']

    def words(self, address):
        output = self.command('human-monitor-command', **{'command-line': f'xp /20wx 0x{address:x}'})
        values = [int(v, 16) for v in re.findall(r'0x([0-9a-f]{8})\b', output)]
        if len(values) != 20:
            raise RuntimeError('incomplete emulator memory response')
        return list(struct.unpack('>10q', struct.pack('>20I', *values)))


def run(args, name):
    source = args.fixtures / name
    work = args.work / name
    work.mkdir(parents=True, exist_ok=True)
    count, stride, timescale, fps, tfdt, mfhd = map(int, (source / 'meta.txt').read_text().split())
    prefix = (source / 'prefix.bin').read_bytes()
    fragment = (source / 'fragment.bin').read_bytes()
    defines = dict(PREFIX_SIZE=len(prefix), FRAGMENT_SIZE=len(fragment), FILE_SIZE=len(prefix) + count * stride,
                   STRIDE=stride, TIMESCALE=timescale, TFDT_OFFSET=tfdt, MFHD_OFFSET=mfhd)
    (work / 'fixture.h').write_text(''.join(f'#define {key} INT64_C({value})\n' for key, value in defines.items()))
    blobs = []
    objcopy = str(args.cc.with_name(args.cc.name.removesuffix('gcc') + 'objcopy'))
    for filename, data in [('prefix.bin', prefix), ('fragment.bin', fragment)]:
        (work / filename).write_bytes(data)
        obj = work / (filename + '.o')
        subprocess.run([objcopy, '-I', 'binary', '-O', 'elf32-powerpc', '-B', 'powerpc:common', filename,
                        str(obj)], cwd=work, check=True)
        binary = bytearray(obj.read_bytes())
        binary[36:40] = (0x100).to_bytes(4, 'big')  # EF_PPC_XSDK: no code in these data-only objects.
        obj.write_bytes(binary)
        blobs.append(str(obj))
    elf = work / 'probe.elf'
    command = [str(args.cc), '-O2', '-mcpu=cell', '-fdisable-rtl-combine', '-I' + str(args.prefix / 'include'),
               '-I' + str(work), '-nostdlib', '-static', '-Wl,-e,_start', '-Wl,-Ttext=0x10010000',
               *map(str, [ROOT / 'bench/opening/ppc.c', ROOT / 'bench/opening/ppc-stubs.c', ROOT / 'bench/opening/ppc-start.S']),
               *blobs, *map(str, args.extra_object), '-Wl,--start-group',
               *map(str, sorted((args.prefix / 'lib').glob('*.a'))), '-lc', '-lm', '-lgcc', '-Wl,--end-group', '-o', str(elf)]
    with (work / 'build.log').open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    nm = subprocess.check_output([str(args.cc.with_name(args.cc.name.removesuffix('gcc') + 'nm')), str(elf)], text=True)
    address = int(re.search(r'^([0-9a-f]+) B results$', nm, re.M)[1], 16)
    previous = Path.cwd()
    qmp = None
    with (work / 'qemu.log').open('w') as log:
        process = subprocess.Popen([str(args.qemu), '-M', 'ppce500', '-cpu', 'e6500', '-m', '512M',
                                    '-bios', str(elf), '-nographic', '-nodefaults', '-S',
                                    '-qmp', 'unix:qmp.sock,server=on,wait=off'], cwd=work, stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            os.chdir(work)
            deadline = time.monotonic() + 20
            while not Path('qmp.sock').exists():
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('emulator failed to start')
                time.sleep(.02)
            qmp = QMP()
            qmp.command('cont')
            while True:
                values = qmp.words(address)
                if values[9] == 0x463436:
                    break
                if time.monotonic() > deadline:
                    raise RuntimeError(f'emulator timeout; checkpoints={values}')
                time.sleep(.05)
            passed = (values[0] >= 0 and values[1] == defines['FILE_SIZE'] and values[2] >= 0 and
                      values[3] == 120000 and values[4] >= 0 and values[5] == 10000000 and
                      0 < values[6] < 32 and values[8] == 0)
            result = dict(case=name, passed=passed, header=values[0], size=values[1], expected_size=defines['FILE_SIZE'],
                          seek120=values[2], pts120_ms=values[3], seek10000=values[4], pts10000_ms=values[5],
                          reads=values[6], maximum_offset=values[7], exceeded_budget=values[8],
                          architecture='PPC64 registers, 32-bit MSR.SF=0; demux only')
            (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps(result), flush=True)
            return passed
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
                os.chdir(previous)



if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixtures', type=Path)
    parser.add_argument('--prefix', required=True, type=Path)
    parser.add_argument('--cc', required=True, type=Path)
    parser.add_argument('--qemu', required=True, type=Path)
    parser.add_argument('--work', type=Path, default=ROOT / '.work/opening-ppc')
    parser.add_argument('--extra-object', type=Path, action='append', default=[])
    args = parser.parse_args()
    for key in ('fixtures', 'prefix', 'cc', 'qemu', 'work'):
        setattr(args, key, getattr(args, key).resolve())
    args.extra_object = [p.resolve() for p in args.extra_object]
    results = [run(args, name) for name in ('long25', 'hdr60')]
    raise SystemExit(0 if all(results) else 1)
