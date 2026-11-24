#!/usr/bin/env python3
"""Exercita o verificador C++ com assinaturas reais de chaves descartáveis."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]

def run(*args):
    return subprocess.run([str(x) for x in args], check=True, stdout=subprocess.PIPE).stdout

with tempfile.TemporaryDirectory(prefix='tube360-update-') as temp:
    base = Path(temp)
    key = base/'key.pem'
    run(sys.executable, ROOT/'tools/sign-updates.py', '--key', key, 'keygen')
    public = run('openssl','pkey','-in',key,'-pubout','-outform','DER')[-32:]
    (base/'public.txt').write_text(public.hex())
    defaults=json.loads((ROOT/'client/src/update/rules-default.json').read_text())
    def sign(name, data):
        path=base/name
        path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
        run(sys.executable, ROOT/'tools/sign-updates.py','--key',key,'sign',path)
    for version in (2,3,4):
        rule=json.loads(json.dumps(defaults));rule['version']=version
        rule['players'][0]['name']='REMOTE_PLAYER'
        rule['players'][0]['code']='102'
        rule['players'][0]['version']='7.8'
        rule['players'][0]['agent']='RemoteFixture/7.8'
        rule['timestampMarkers']=['remoteTimestamp:']
        sign('rules'+str(version)+'.json',rule)
    invalid=json.loads(json.dumps(defaults));invalid['version']=5;invalid['limits']['retries']=100
    sign('limit.json',invalid)
    invalid['limits']['retries']=2;invalid['strings']['webOrigin']='https://www.youtube.com@evil.example'
    sign('domain.json',invalid)
    invalid['strings']['webOrigin']='https://www.youtube.com';invalid['script']='eval(1)'
    sign('script.json',invalid)
    payload=base/'download.xex';payload.write_bytes(b'XEX2'+b'A'*(10*1024*1024-4))
    run(sys.executable,ROOT/'tools/sign-updates.py','--key',key,'manifest',payload,
        '--build','2','--version','0.3.1','--url','https://github.com/PedroHenrique0713/tube360/releases/download/v0.3.1/tube360.xex',
        '--notes-en','Fixture update.','--notes-pt','Atualização de teste.','--output',base/'app.json')
    print(run(sys.argv[1],base,ROOT/'client/src/update/rules-default.json').decode(),end='')
    if len(sys.argv)>2:
        print(run(sys.argv[2],base).decode(),end='')
    print('Signed update contracts passed (OpenSSL → C++, disposable private key).')
