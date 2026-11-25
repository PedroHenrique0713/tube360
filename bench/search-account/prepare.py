"""Closed replay; public renderer recordings and explicitly fictional account/media."""
import json
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
work=Path(sys.argv[1]).resolve();work.mkdir(parents=True,exist_ok=True)
replay=work/'replay'
subprocess.run([sys.executable,str(ROOT/'bench/catalog/prepare-replay.py'),str(replay)],check=True)
manifest=json.loads((replay/'responses.json').read_text())
def route(path,file,fields=None,**extra):
    manifest['requests'].insert(0,dict(url=path,file=file,fields=fields or {},**extra))
base='https://www.youtube.com/'
shutil.copy(ROOT/'bench/search/fixtures/search-public.json',replay/'search.json')
route(base+'youtubei/v1/search?prettyPrint=false','search.json',authorization='')
def artwork(value):
    if isinstance(value,dict):
        for key,child in value.items():
            if key=='url' and isinstance(child,str) and child.startswith('https://') and any(host in child for host in ('ytimg.com/','ggpht.com/','googleusercontent.com/')):route(child,'art.png')
            if key=='videoId' and isinstance(child,str) and len(child)==11:route('https://i.ytimg.com/vi/'+child+'/mqdefault.jpg','art.png')
            artwork(child)
    elif isinstance(value,list):
        for child in value:artwork(child)
artwork(json.loads((replay/'search.json').read_text()))
route(base+'youtubei/v1/browse?prettyPrint=false','search.json',{'browseId':'FEwhat_to_watch'})
route(base+'tv','tv-context.html')
route('https://music.youtube.com/youtubei/v1/browse?prettyPrint=false&alt=json','music-invalid-bearer.json',status=401,authorization='console-fixture-token')
# TV DOM from the sanitized recording; every title below is fictional.
home=json.loads((replay/'tv-music-public.json').read_text())
def titles(value):
    if isinstance(value,dict):
        for key,child in value.items():
            if key=='simpleText' and isinstance(child,str):value[key]='Demo · fixture'
            else:titles(child)
    elif isinstance(value,list):
        for child in value:titles(child)
titles(home)
(replay/'account-home.json').write_text(json.dumps(home))
for bid in ('FEtopics_music','FEmusic_last_played','FEplaylist_aggregation','VLLM'):
    route(base+'youtubei/v1/browse?prettyPrint=false&alt=json','account-home.json',{'browseId':bid},authorization='console-fixture-token')
# Real 1920x800/24 AVC, synthesized locally. No request to the named public video.
subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=1920x800:rate=24:duration=8','-an','-c:v','libx264','-threads','2','-preset','ultrafast','-g','24','-pix_fmt','yuv420p','-movflags','+faststart',str(replay/'wide.mp4')],check=True)
player={'playabilityStatus':{'status':'OK'},'videoDetails':{'videoId':'R6MlUcmOul8','title':'Demo · 1920 × 800 · 24 fps','lengthSeconds':'8'},'streamingData':{'adaptiveFormats':[
    {'itag':137,'width':1920,'height':800,'fps':24,'mimeType':'video/mp4; codecs="avc1.640028"','url':'https://fixture.googlevideo.com/wide.mp4'},
    {'itag':140,'mimeType':'audio/mp4; codecs="mp4a.40.2"','url':'https://fixture.googlevideo.com/audio.m4a','bitrate':128000}]}}
(replay/'wide-player.json').write_text(json.dumps(player))
route(base+'youtubei/v1/player?prettyPrint=false','wide-player.json',{'videoId':'R6MlUcmOul8'})
route('https://fixture.googlevideo.com/wide.mp4','wide.mp4',range=True)
route('https://fixture.googlevideo.com/audio.m4a','audio.m4a',range=True)
route(base+'watch?v=R6MlUcmOul8&bpctr=9999999999&has_verified=1','watch.html')
(replay/'responses.json').write_text(json.dumps(manifest))
accounts=work/'accounts';shutil.copytree(ROOT/'bench/account/fixtures',accounts,dirs_exist_ok=True)
rules=json.loads((accounts/'responses.json').read_text())
rules.insert(0,{'url':'/account/accounts_list?','contains':'','json':{'contents':[{'accountItemRenderer':{'accountName':{'simpleText':'Demo · fictional account'},'isSelected':True}}]}})
(accounts/'responses.json').write_text(json.dumps(rules))
# Lower real source and long audio for a genuine manual-pressure warning.
subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=854x480:rate=30:duration=24','-an','-c:v','libx264','-threads','2','-preset','ultrafast','-g','30','-movflags','+faststart',str(replay/'480.mp4')],check=True)
subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','sine=frequency=440:sample_rate=44100:duration=24','-ac','2','-c:a','aac','-movflags','+faststart',str(replay/'long-audio.m4a')],check=True)
pressure=json.loads(json.dumps(player));pressure['videoDetails']['lengthSeconds']='24'
pressure['streamingData']['adaptiveFormats'][0].update(itag=135,width=854,height=480,fps=30,url='https://fixture.googlevideo.com/480.mp4')
pressure['streamingData']['adaptiveFormats'][1]['url']='https://fixture.googlevideo.com/long-audio.m4a'
(replay/'pressure-player.json').write_text(json.dumps(pressure))
route('https://fixture.googlevideo.com/pressure-player','pressure-player.json')
route('https://fixture.googlevideo.com/480.mp4','480.mp4',range=True)
route('https://fixture.googlevideo.com/long-audio.m4a','long-audio.m4a',range=True)
(replay/'responses.json').write_text(json.dumps(manifest))
# Production executable, replacing every HTTPS constructor and optionally slowing decode in fixtures.
build=ROOT/'client/build';obj=work/'transport.o'
subprocess.run(['c++','-std=c++20','-O1','-I'+str(ROOT/'client/src'),'-I'+str(ROOT/'client/third_party'),'-I'+str(ROOT/'.work/ffmpeg-pc/include'),'-c',str(ROOT/'bench/search-account/fixture_transport.cpp'),'-o',str(obj)],check=True)
args=shlex.split((build/'CMakeFiles/tube360.dir/link.txt').read_text())
args[args.index('-o')+1]=str(work/'tube360-replay')
args.extend([str(obj),'-Wl,--wrap=_ZN2yt5httpsEPKSt6atomicIbE','-Wl,--wrap=avcodec_receive_frame'])
subprocess.run(args,cwd=build,check=True)
print(work/'tube360-replay')
