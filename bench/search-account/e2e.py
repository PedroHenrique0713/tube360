"""Actual PC app, controller and Vulkan screenshots, isolated from every personal store."""
import http.server
import threading
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
work=Path(sys.argv[1]).resolve();wanted=set(sys.argv[2:]);results=[]
# a rádio de continuação da reprodução automática parte da ÚLTIMA faixa da fila (F12audio003): grava a mesma resposta para ela
_manifest=work/'replay'/'responses.json';_data=json.loads(_manifest.read_text())
if not any(r.get('fields',{}).get('playlistId')=='RDAMVMF12audio003' for r in _data['requests']):
    _data['requests']+=[dict(r,fields=dict(r['fields'],playlistId='RDAMVMF12audio003')) for r in _data['requests'] if r.get('fields',{}).get('playlistId')=='RDAMVMF12audio001']
    _manifest.write_text(json.dumps(_data))
def run(name,lang='pt',signed=True,keys='',ms=2400,settings='',**extra):
    if wanted and name not in wanted:return
    case=work/name;assets=case/'assets';assets.mkdir(parents=True,exist_ok=True)
    for font in (ROOT/'client/assets').glob('*.ttf'):shutil.copy(font,assets)
    (assets/'settings.txt').write_text('lang='+lang+'\ncols=4\nsound=tv\nvideo_mode=auto\n'+settings)
    for filename in ('local-library.json','yt-console-account.json'):(assets/filename).unlink(missing_ok=True)
    if signed:
        (assets/'yt-console-account.json').write_text(json.dumps({'access_token':'console-fixture-token','refresh_token':'fixture-refresh','expires_at':4070908800,'client':{'client_id':'fixture-client','client_secret':'fixture-secret'},'name':'Demo'}))
    env={k:v for k,v in os.environ.items() if not k.startswith(('XT_','F47_'))}
    env.update(HOME=str(case/'home'),XDG_CONFIG_HOME=str(case/'config'),XDG_CACHE_HOME=str(case/'cache'),
        SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',LP_NUM_THREADS='2',XT_CAST_DISABLE='1',XT_ASSETS=str(assets),
        F47_REPLAY=str(work/'replay'),XT_ACCOUNT_FIXTURES=str(work/'accounts'),F47_AUDIT=str(case/'audit.txt'),
        XT_SHOT=str(case/'screen.ppm'),XT_SHOT_MS=str(ms),XT_KEYS=keys,XT_KEYS_MS='180',XT_SHOT_FRAME='2147483647')
    server=None;server_requests=[]
    if extra.pop('F47_TEST_SERVER',None):
        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self,*args):pass
            def do_GET(self):
                server_requests.append(self.path);data=b'ok\t0\tDemo PC fixture\n'
                self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);server.daemon_threads=True
        thread=threading.Thread(target=server.serve_forever);thread.start()
        (assets/'server.txt').write_text('127.0.0.1 %d\n'%server.server_port)
    env.update(extra);(case/'audit.txt').unlink(missing_ok=True)
    try:
        with (case/'app.log').open('w') as output:
            subprocess.run([str(work/'tube360-replay')],env=env,cwd=case,stdout=output,stderr=subprocess.STDOUT,timeout=45,check=True)
    finally:
        if server:server.shutdown();server.server_close();thread.join()
    if server:assert not any(path.startswith('/api/nav') for path in server_requests),server_requests
    log=(case/'app.log').read_text();audit=(case/'audit.txt').read_text()
    assert 'captura:' in log and 'atlas full' not in log,log[-3000:]
    assert 'PRIVATE_TOKEN' not in log and 'private@example.test' not in log,log[-3000:]
    if name.startswith('search'):
        requests=[line for line in audit.splitlines() if 'search=1' in line]
        assert requests and all('bearer=0 cookieless=1' in line for line in requests),audit
        if 'failure' in name:
            assert 'rota=WEB/search http=200 codigo=youtube_unavailable api_code=400' in log,log[-3000:]
            assert 'navegação search: 0 itens' in log,log[-3000:]
        else:assert re.search(r'navegação search: [1-9]\d* itens',log),log[-3000:]
        if 'filtered' in name:assert any('params=1' in line for line in requests),audit
    if name.startswith('music') and 'library' not in name:
        assert 'music=tv' in log and 'navegação music: 12 itens' in log,log[-3000:]
    if name.startswith('music-library'):
        assert 'navegação music: 6 itens' in log,log[-3000:]
    if name=='autoplay-busy':  # a letra (pedido atrasado 15 s) segura o worker além do fim da fila: a rádio sai depois e a Música continua
        assert log.count('musica: primeiro audio')>=4 and 'navegacao: Player -> ' not in log and 'catalogo falha' not in log.split('captura:')[0].split('/api/nav/play')[-1],log[-3500:]
    if name.startswith('warning'):
        assert 'manual 480p itag 135 sob pressao sustentada' in log and 'aviso=1' in log,log[-4000:]
        assert 'qualidade: fonte ' not in log and log.count('console: MP4')==1,log[-4000:]
    if name.startswith(('quality','player')):
        assert 'video-memoria: 1920x800' in log and 'fonte=800' in log and 'estado=3' in log,log[-3500:]
        if name.startswith('quality'):assert 'painel=2' in log and '1080p30 |' in log,log[-3500:]
    with Image.open(case/'screen.ppm') as im:
        if name.startswith('music-') and 'crt' not in name:
            # Atalhos da Biblioteca: só aparecem na aba Biblioteca (cartões de 190 px a cada 214). A arte deles usa o acento A2; a arte
            # genérica `art.png` (degradê roxo e laranja) não tem pixel dessa cor, então o critério não passa com qualquer capa.
            def accent(box):
                return sum(abs(r-255)<14 and abs(g-90)<14 and abs(b-82)<14 for r,g,b in im.convert('RGB').crop(box).get_flattened_data())
            tiles=[accent((136+i*214,126,136+i*214+190,316)) for i in range(5)]
            if name.startswith('music-library'):assert all(count>150 for count in tiles),(name,'missing shortcut artwork',tiles)
            else:assert all(count<=150 for count in tiles),(name,'the Home must not show the Library shortcuts',tiles)  # contraprova: o mesmo critério reprova a Home
        im.save(case/'screen.png')
    results.append({'name':name,'image':str(case/'screen.png')})
    print(name,'PASS',flush=True)
