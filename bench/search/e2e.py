"""Exercise controller input in the actual offscreen PC app, with all HTTPS closed."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
work=Path(sys.argv[1]).resolve()
wanted=set(sys.argv[2:])
results=[]
def run(name,lang='pt',height=720,large=False,keys='',seconds=2.8,**extra):
    if wanted and name not in wanted: return
    assets=work/name
    assets.mkdir(exist_ok=True)
    for font in (ROOT/'client/assets').glob('*.ttf'): shutil.copyfile(font,assets/font.name)
    (assets/'settings.txt').write_text(f'lang={lang}\ncols=4\nsound=tv\noutput_height={height}\nlarge_text={"on" if large else "off"}\n')
    env=dict(os.environ,HOME=str(assets/'home'),XDG_CONFIG_HOME=str(assets/'config'),XDG_CACHE_HOME=str(assets/'cache'),
        SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',XT_ASSETS=str(assets),XT_CAST_DISABLE='1',
        XT_SEARCH_REPLAY=str(work/'replay'),XT_ACCOUNT_FIXTURES=str(work/'accounts'),XT_SCREEN='search',XT_SEARCH_TEXT='astronomia',
        XT_KEYS=keys,XT_KEYS_MS='120',XT_SHOT=str(assets/'screen.ppm'),XT_SHOT_MS=str(int(seconds*1000)),
        XT_SEARCH_AUDIT=str(assets/'requests.txt'),XT_SEARCH_METRICS=str(assets/'frames.json'))
    env.update(extra)
    (assets/'requests.txt').unlink(missing_ok=True)
    with (assets/'app.log').open('w') as log:
        subprocess.run([str(work/'tube360-search-replay')],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=45,check=True)
    text=(assets/'app.log').read_text()
    assert 'captura:' in text,text[-3000:]
    assert 'atlas full' not in text,text[-2000:]
    if lang=='ar': assert 'Arabic fonts:' in text,text[-2000:]
    subprocess.run(['magick',str(assets/'screen.ppm'),str(assets/'screen.png')],check=True)
    audit=(assets/'requests.txt').read_text() if (assets/'requests.txt').exists() else ''
    if name.startswith('suggestions-'): assert 'sugestoes=8' in text,text[-2000:]
    if name.startswith('filters-'): assert 'painel=1 tipo=1 duracao=1 data=2 ordem=1' in text,text[-2000:]
    if name.startswith('results-'):
        assert 'captura navegação: search' in text and 'CAMSBggDEAEYBA%3D%3D' in audit,text[-3000:]+audit[-3000:]
    if name=='suggestion-accept': assert 'captura navegação: search' in text and 'astronomia musica' in audit,audit[-3000:]
    if name=='recent-accept': assert 'captura navegação: search' in text and 'palmeiras 4x1 corinthians' in audit and 'recentes=3' in text,audit[-3000:]+text[-2000:]
    if name in ('offline','typing-slow','typing-fast'): assert 'tela 1, vídeo' in text,text[-2000:]
    if name=='offline': assert 'sugestoes=0' in text and 'painel=0' in text,text[-2000:]
    if name=='cancel-filters': assert 'painel=0 tipo=0 duracao=0 data=0 ordem=0' in text,text[-2000:]
    if name=='clear-filters': assert 'captura navegação: search' in text and 'ordem=0' in text,text[-2000:]
    if name in ('search-back','list-back'): assert 'tela 0, vídeo' in text,text[-2000:]
    if name=='offline-search': assert 'captura navegação: search' in text,text[-2000:]
    if name=='order-cycle-relevance':
        searches=[json.loads(line.split('\t',1)[1]) for line in audit.splitlines() if line.startswith('body\t') and '"query"' in line]
        assert len(searches)==1 and searches[0].get('params')=='EgYIAxABGAQ%3D',searches
        assert 'captura navegação: search' in text and 'tipo=1 duracao=1 data=2 ordem=0' in text,text[-2000:]
    if name=='clear-filters':
        searches=[json.loads(line.split('\t',1)[1]) for line in audit.splitlines() if line.startswith('body\t') and '"query"' in line]
        assert len(searches)==2 and 'params' in searches[0] and 'params' not in searches[-1],searches
    if name=='channel-open': assert 'captura navegação: channel' in text and 'tela 4, vídeo ,' in text,text[-3000:]
    if name=='playlist-open': assert 'captura navegação: playlist' in text and 'tela 4, vídeo ,' in text,text[-3000:]
    if name in ('channel-open','playlist-open'):
        bodies=[json.loads(line.split('\t',1)[1]) for line in audit.splitlines() if line.startswith('body\t')]
        expected='UCfOj-wa2wA7aI0UjZYeBgHg' if name=='channel-open' else 'VLPLUBR4s0w2cG1kyI2MDBJV0J3B5HuJoB7g'
        assert any(body.get('browseId')==expected for body in bodies),bodies
        if name=='channel-open': assert all(body.get('browseId')==expected for body in bodies if str(body.get('browseId','')).startswith('UC')),bodies
        assert 'player: pedido' not in text,text[-3000:]
    metrics=json.loads((assets/'frames.json').read_text()) if (assets/'frames.json').exists() else None
    results.append(dict(name=name,image=str(assets/'screen.png'),frames=metrics))
    print(name,'OK',metrics,flush=True)

# LB, video, duration short, week, view-count order. Apply at row 4.
draft='[RDRDRRDR'
for lang in ('pt','en','es','ar'):
    run('suggestions-'+lang,lang=lang)
    run('filters-'+lang,lang=lang,keys=draft)
    run('results-'+lang,lang=lang,keys=draft+'DAS',seconds=3.4)
for lang in ('pt','en','es','ar'):
    run('suggestions-crt-large-'+lang,lang=lang,height=480,large=True)
    run('filters-crt-large-'+lang,lang=lang,height=480,large=True,keys=draft)
    run('results-crt-large-'+lang,lang=lang,height=480,large=True,keys=draft+'DAS',seconds=3.4)
run('suggestion-accept',keys='........RRRRRRRRRRDA',seconds=3.5)
run('suggestions-arabic-query',lang='ar',XT_SEARCH_TEXT='علوم')
run('suggestions-arabic-query-crt-large',lang='ar',height=480,large=True,XT_SEARCH_TEXT='علوم',keys='........RRRRRRRRRRD')
run('recent-accept',keys='RRRRRRRRRRA',seconds=2.8,XT_SEARCH_ERROR='1')
run('offline',XT_SEARCH_ERROR='1')
run('offline-search',keys='S',XT_SEARCH_ERROR='1')
run('cancel-filters',keys=draft+'B')
run('order-cycle-relevance',keys=draft+'RDAS',seconds=3.4)
run('clear-filters',keys=draft+'DAS...[DDDDRA',seconds=4)
run('search-back',keys='B')
run('list-back',keys='........RRRRRRRRRRLB')
# Recorded page starts with two playlists, then videos, then a channel. Indices verified by contract test.
run('playlist-open',keys='S......A',seconds=3)
run('channel-open',keys='S......DDDA',seconds=3)
run('baseline',seconds=4,XT_SEARCH_ERROR='1')
run('typing-fast',keys='A'*30,seconds=4,XT_KEYS_MS='60',XT_SEARCH_SYNTHETIC_QUERY='1')
run('typing-slow',keys='A'*30,seconds=4,XT_KEYS_MS='60',XT_SEARCH_SYNTHETIC_QUERY='1',XT_SEARCH_DELAY_MS='1800')
manifest=work/'screenshots.json'
if wanted and manifest.exists():
    by_name={entry['name']:entry for entry in json.loads(manifest.read_text())}
    by_name.update({entry['name']:entry for entry in results})
    results=list(by_name.values())
manifest.write_text(json.dumps(results,indent=2))
