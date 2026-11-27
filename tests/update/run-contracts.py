#!/usr/bin/env python3
"""Compila os contratos já existentes com os arquivos estáticos do PC desta worktree."""
from pathlib import Path
import subprocess
import tempfile
import sys
ROOT=Path(__file__).resolve().parents[2]
build=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT/'client/build'
with tempfile.TemporaryDirectory(prefix='tube360-update-contracts-') as temporary:
    base=Path(temporary)
    names=('libtube360-update-core.a','libtube360-yt.a','libtube360-boot.a','libmbedcrypto.a',
           'libmbedtls.a','libmbedx509.a','libcurl.a','libeverest.a','libp256m.a','libquickjs-ng.a')
    libraries=[]
    for name in names:
        paths=list(build.rglob(name));assert len(paths)==1,(name,paths)
        libraries.append(str(paths[0]))
    for source,target in [('contracts.cpp','contracts'),('launcher.cpp','launcher')]:
        command=['c++','-std=c++20','-O2','-I'+str(ROOT/'client/src'),'-I'+str(ROOT/'client/third_party'),str(ROOT/'tests/update'/source)]
        if source=='launcher.cpp':command.append(str(ROOT/'client/src/update/launcher.cpp'))
        command+=['-Wl,--start-group',*libraries,'-Wl,--end-group','-pthread','-lm','-o',str(base/target)]
        subprocess.run(command,check=True)
    subprocess.run([sys.executable,str(ROOT/'tests/update/test.py'),str(base/'contracts'),str(base/'launcher')],check=True)
