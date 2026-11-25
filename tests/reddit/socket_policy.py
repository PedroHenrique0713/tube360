"""Compila o socket.c realmente preparado; só NetDll é fake. Sem sockets de rede reais."""
import argparse
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('evidence',type=Path);p.add_argument('--sdk',type=Path,default=ROOT/'.work/native-display/xsdk');p.add_argument('--source-sdk',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
# Aplica do zero sobre as três fontes originais em um diretório descartável e exige idempotência.
spec=importlib.util.spec_from_file_location('prepare',ROOT/'tools/prepare-boot-diagnostics.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
original=a.source_sdk.resolve()
with tempfile.TemporaryDirectory(prefix='tube360-socket-policy-') as tmp:
    work=Path(tmp);sdk=work/'sdk';(sdk/'src').mkdir(parents=True)
    for name in ('socket.c','start.c','log.c'):shutil.copy(original/'src'/name,sdk/'src'/name)
    module.prepare(sdk);first={f.name:f.read_bytes() for f in (sdk/'src').glob('*.c')};module.prepare(sdk)
    assert first=={f.name:f.read_bytes() for f in (sdk/'src').glob('*.c')}
    include=['-I'+str(a.sdk.resolve()/'include'),'-I'+str(a.sdk.resolve()/'src')]
    obj=work/'socket.o';exe=work/'test'
    names='socket bind connect listen accept send recv sendto recvfrom shutdown setsockopt getsockopt getsockname getpeername fcntl poll select'.split()
    renamed=['-D'+name+'=tube360_test_'+name for name in names]
    subprocess.run(['cc','-std=c11','-D_GNU_SOURCE',*renamed,'-ffunction-sections','-fdata-sections',*include,'-c',str(sdk/'src/socket.c'),'-o',str(obj)],check=True)
    subprocess.run(['c++','-std=c++17','-ffunction-sections',*include,str(ROOT/'tests/reddit/socket_policy.cpp'),str(obj),'-pthread','-Wl,--gc-sections','-o',str(exe)],check=True)
    for mode in ('normal','limits','unsupported','not-supported','denied','startup-denied','wsa-failed'):
        run=subprocess.run([str(exe),mode],check=True,capture_output=True,text=True)
        (a.evidence/(mode+'.log')).write_text(run.stdout)
        if mode=='limits':assert 'network.xnet.default' in run.stdout
        if mode in ('unsupported','not-supported'):assert 'trying standard socket; TLS unchanged' in run.stdout
        if mode=='denied':assert 'permission errors not bypassed' in run.stdout
        print(run.stdout.strip(),flush=True)
print('PASS SDK patch idempotence and seven actual socket.c paths',flush=True)
