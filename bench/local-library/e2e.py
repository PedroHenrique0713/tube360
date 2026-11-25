"""Production UI/player replay; isolated files, recorded channel DOM and synthetic media/art."""
import argparse
import json
import http.server
import threading
import urllib.parse
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('cases',nargs='*');p.add_argument('--prepare-only',action='store_true');args=p.parse_args()
work=args.directory.resolve();work.mkdir(parents=True,exist_ok=True)
replay=work/'replay';replay.mkdir(exist_ok=True)

def cmd(*a):subprocess.run(list(map(str,a)),check=True,cwd=ROOT)

def prepare():
    cmd('python3','bench/catalog/prepare-replay.py',replay)
    (replay/'home.html').write_text('<script>ytcfg.set({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"fixture"}}})</script>')
    (replay/'watch.html').write_text('<script>ytcfg.set({"VISITOR_DATA":"fixture","jsUrl":"/s/player/fixture/base.js"})</script>')
    (replay/'base.js').write_text('var a={signatureTimestamp:20775};')
    cards=[{'videoRenderer':{'videoId':f'video{i:06}', 'title':{'simpleText':title},'longBylineText':{'runs':[{'text':'Demo · Canal de bancada'}]},'lengthText':{'simpleText':'2:00'}}} for i,title in enumerate(['Demo · Vídeo para assistir depois','Demo · Retomar de onde parou','Demo · العربية على هذا الجهاز'],1)]
    (replay/'home.json').write_text(json.dumps({'contents':cards}))
    (replay/'empty.json').write_text('{"contents":[]}')
    # 120 s permits real seeks and resume boundaries, at a small decode cost.
    for name,opts in [('local-video.mp4',['-f','lavfi','-i','testsrc2=size=320x180:rate=30','-an','-c:v','libx264','-threads','2','-preset','ultrafast','-g','30']),('local-audio.m4a',['-f','lavfi','-i','sine=frequency=440:sample_rate=44100','-c:a','aac','-b:a','64000'])]:
        if not (replay/name).exists():cmd('ffmpeg','-v','error','-y',*opts,'-t','120','-movflags','+faststart',replay/name)
    player={'playabilityStatus':{'status':'OK'},'videoDetails':{'videoId':'video000001','title':'Demo · Local resume fixture','author':'Demo · Canal de bancada','lengthSeconds':'120'},'streamingData':{'expiresInSeconds':'3600','adaptiveFormats':[{'itag':160,'width':320,'height':180,'fps':30,'mimeType':'video/mp4; codecs="avc1.64001f"','url':'https://fixture.googlevideo.com/local-video.mp4'},{'itag':140,'bitrate':64000,'mimeType':'audio/mp4; codecs="mp4a.40.2"','url':'https://fixture.googlevideo.com/local-audio.m4a'}]}}
    (replay/'local-player.json').write_text(json.dumps(player))
    manifest=json.loads((replay/'responses.json').read_text())['requests']
    manifest=[r for r in manifest if r['url']!='https://www.youtube.com/']
    web='https://www.youtube.com/';prefix=[{'url':web,'file':'home.html'}]
    prefix += [{'url':web+f'youtubei/v1/{ep}?prettyPrint=false','file':file} for ep,file in [('player','local-player.json'),('next','empty.json'),('search','home.json')]]
    prefix += [{'url':web+'s/player/fixture/base.js','file':'base.js'}]
    for i in range(1,4):
        prefix += [{'url':web+f'watch?v=video{i:06}&bpctr=9999999999&has_verified=1','file':'watch.html'}, {'url':f'https://i.ytimg.com/vi/video{i:06}/mqdefault.jpg','file':'art.png'}]
    prefix += [{'url':f'https://fixture.googlevideo.com/{name}','file':name,'range':True} for name in ('local-video.mp4','local-audio.m4a')]
    # Route channel requests before the generic public Home browse reply.
    manifest=prefix+manifest+[{'url':web+'youtubei/v1/browse?prettyPrint=false','file':'home.json'}]
    shutil.copyfile(ROOT/'bench/local-library/fixtures/channel-shorts.json',replay/'channel-shorts.json')
    shorts=json.loads((replay/'channel-shorts.json').read_text())
    # Add a selected tab endpoint to the older recordings for controller navigation.
    for name in ('channel','channel-videos','channel-live','channel-playlists'):
        path=replay/(name+'.json');data=json.loads(path.read_text());data['contents']['twoColumnBrowseResultsRenderer']['tabs'].insert(2,{'tabRenderer':{'title':'Shorts','endpoint':{'commandMetadata':{'webCommandMetadata':{'url':'/@GoogleDevelopers/shorts'}},'browseEndpoint':{'browseId':'UC_x5XG1OV2P6uZZ5FSM9Ttw','params':'SHORTS_FIXTURE'}}}});path.write_text(json.dumps(data))
    manifest.insert(0,{'url':web+'youtubei/v1/browse?prettyPrint=false','fields':{'browseId':'UC_x5XG1OV2P6uZZ5FSM9Ttw','params':'SHORTS_FIXTURE'},'file':'channel-shorts.json'})
    def urls(x):
        if isinstance(x,dict):
            for k,v in x.items():
                if k=='url' and isinstance(v,str) and 'ytimg.com/' in v:yield v
                else:yield from urls(v)
        elif isinstance(x,list):
            for v in x:yield from urls(v)
    manifest += [{'url':u,'file':'art.png'} for u in urls(shorts)]
    # Explicitly simulated WebP/relative URL case, derived from the recorded video DOM.
    data=json.loads((replay/'channel-videos.json').read_text())
    def webp(x):
        if isinstance(x,dict):
            for k,v in x.items():
                if k=='url' and isinstance(v,str) and 'i.ytimg.com/vi/' in v:
                    vid=v.split('/vi/')[1].split('/')[0];x[k]=f'//i.ytimg.com/vi_webp/{vid}/hq720.webp'
                    manifest.insert(0,{'url':'https:'+x[k],'file':'unsupported.webp'})
                    manifest.append({'url':f'https://i.ytimg.com/vi/{vid}/mqdefault.jpg','file':'art.png'})
                else:webp(v)
        elif isinstance(x,list):
            for v in x:webp(v)
    webp(data);(replay/'channel-webp.json').write_text(json.dumps(data))
    cmd('magick','-size','320x180','gradient:#124748-#b34663',replay/'unsupported.webp')
    (replay/'responses.json').write_text(json.dumps({'requests':manifest}))
    accounts=work/'accounts';accounts.mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'bench/account/fixtures/responses.json',accounts/'responses.json');shutil.copyfile(ROOT/'bench/account/fixtures/thumb.png',accounts/'thumb.png')

