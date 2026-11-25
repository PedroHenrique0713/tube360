"""Política C real com export de dados/arquivo simulado; nenhum acesso a hardware."""
import argparse
import json
import socket
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('evidence',type=Path);p.add_argument('--sdk',type=Path,default=ROOT/'.work/native-display/xsdk');a=p.parse_args();a.evidence=a.evidence.resolve();a.evidence.mkdir(parents=True,exist_ok=True)
cases=[(name,f'{i:x}1234567',0,None,0,int(i in (5,6)),name) for i,name in enumerate(('Xenon','Zephyr','Falcon','Jasper','Trinity','Corona','Winchester'))]
cases += [
    ('unknown','f1234567',0,None,0,0,'unknown'),('export-absent','30000000',1,None,0,0,'unknown'),('module-absent','30000000',2,None,0,0,'unknown'),
    ('auto','30000000',0,b'auto\n',0,0,'Jasper'),('safe','60000000',0,b'safe\n',0,0,'Winchester'),
    ('native-jasper','30000000',0,b'native\r\n',0,1,'Jasper'),('native-unknown','f0000000',0,b' native \n',0,1,'unknown'),
    ('auto-corona','50000000',0,b'auto\n',0,1,'Corona'),('safe-corona','50000000',0,b'safe',0,0,'Corona'),
    ('empty','30000000',0,b'',0,0,'Jasper'),('garbage','30000000',0,b'native extra',0,0,'Jasper'),('case','30000000',0,b'NATIVE',0,0,'Jasper'),
    ('oversize','30000000',0,b'native'+b' '*70,0,0,'Jasper'),('nul','30000000',0,b'native\0',0,0,'Jasper'),('denied','60000000',0,b'native',1,0,'Winchester')]
with tempfile.TemporaryDirectory(prefix='tube360-video-policy-') as tmp:
    work=Path(tmp);exe=work/'test'
    subprocess.run(['cc','-std=c11','-D__XSDK__','-idirafter',str(a.sdk.resolve()/'include'),'-I'+str(ROOT/'client/src'),str(ROOT/'client/src/video_policy.c'),str(ROOT/'tests/reddit/video_policy.c'),str(a.sdk.resolve()/'src/log.c'),'-Wl,--wrap=fopen','-o',str(exe)],check=True)
    for name,flags,missing,data,denied,expected,board in cases:
        folder=a.evidence/name;folder.mkdir(exist_ok=True);config=folder/'video-driver.txt';config.unlink(missing_ok=True)
        if data is not None:config.write_bytes(data)
        (folder/'input.json').write_text(json.dumps(dict(flags=flags,missing=missing,file_hex=data.hex() if data is not None else None,denied=denied,expected=expected,board=board),indent=2)+'\n')
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as receiver:
            receiver.bind(('127.0.0.1',0));receiver.settimeout(.1)
            (folder/'net_log.txt').write_text(f'127.0.0.1 {receiver.getsockname()[1]}\n')
            run=subprocess.run([str(exe),flags,str(missing),str(denied),str(expected)],cwd=folder,check=True,capture_output=True,text=True)
            packets=[]
            while True:
                try:packets.append(receiver.recv(4096)[4:].decode())
                except TimeoutError:break
        udp=''.join(packets);(folder/'udp.log').write_text(udp)
        summary=f'video: model={board}'
        for sink in (udp,(folder/'sdk-normal.log').read_text()):
            assert summary in sink and f'path={"native" if expected else "safe"}' in sink,(name,sink)
        (folder/'boot.log').write_text(run.stdout)
        assert f'model={board};' in run.stdout and f'selected={"native" if expected else "safe"}' in run.stdout,run.stdout
        assert run.stdout.count('begin video.policy-file')==1,run.stdout
        print('PASS policy',name,board,'native' if expected else 'safe',flush=True)
