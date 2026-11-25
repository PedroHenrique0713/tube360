"""Prepare closed offline app fixtures; preserve public fragments and label synthetic replies."""
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
work=Path(sys.argv[1]).resolve()
fixtures=ROOT/'bench/search/fixtures'
replay=work/'replay'
replay.mkdir(parents=True,exist_ok=True)
for name in ('search-public.json','home.html','suggestions-pt.jsonp'):
    shutil.copyfile(fixtures/name,replay/name)
requests=[]
def route(url,file,**extra): requests.append(dict(url=url,file=file,**extra))
base='https://www.youtube.com/'
route(base,'home.html')
route(base+'youtubei/v1/search?prettyPrint=false','search-public.json')
route(base+'youtubei/v1/browse?prettyPrint=false','search-public.json')
# Channel/playlist destinations reuse earlier declared public catalogue recordings.
for name in ('channel.json','playlist.json'):
    shutil.copyfile(ROOT/'bench/catalog/fixtures'/name,replay/name)
route(base+'youtubei/v1/browse?prettyPrint=false','channel.json',fields={'browseId':'UCfOj-wa2wA7aI0UjZYeBgHg'})
public=json.loads((fixtures/'search-public.json').read_text())
playlists=[v['lockupViewModel']['contentId'] for v in public['contents'] if 'lockupViewModel' in v and v['lockupViewModel'].get('contentType')=='LOCKUP_CONTENT_TYPE_PLAYLIST']
for pid in playlists:
    route(base+'youtubei/v1/browse?prettyPrint=false','playlist.json',fields={'browseId':'VL'+pid})
# Specific browse rules must precede the home fallback.
requests.sort(key=lambda r:0 if 'fields' in r else 1)
for lang,region in [('pt','BR'),('en','US'),('es','MX'),('ar','SA')]:
    route('https://suggestqueries-clients6.youtube.com/complete/search?client=youtube&ds=yt&hl='+lang+'&gl='+region+'&q=astronomia','suggestions-pt.jsonp')
# Synthetic RTL rows exercise the recorded protocol without another live query.
from urllib.parse import quote
arabic=['علوم الفضاء','علوم للأطفال','علوم 2026','علوم بالعربية','علوم الأرض','علوم الحياة','علوم القرآن','علوم وتجارب']
(replay/'suggestions-ar-synthetic.json').write_text(json.dumps(['علوم',[[text,0] for text in arabic]],ensure_ascii=False))
route('https://suggestqueries-clients6.youtube.com/complete/search?client=youtube&ds=yt&hl=ar&gl=SA&q='+quote('علوم'),'suggestions-ar-synthetic.json')
# Art is synthetic, with the actual public URLs used only as routing keys. No CDN traffic.
subprocess.run(['magick','-size','320x180','gradient:#202947-#a6405a',str(replay/'art.png')],check=True)
urls=set()
def walk(x):
    if isinstance(x,dict):
        for key,value in x.items():
            if key=='url' and isinstance(value,str) and value.startswith('https://') and any(d in value for d in ('ytimg.com','ggpht.com','googleusercontent.com')): urls.add(value)
            walk(value)
    elif isinstance(x,list):
        for value in x: walk(value)
for file in ('search-public.json','channel.json','playlist.json'):
    walk(json.loads((replay/file).read_text()))
for value in public['contents']:
    if 'videoRenderer' in value: urls.add('https://i.ytimg.com/vi/'+value['videoRenderer']['videoId']+'/mqdefault.jpg')
for url in urls: route(url,'art.png')
(replay/'responses.json').write_text(json.dumps({'requests':requests}))
(work/'accounts').mkdir(exist_ok=True)
(work/'accounts/responses.json').write_text('[]')
# Relink the actual production main object; wrap every HTTPS constructor, including the player.
obj=work/'transport.o'
subprocess.run(['c++','-std=c++20','-O1','-I'+str(ROOT/'client/src'),'-I'+str(ROOT/'client/third_party'),
                '-c',str(ROOT/'bench/search/fixture_transport.cpp'),'-o',str(obj)],check=True)
build=ROOT/'client/build'
args=shlex.split((build/'CMakeFiles/tube360.dir/link.txt').read_text())
args[args.index('-o')+1]=str(work/'tube360-search-replay')
args.extend([str(obj),'-Wl,--wrap=_ZN2yt5httpsEPKSt6atomicIbE'])
subprocess.run(args,cwd=build,check=True)
print(work/'tube360-search-replay')
