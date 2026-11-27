#!/usr/bin/env python3
"""Executa o callback compilado com headers xsdk e o launcher real, só em arquivos locais."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'client/build'
# O callback isolado não prova que o executável o registra antes de iniciar serviços.
source = (ROOT / 'client/src/main.cpp').read_text()
source = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                lambda m: ' ' if m[0].startswith(('//', '/*')) else m[0], source, flags=re.S)
if not re.search(r'^int\s+main\s*\([^)]*\)\s*\{\s*#ifdef\s+__XSDK__\s+'
                 r'boot_open\s*\(\s*""\s*,\s*"app"\s*\)\s*;\s*'
                 r'xsdk_on_terminate\s*\(\s*boot_session_terminated\s*\)\s*;', source, re.M):
    raise SystemExit('FAIL main termination registration: expected immediately after boot_open at entry')
print('PASS main termination registration immediately after boot_open at entry', flush=True)
# Exige a chamada direta no corpo do laço do produto, sem aceitar comentário, string ou ramo opcional.
tokens = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '""', source)
run = re.search(r'\bint\s+run\s*\([^)]*\)\s*\{', tokens)
assert run, 'FAIL main heartbeat: run function not found'
tokens = tokens[run.end():]
loop = re.search(r'\bwhile\s*\(\s*!quit\s*\)\s*\{', tokens)
assert loop, 'FAIL main heartbeat: render loop not found'
depth, end = 1, loop.end()
while depth and end < len(tokens):
    depth += (tokens[end] == '{') - (tokens[end] == '}')
    end += 1
body = tokens[loop.end():end-1]
beat = re.search(r'\bin\.tick\s*\(\s*now\s*\)\s*;\s*boot_session_heartbeat\s*\(\s*\)\s*;', body)
if not beat or body[:beat.start()].count('{') != body[:beat.start()].count('}'):
    raise SystemExit('FAIL main heartbeat: expected unconditional heartbeat after input tick in render loop')
print('PASS main heartbeat executed unconditionally in render loop after input tick', flush=True)
probe = r'''
#include "update/launcher.hpp"
#include <cassert>
using namespace updates;
struct Host:LauncherHost {
    int posts=0,launches=0;
    void log(const string&)override{}
    bool screen()override{return true;}
    bool closeScreen()override{return true;}
    int launch(const string&)override{++launches;return -1;}
    bool randomBytes(unsigned char* out,size_t n)override{for(size_t i=0;i<n;++i)out[i]=i+1;return true;}
    yt::Response postReport(const yt::Request&)override{++posts;return {503,{},"offline fixture"};}
};
int main(int argc,char** argv) {
    assert(argc==2);bool clean=string(argv[1])=="clean";
    assert(reports::make("").empty()==clean);
    Host host;launchApp("","",host);
    assert(host.posts==(clean?0:1) && host.launches==1);
}
'''
with tempfile.TemporaryDirectory(prefix='tube360-f56-terminate-') as temporary:
    base = Path(temporary)
    obj = base / 'boot_log.o'
    include = ['-I' + str(ROOT / 'client/src'), '-I' + str(ROOT / '.work/native-display/xsdk/include')]
    subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-D__XSDK__',
                    '-DTUBE360_BOOT_TEST_TIME_BASE', *include,
                    '-c', str(ROOT / 'client/src/boot_log.c'), '-o', str(obj)], check=True)
    native = base / 'native'
    subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-D__XSDK__', *include,
                    str(ROOT / 'tests/update/terminate-session.c'), str(obj), '-pthread',
                    str(ROOT / '.work/native-display/xsdk/src/terminate.c'),
                    '-Wl,--wrap=fopen', '-Wl,--wrap=malloc', '-Wl,--wrap=pthread_mutex_lock', '-Wl,--wrap=clock_gettime',
                    '-o', str(native)], check=True)
    cpp = base / 'launcher.cpp'
    cpp.write_text(probe)
    launcher = base / 'launcher'
    libraries = []
    for name in ('libtube360-update-core.a', 'libtube360-boot.a', 'libmbedcrypto.a'):
        matches = list(build.rglob(name))
        assert len(matches) == 1, (name, matches)
        libraries.append(str(matches[0]))
    subprocess.run(['c++', '-std=c++20', '-O2', '-I' + str(ROOT / 'client/src'),
                    '-I' + str(ROOT / 'client/third_party'), str(cpp), str(ROOT / 'client/src/update/launcher.cpp'),
                    '-Wl,--start-group', *libraries, '-Wl,--end-group', '-pthread', '-o', str(launcher)], check=True)
    hdd = '\\Device\\Harddisk0\\Partition1\\Apps\\Tube360\\'
    usb = '\\Device\\Mass0\\Apps with spaces\\Tube360\\'
    cases = [(hdd, 'system', True), (usb, 'system', True),
             (hdd, 'at-limit', True), (usb, 'over-limit', False),
             (hdd, 'stale', False), (usb, 'long-hang', False),
             (usb, 'refreshed', True), (hdd, 'no-heartbeat', False),
             (usb, 'interrupted-heartbeat', False),
             (hdd, 'counter-wrap', True), (usb, 'counter-zero', True),
             ('\\Device\\Mass1\\' + 'x' * 230 + '\\', 'system', True),
             (hdd, 'crash', False), (usb, 'hang', False),
             (usb, 'create-fail', False), (usb, 'write-fail', False),
             (usb, 'io-write-fail', False), (usb, 'short-write', False),
             (usb, 'prefix-fail', False), (usb, 'flush-fail', False),
             *[(usb, f'prefix-short-{count}', False) for count in range(1, 6)],
             (usb, 'prefix-short-5-empty', False),
             (usb, 'gpu-error', False), (usb, 'startup-error', False), (usb, 'error-no-log', False),
             (usb, 'already-clean', True), (usb, 'already-clean-write-fail', True),
             (usb, 'clean-resume', True),
             (usb, 'player-stuck', False), (hdd, 'player-recovered', True),
             (usb, 'retryable-network', True), (usb, 'recoverable-output', True),
             (usb, 'startup-recent', True), (usb, 'startup-at-limit', True),
             (usb, 'startup-over-limit', False), (usb, 'startup-progress', True),
             (usb, 'startup-info-only', False), (usb, 'startup-worker-only', False),
             (usb, 'running-log-only', False),
             ('game:\\', 'system', False), ('\\Device\\Mass0\\' + 'x' * 1010 + '\\', 'system', False)]
    for index, (root, mode, clean) in enumerate(cases):
        folder = base / str(index)
        folder.mkdir()
        (folder / 'tube360.xex').write_bytes(b'XEX2fixture')
        original = 'open\napp_started=1\nversion=0.3.3 build=33\n'
        if mode.endswith('-empty'):
            original = 'open\n\napp_started=1\nversion=0.3.3 build=33\n'
        (folder / 'session.txt').write_text(original)
        result = subprocess.run([str(native), root, mode], cwd=folder, capture_output=True, text=True)
        assert result.returncode == (73 if mode in ('crash', 'hang') else 0), result.stderr
        marker = (folder / 'session.txt').read_text()
        assert marker.startswith('clean\n') == clean, (mode, marker)
        if mode.startswith('already-clean'):
            assert marker == 'clean\n\n', (mode, marker)
        elif clean:
            assert marker.startswith('clean\nterminated by system\n'), (mode, marker)
        if not clean and mode not in ('short-write', 'prefix-fail', 'flush-fail') and not mode.startswith('prefix-short-'):
            assert marker == original + 'app_started=1\nversion=0.3.3 build=33\n', (mode, marker)
        subprocess.run([str(launcher), 'clean' if clean else 'open'], cwd=folder, check=True)
        print(result.stdout.strip() or 'PASS no callback: ' + mode, flush=True)
        print(f'PASS launcher after {mode}: reports={0 if clean else 1}', flush=True)
    print(f'{len(cases)} native termination/launcher scenarios passed; no console accessed.')
