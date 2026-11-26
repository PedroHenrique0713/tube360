#!/usr/bin/env python3
"""Executa o callback compilado com headers xsdk e o launcher real, só em arquivos locais."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'client/build'
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
    subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-D__XSDK__', *include,
                    '-c', str(ROOT / 'client/src/boot_log.c'), '-o', str(obj)], check=True)
    native = base / 'native'
    subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-D__XSDK__', *include,
                    str(ROOT / 'tests/update/terminate-session.c'), str(obj), '-pthread',
                    '-Wl,--wrap=fopen', '-Wl,--wrap=malloc', '-Wl,--wrap=pthread_mutex_lock',
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
             ('\\Device\\Mass1\\' + 'x' * 230 + '\\', 'system', True),
             (hdd, 'crash', False), (usb, 'hang', False),
             (usb, 'create-fail', False), (usb, 'write-fail', False),
             ('game:\\', 'system', False), ('\\Device\\Mass0\\' + 'x' * 1010 + '\\', 'system', False)]
    for index, (root, mode, clean) in enumerate(cases):
        folder = base / str(index)
        folder.mkdir()
        (folder / 'tube360.xex').write_bytes(b'XEX2fixture')
        result = subprocess.run([str(native), root, mode], cwd=folder, capture_output=True, text=True)
        assert result.returncode == (73 if mode in ('crash', 'hang') else 0), result.stderr
        marker = (folder / 'session.txt').read_text()
        assert (marker == 'clean\nterminated by system\n') == clean, (mode, marker)
        if mode in ('crash', 'hang', 'create-fail'):
            assert marker.startswith('open\n'), (mode, marker)
        subprocess.run([str(launcher), 'clean' if clean else 'open'], cwd=folder, check=True)
        print(result.stdout.strip() or 'PASS no callback: ' + mode, flush=True)
        print(f'PASS launcher after {mode}: reports={0 if clean else 1}', flush=True)
    print(f'{len(cases)} native termination/launcher scenarios passed; no console accessed.')
