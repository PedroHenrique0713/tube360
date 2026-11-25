"""Executa código nativo de diagnóstico com exports/NT I/O simulados, sem console."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('evidence',type=Path)
parser.add_argument('--sdk',type=Path,default=ROOT/'.work/native-display/xsdk')
a=parser.parse_args();a.evidence=a.evidence.resolve();a.evidence.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='tube360-boot-native-') as tmp:
    work=Path(tmp);include=['-I'+str(ROOT/'client/src'),'-I'+str(a.sdk.resolve()/'include')]
    objects=[]
    for name in ('boot_log','boot_early'):
        obj=work/(name+'.o');objects.append(str(obj))
        subprocess.run(['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-D__XSDK__',*include,'-c',str(ROOT/f'client/src/{name}.c'),'-o',str(obj)],check=True)
    binary=work/'native'
    subprocess.run(['c++','-std=c++17','-D__XSDK__',*include,str(ROOT/'tests/reddit/boot_native.cpp'),str(ROOT/'client/src/boot_platform.cpp'),*objects,'-pthread','-Wl,--wrap=fopen','-Wl,--wrap=fsync','-o',str(binary)],check=True)
    for mode in ('usb','hdd','component','unknown-av','absent','long','sync-fail'):
        folder=a.evidence/mode;folder.mkdir(exist_ok=True)
        for old in folder.glob('b*.log'):old.unlink()
        run=subprocess.run([str(binary),mode,str(folder)],check=True,capture_output=True,text=True)
        (folder/'result.txt').write_text(run.stdout)
        boot=next(folder.glob('b*app.log'),None) or folder/'ba.log'
        text=boot.read_text();raw=(folder/'boot-crt.log' if mode!='long' else folder/'bc.log').read_text()
        assert 'begin crt.entry' in raw and 'end crt.locks' in raw,raw
        assert 'begin entry' in text and 'begin fixture.stop' in text,text
        assert 'exploit=unknown' in text and 'required=0' in text,text
        assert 'syncs=0' not in run.stdout,run.stdout
        if mode=='absent':assert 'connector=' not in text and 'present=1' not in text,text
        else:assert 'version=2.0.17559.0' in text,text
        if mode in ('usb','long'):assert 'drive=USB0' in text,text
        if mode=='hdd':assert 'drive=HDD' in text,text
        if mode=='component':assert 'connector=component' in text,text
        if mode=='usb':assert 'connector=HDMI' in text,text
        if mode=='unknown-av':assert 'avpack=123 connector=other/unknown' in text,text
        if mode=='sync-fail':assert 'durability=failed' in text and 'userspace flush only' in text,text
        if mode=='long':assert 'short_name=1' in text and 'ba.log' in run.stdout,text
        print('PASS native boot',mode,run.stdout.strip(),flush=True)
