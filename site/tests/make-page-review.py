"""Compose before/after evidence; originals stay unchanged and individually linked."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import hashlib,json
ROOT=Path(__file__).resolve().parents[2];WORK=ROOT/'site/.work/adendo-1';OUT=WORK/'comparisons';OUT.mkdir(exist_ok=True)
font=ImageFont.truetype(str(ROOT/'client/assets/inter-600.ttf'),22)
records=[];sections=[]
for lang in ['en','pt-BR']:
 for width,height in [(1366,650),(1920,1080),(390,844)]:
  prefix=f'{lang}-{width}';files={phase:WORK/phase/f'{prefix}-top.png' for phase in ['before','after']}
  board=Image.new('RGB',(width*2+48,height+80),'#111116');draw=ImageDraw.Draw(board)
  draw.text((16,12),f'Antes · {lang} · {width}×{height}',font=font,fill='#f5f5f7');draw.text((width+32,12),f'Depois · {lang} · {width}×{height}',font=font,fill='#f5f5f7')
  for x,phase in [(16,'before'),(width+32,'after')]:
   with Image.open(files[phase]) as im:assert im.size==(width,height);board.paste(im.convert('RGB'),(x,64))
  board.save(OUT/(prefix+'.png'),optimize=True)
  record={'lang':lang,'width':width,'height':height,'board':str(OUT/(prefix+'.png')),'images':{}}
  for phase in ['before','after']:
   for kind in ['top','full']:
    p=WORK/phase/f'{prefix}-{kind}.png';record['images'][phase+'_'+kind]={'file':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
  records.append(record)
  gallery=''.join(f'<figure><figcaption>{label}</figcaption><a href="{phase}/{prefix}-full.png"><img src="{phase}/{prefix}-full.png" loading="lazy" alt="Página inteira {label.lower()} em {lang}, largura {width}"></a></figure>' for phase,label in [('before','Antes'),('after','Depois')])
  details=''.join(f'<a href="after/{prefix}-{name}.png">{label}</a>' for name,label in [('latest-news','Resumo'),('features','Recursos'),('releases','Histórico'),('install','Instalação'),('credits','Rodapé')])
  sections.append(f'<section><h2>{lang} · {width}×{height}</h2><p><a href="comparisons/{prefix}.png">Prancha do topo</a> · {details}</p><details><summary>Abrir comparação da página inteira</summary><div class="pair">{gallery}</div></details></section>')
(WORK/'index.html').write_text('''<!doctype html><html lang="pt-BR"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>F49 — adendo 1, antes e depois</title><style>body{margin:24px;background:#0b0b0e;color:#f5f5f7;font:16px/1.7 system-ui}h1{font-size:28px}h2{font-size:22px}a{color:#ff9c96}p a{margin-right:12px}section{margin:40px 0;border-top:1px solid #35353e;padding-top:16px}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:20px;align-items:start}figure{margin:0}img{width:100%;height:auto;border:1px solid #35353e}figcaption{font-weight:600;margin-bottom:10px}summary{cursor:pointer}p{max-width:1000px}</style><h1>Tube360 — adendo 1</h1><p>Antes e depois em seis combinações de idioma e resolução. As capturas originais ficam preservadas; clique para abrir em tamanho integral. As pranchas do topo apenas compõem os screenshots lado a lado. As comparações completas abrem abaixo.</p><p><a href="../rc4-review/pranchas/index.html">App real × demo atualizada: Configurações em vermelho e capas de música sem texto</a></p>'''+''.join(sections),encoding='utf-8')
(WORK/'comparison-manifest.json').write_text(json.dumps(records,ensure_ascii=False,indent=2)+'\n')
print('6 before/after boards and full-page comparisons:',WORK/'index.html')