draft='[RDRDRRDR'
for lang in ('pt','en','es','ar'):
    run('search-'+lang,lang=lang,XT_NAV='search',XT_NAV_QUERY='astronomia')
    run('search-filtered-'+lang,lang=lang,keys=draft+'DAS',ms=3400,XT_SCREEN='search',XT_SEARCH_TEXT='astronomia')
    run('music-'+lang,lang=lang,XT_NAV='music',XT_MUSIC='1')
    run('music-library-'+lang,lang=lang,XT_NAV='music',XT_NAV_QUERY='library',XT_MUSIC='1')
    run('player-'+lang,lang=lang,signed=False,keys='....DLL',ms=3000,XT_PLAY='R6MlUcmOul8')
    run('quality-'+lang,lang=lang,signed=False,keys='....Y',ms=2600,XT_PLAY='R6MlUcmOul8')
    run('warning-'+lang,lang=lang,signed=False,ms=18000,settings='video_mode=480p\n',XT_PLAY='R6MlUcmOul8',F47_SLOW_DECODE='1')
    run('music-crt-'+lang,lang=lang,settings='output_height=480\nlarge_text=on\n',XT_NAV='music',XT_MUSIC='1')
for status in ('400','401','403','200'):
    run('search-bearer-'+status,F47_WEB_STATUS=status,XT_NAV='search',XT_NAV_QUERY='astronomia')
    run('search-guest-'+status,signed=False,F47_WEB_STATUS=status,XT_NAV='search',XT_NAV_QUERY='astronomia')
run('search-server',settings='server=on\n',F47_TEST_SERVER='1',XT_NAV='search',XT_NAV_QUERY='astronomia')
run('search-server-failure',settings='server=on\n',F47_TEST_SERVER='1',F47_FAIL_SEARCH='1',F47_WEB_STATUS='200',XT_NAV='search',XT_NAV_QUERY='astronomia')
run('search-failure',F47_FAIL_SEARCH='1',F47_WEB_STATUS='200',XT_NAV='search',XT_NAV_QUERY='astronomia')
run('autoplay-busy',keys='...A........RRRRRRRUR',ms=26000,XT_NAV='radio',XT_NAV_ID='F12audio001',XT_MUSIC='1',F47_SLOW_NEXT_AFTER='3',F47_SLOW_NEXT_MS='15000')
manifest=work/'screenshots.json'
if wanted and manifest.exists():
    previous={entry['name']:entry for entry in json.loads(manifest.read_text())};previous.update({entry['name']:entry for entry in results});results=list(previous.values())
manifest.write_text(json.dumps(results,indent=2))
