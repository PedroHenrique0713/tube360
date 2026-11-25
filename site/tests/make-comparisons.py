"""Lay out original app/demo captures side by side without retouching either screen."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageChops, ImageStat
import hashlib, html, json
ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/'site/.work/rc4-review';OUT=WORK/'pranchas';OUT.mkdir(exist_ok=True)
font=ImageFont.truetype(str(ROOT/'client/assets/inter-600.ttf'),22)
labels={'home':'Home','search':'Busca com sugestões','filters':'Filtros LB','subscriptions':'Inscrições','library':'Biblioteca','music':'Música da conta','phone':'Transmitir do celular','settings-top':'Configurações — início','settings-middle':'Configurações — meio','settings-bottom':'Configurações — última linha','player':'Player com SponsorBlock','quality':'Qualidade Y — início','quality-bottom':'Qualidade Y — última opção','channel':'Canal','shorts':'Shorts'}
records=[];body=[]
for key,label in labels.items():
 native=WORK/'native'/key/'screen.png';demo=WORK/'demo'/(key+'.png')
 with Image.open(native) as app,Image.open(demo) as browser:
  assert app.size==(1280,720),(key,app.size)
  assert browser.size==(1280,720),(key,browser.size)
  board=Image.new('RGB',(2608,800),'#111116');draw=ImageDraw.Draw(board)
  draw.text((16,10),label,font=font,fill='#f5f5f7')
  draw.text((16,42),'App real rc4 · conta fictícia · 1280×720',font=font,fill='#a1a1aa')
  draw.text((1312,42),'Demo HTML · 1280×720',font=font,fill='#a1a1aa')
  board.paste(app.convert('RGB'),(16,76));board.paste(browser.convert('RGB'),(1312,76));board.save(OUT/(key+'.png'),optimize=True)
  mae=sum(ImageStat.Stat(ImageChops.difference(app.convert('RGB'),browser.convert('RGB'))).mean)/3
  records.append(dict(screen=key,label=label,app=str(native),demo=str(demo),board=str(OUT/(key+'.png')),app_sha256=hashlib.sha256(native.read_bytes()).hexdigest(),demo_sha256=hashlib.sha256(demo.read_bytes()).hexdigest(),mean_absolute_rgb_difference=round(mae,3)))
 body.append(f'<section id="{key}"><h2>{html.escape(label)}</h2><a href="{key}.png"><img src="{key}.png" alt="App real à esquerda e demo à direita: {html.escape(label)}" loading="lazy"></a><p><a href="../native/{key}/screen.png">App real</a> · <a href="../demo/{key}.png">Demo</a> · <a href="{key}.png">Prancha em tamanho integral</a></p></section>')
(OUT/'index.html').write_text('''<!doctype html><html lang="pt-BR"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>F49 — app rc4 × demo</title><style>body{margin:24px;background:#0b0b0e;color:#f5f5f7;font:16px/1.6 system-ui}h1{font-size:28px}h2{font-size:21px}section{margin:42px 0}img{width:100%;height:auto;border:1px solid #35353e}a{color:#ff867e}p{max-width:950px}</style><h1>Tube360 rc4 × demo navegável</h1><p>Capturas em PT-BR. O app de PC usa o desenho de produção, respostas gravadas, conta fictícia e estado isolado. A demo reproduz os mesmos dados. Clique na prancha para examinar os dois lados em resolução integral. Os screenshots foram compostos sem retoque. A diferença RGB é apenas diagnóstico; não substitui a conferência visual.</p>'''+''.join(body),encoding='utf-8')
(WORK/'comparisons.json').write_text(json.dumps(records,ensure_ascii=False,indent=2)+'\n')
print(f'{len(records)} pranchas generated: {OUT / "index.html"}')