if not (replay/'local-player.json').exists():prepare()
if args.prepare_only:raise SystemExit
cmd('python3','bench/catalog/build-replay.py',work)
results=[]

def seed():
    return {'version':1,'later':[dict(id='video000001',title='Demo · Vídeo para assistir depois',channel='Demo · Canal de bancada',duration=120,position=0,order=2),dict(id='video000002',title='Demo · Retomar de onde parou',channel='Demo · Canal de bancada',duration=120,position=0,order=1)],'history':[dict(id='video000001',title='Demo · Retomar de onde parou',channel='Demo · Canal de bancada',duration=120,position=30,order=3)]}

def run(name,lang='pt',large=False,height=720,keys='',seconds=3.5,saved=True,signed=False,assets=None,blocked=False,phone_control=False,**extra):
    if args.cases and name not in args.cases:return None
    assets=assets or work/name;assets.mkdir(exist_ok=True)
    for font in (ROOT/'client/assets').glob('*.ttf'):shutil.copyfile(font,assets/font.name)
    (assets/'settings.txt').write_text(f'lang={lang}\nserver={"auto" if phone_control else "off"}\nsound=tv\nsponsorblock=off\noutput_height={height}\nlarge_text={"on" if large else "off"}\n')
    if saved is True:(assets/'local-library.json').write_text(json.dumps(seed()))
    elif saved is False:(assets/'local-library.json').unlink(missing_ok=True)
    elif saved=='corrupt':(assets/'local-library.json').write_text('{broken')
    if signed:
        (assets/'yt-console-account.json').write_text(json.dumps({'access_token':'console-fixture-token','refresh_token':'fixture-refresh','expires_at':4070908800,'client':{'client_id':'fixture-client','client_secret':'fixture-secret'},'name':'Conta fictícia'}));(assets/'yt-console-account.json').chmod(0o600)
    if blocked:(assets/'local-library.json.tmp').mkdir(exist_ok=True)
    env={k:v for k,v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(work/'home'),XDG_CONFIG_HOME=str(work/'config'),XDG_CACHE_HOME=str(work/'cache'),LP_NUM_THREADS='2',SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',XT_CAST_DISABLE='1',XT_ASSETS=str(assets),XT_CATALOG_REPLAY=str(replay),XT_ACCOUNT_FIXTURES=str(work/'accounts'),XT_ACCOUNT_AUDIT=str(assets/'account-audit.log'),XT_KEYS=keys,XT_KEYS_MS='250',XT_SHOT=str(assets/'screen.ppm'),XT_SHOT_MS=str(int(seconds*1000)))
    env.update(extra)
    remote_server=None; remote_thread=None; unexpected=[]
    if phone_control:
        class RemoteReplay(http.server.BaseHTTPRequestHandler):
            sent=False
            def log_message(self,*a):pass
            def do_GET(self):
                route=urllib.parse.urlsplit(self.path).path
                if route=='/api/remote/poll':
                    body='none\n' if RemoteReplay.sent else 'play\tvideo000001\t30\tDemo phone fixture\tcast\t1\n';RemoteReplay.sent=True
                elif route=='/api/account':body='ok\tsigned_out\t\t\t\n'
                elif route=='/api/sleep':body='ok\t0\t0\t0\t0\n'
                elif route=='/api/nest/info':body='ok\t0\t\n'
                elif route=='/api/video/video000001/next':body='none\n'
                else:
                    unexpected.append(route);self.send_error(503);return
                raw=body.encode();self.send_response(200);self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
        remote_server=http.server.ThreadingHTTPServer(('127.0.0.1',0),RemoteReplay)
        remote_thread=threading.Thread(target=remote_server.serve_forever);remote_thread.start()
        env['XT_SERVER']=f'127.0.0.1:{remote_server.server_port}'
    try:
        with (assets/'app.log').open('w') as log:subprocess.run([str(work/'tube360-replay')],env=env,stdout=log,stderr=log,timeout=70,check=True)
    finally:
        if remote_server:remote_server.shutdown();remote_server.server_close();remote_thread.join()
    assert not unexpected,unexpected
    log=(assets/'app.log').read_text();assert 'captura:' in log and (phone_control or 'desligado: só console' in log),log[-4000:]
    cmd('magick',assets/'screen.ppm',assets/'screen.png')
    results.append({'name':name,'image':str(assets/'screen.png')});(work/'results.json').write_text(json.dumps(results,indent=2))
    print('PASS',name,flush=True);return assets,log

for lang in ('pt','en','es','ar'):
    run('later-'+lang,lang=lang,XT_NAV='local_later')
    run('resume-'+lang,lang=lang,XT_PLAY='video000001')
    run('settings-'+lang,lang=lang,XT_SCREEN='settings',keys='D'*16,seconds=5.5)
run('later-43-large',height=480,large=True,XT_NAV='local_later')
run('resume-ar-43-large',lang='ar',height=480,large=True,XT_PLAY='video000001')
run('clear-ar-43-large',lang='ar',height=480,large=True,XT_SCREEN='settings',keys='D'*16+'A',seconds=6)
run('library-guest',XT_NAV='library')
r=run('library-account',signed=True,XT_NAV='library',seconds=5)
if r:assert 'conectada=1' in r[1] and json.loads((r[0]/'local-library.json').read_text())==seed()
r=run('page-back',XT_NAV='library',keys='AB')
if r:assert 'captura navegação: library' in r[1] and 'pilha=1' in r[1]
run('corrupt',saved='corrupt',XT_NAV='local_later',seconds=2)
run('menu-guest',XT_NAV='local_later',keys='S')
run('menu-account',signed=True,XT_NAV='local_later',keys='S',seconds=4)
r=run('remove-local-account',signed=True,XT_NAV='local_later',keys='SDDDA',seconds=4)
if r:
    assert len(json.loads((r[0]/'local-library.json').read_text())['later'])==1
    assert '/browse/edit_playlist' not in (r[0]/'account-audit.log').read_text()
r=run('add-local',saved=False,XT_KEYS_MS='500',keys='SDA',seconds=4)
if r:assert len(json.loads((r[0]/'local-library.json').read_text())['later'])==1
r=run('storage-error',blocked=True,XT_NAV='local_later',keys='SDA')
if r:assert 'ok=0' in r[1] and len(json.loads((r[0]/'local-library.json').read_text())['later'])==2
r=run('remove-local',XT_NAV='local_later',keys='SDA')
if r:assert len(json.loads((r[0]/'local-library.json').read_text())['later'])==1
r=run('resume-play',XT_PLAY='video000001',keys='A',seconds=5)
if r:
    assert re.search(r'pedido video000001 inicio=30\.000',r[1]) and 'primeiro quadro:' in r[1],r[1][-4000:]
    pos=json.loads((r[0]/'local-library.json').read_text())['history'][0]['position'];assert 30<pos<40,pos
r=run('restart-play',XT_PLAY='video000001',keys='DA',seconds=5)
if r:
    assert 'inicio=0.000' in r[1] and 'primeiro quadro:' in r[1];assert not json.loads((r[0]/'local-library.json').read_text())['history']
r=run('phone-resume',phone_control=True,seconds=5)
if r:
    assert re.search(r'pedido video000001 inicio=30\.000.*remoto=1',r[1]) and 'primeiro quadro:' in r[1]
    assert json.loads((r[0]/'local-library.json').read_text())['history'][0]['position']>30
r=run('natural-end',XT_PLAY='video000001',keys='A]]',seconds=7)
if r:assert not json.loads((r[0]/'local-library.json').read_text())['history']
r=run('checkpoint',XT_PLAY='video000001',keys='A',seconds=34)
if r:
    assert r[1].count('local: save revision=')==1,r[1][-3000:]
    assert json.loads((r[0]/'local-library.json').read_text())['history'][0]['position']>60
r=run('resume-back',XT_PLAY='video000001',keys='B')
if r:assert 'tela 0, vídeo ' in r[1] and 'player: pedido' not in r[1]
for name,keys in [('clear-cancel','D'*16+'AB'),('clear-confirm','D'*16+'ADA'),('clear-list','D'*17+'ADA')]:
    r=run(name,XT_SCREEN='settings',keys=keys,seconds=6.5)
    if r:
        data=json.loads((r[0]/'local-library.json').read_text())
        assert len(data['history'])==(1 if name!='clear-confirm' else 0)
        assert len(data['later'])==(0 if name=='clear-list' else 2)
# Actual recorded tabs; two ups enter the channel tab row from the card grid.
for tab,keys in [('videos',''),('shorts','URD'),('live','URRD'),('playlists','URRRD')]:
    r=run('channel-'+tab,XT_NAV='channel',XT_NAV_ID='UC_x5XG1OV2P6uZZ5FSM9Ttw',keys=keys,seconds=5)
    if r:
        assert 'navegação channel:' in r[1] and re.search(r'miniaturas: [1-9]\d* bytes GPU',r[1]),r[1][-2500:]
# Change only the matching video tab replay for the explicit WebP simulation.
manifest_path=replay/'responses.json';original=manifest_path.read_text();manifest=json.loads(original)
for rule in manifest['requests']:
    if rule['file']=='channel-videos.json':rule['file']='channel-webp.json'
manifest_path.write_text(json.dumps(manifest))
try:
    r=run('channel-webp',XT_NAV='channel',XT_NAV_ID='UC_x5XG1OV2P6uZZ5FSM9Ttw',seconds=5)
    if r:assert 'JPEG fallback ok=1' in r[1],r[1][-2500:]
finally:manifest_path.write_text(original)
print('UI checks:',len(results))
