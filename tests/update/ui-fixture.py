#!/usr/bin/env python3
"""Prepara QA desktop assinado, com payload falso. Nunca enviar estes XEX ao console."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('directory',type=Path)
p.add_argument('--key',type=Path,required=True)
p.add_argument('--build',type=int,default=2)
p.add_argument('--version',default='0.3.1')
a=p.parse_args();d=a.directory.resolve();d.mkdir(parents=True,exist_ok=True)
for font in (ROOT/'client/assets').glob('inter-*.ttf'):shutil.copyfile(font,d/font.name)
(d/'settings.txt').write_text('lang=pt\nserver=off\ncols=4\nquality=720\nsound=tv\n')
rules=json.loads((ROOT/'client/src/update/rules-default.json').read_text());rules['version']=2
(d/'rules.json').write_text(json.dumps(rules,ensure_ascii=False,indent=2)+'\n')
(d/'tube360.xex').write_bytes(b'XEX2old desktop fixture')
(d/'app.xex').write_bytes(b'XEX2desktop fixture '+b'X'*(1200*1024))
(d/'home.html').write_text('<script>ytcfg.set({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"fixture"}}})</script>')
ids=['4p_9OPKgcDE','abcdefghijk','bcdefghijkl','cdefghijklm','defghijklmn','efghijklmno','fghijklmnop','ghijklmnopq']
titles=['Tube360 · seu console conectado','Futebol em campo','Música para ouvir agora','Uma viagem pelo Brasil','Tecnologia no dia a dia','Cultura e histórias','Aprendendo algo novo','Mais um vídeo para assistir']
(d/'browse.json').write_text(json.dumps({'contents':{'rows':[{'videoRenderer':{'videoId':i,'title':{'simpleText':title}}} for i,title in zip(ids,titles)]}},ensure_ascii=False))
# Asset público do próprio repo: o replay não acessa sites externos.
try:
 from PIL import Image
 Image.open(ROOT/'docs/img/hero.jpg').resize((320,180)).save(d/'thumb.jpg')
except ImportError:
 shutil.copyfile(ROOT/'docs/img/hero.jpg',d/'thumb.jpg')
cmd=[sys.executable,str(ROOT/'tools/sign-updates.py'),'--key',str(a.key)]
subprocess.run(cmd+['sign',str(d/'rules.json')],check=True)
subprocess.run(cmd+['manifest',str(d/'app.xex'),'--build',str(a.build),'--version',a.version,
 '--url','https://github.com/PedroHenrique0713/tube360/releases/download/v0.3.1/tube360.xex',
 '--notes-en','YouTube fixes and automatic app recovery. Download once and reopen Tube360 to install.',
 '--notes-pt','Correções do YouTube e recuperação automática do app. Baixe e reabra o Tube360 para instalar.',
 '--output',str(d/'app.json')],check=True)
print(d)
