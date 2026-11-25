"""Offline UI replay: recorded public titles, synthetic art/media, no account or network."""
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
work = Path(sys.argv[1]).resolve()
replay = work / 'replay'
accounts = work / 'accounts'
for path in (replay, accounts, work/'home', work/'cache', work/'config'):
    path.mkdir(parents=True, exist_ok=True)
titles = json.loads((ROOT/'bench/arabic/fixtures/titles.json').read_text())
cards = [{'videoRenderer': {'videoId': t['id'], 'title': {'simpleText': t['title']},
           'longBylineText': {'runs': [{'text': t['channel']}]}, 'lengthText': {'simpleText': '12:34'}}} for t in titles]
home = {'contents': cards * 3}
(replay/'browse.json').write_text(json.dumps(home))
(replay/'home.html').write_text('<script>ytcfg.set({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"fixture"}}})</script>')
(replay/'watch.html').write_text('<script>ytcfg.set({"VISITOR_DATA":"fixture","jsUrl":"/s/player/fixture/base.js"})</script>')
(replay/'base.js').write_text('var a={signatureTimestamp:20775};')
(replay/'empty.json').write_text('{"contents":[]}')
(replay/'error.json').write_text('{"playabilityStatus":{"status":"UNPLAYABLE","reason":"Video unavailable"}}')
# Synthetic 8 s clip is clearly identified in the proof; it is not the public video's footage.
for name, args in [
    ('video.mp4', ['-f','lavfi','-i','testsrc2=size=1280x720:rate=30','-t','8','-an','-c:v','libx264','-threads','2','-preset','ultrafast','-g','30','-movflags','+faststart']),
    ('audio.m4a', ['-f','lavfi','-i','sine=frequency=440:sample_rate=44100','-t','8','-c:a','aac','-movflags','+faststart'])]:
    if not (replay/name).exists():
        subprocess.run(['ffmpeg','-y','-v','error',*args,str(replay/name)],check=True)
player = {'playabilityStatus': {'status': 'OK'}, 'videoDetails': {'videoId': titles[0]['id'],
    'title': titles[0]['title'], 'author': titles[0]['channel'], 'lengthSeconds': '8'},
    'streamingData': {'expiresInSeconds': '3600', 'adaptiveFormats': [
        {'itag':136,'height':720,'width':1280,'fps':30,'mimeType':'video/mp4; codecs="avc1.64001f"','url':'https://fixture.googlevideo.com/video'},
        {'itag':140,'bitrate':128000,'mimeType':'audio/mp4; codecs="mp4a.40.2"','url':'https://fixture.googlevideo.com/audio'}]}}
(replay/'player.json').write_text(json.dumps(player))
if not (replay/'art.png').exists():
    subprocess.run(['magick','-size','320x180','gradient:#242d54-#b34663',str(replay/'art.png')],check=True)
requests=[]
def route(url, filename): requests.append({'url':url,'file':filename})
base='https://www.youtube.com/'
route(base,'home.html')
for endpoint in ('browse','search'): route(base+'youtubei/v1/'+endpoint+'?prettyPrint=false','browse.json')
route(base+'youtubei/v1/next?prettyPrint=false','empty.json')
route(base+'s/player/fixture/base.js','base.js')
for t in titles:
    route(base+'watch?v='+t['id']+'&bpctr=9999999999&has_verified=1','watch.html')
    route('https://i.ytimg.com/vi/'+t['id']+'/mqdefault.jpg','art.png')
requests.append({'url':base+'youtubei/v1/player?prettyPrint=false','fields':{'videoId':'error000001'},'file':'error.json'})
route(base+'watch?v=error000001&bpctr=9999999999&has_verified=1','watch.html')
route(base+'youtubei/v1/player?prettyPrint=false','player.json')
requests += [{'url':'https://fixture.googlevideo.com/'+name,'file':filename,'range':True} for name,filename in [('video','video.mp4'),('audio','audio.m4a')]]
(replay/'responses.json').write_text(json.dumps({'requests':requests}))
# Separate native-account replay has its own manifest schema, no real login.
account_rules=json.loads((ROOT/'bench/account/fixtures/responses.json').read_text())
for rule in account_rules:
    if '/browse?' in rule.get('url','') or '/search?' in rule.get('url',''):
        rule.pop('authorization',None); rule['json']=home
(accounts/'responses.json').write_text(json.dumps(account_rules))
print('Prepared offline Arabic fixtures:',work)

# Latin-only control proves that neither metadata nor the language selector eagerly loads Noto.
for name,source in [('plain-replay',replay),('plain-accounts',accounts)]:
    dest=work/name; shutil.copytree(source,dest,dirs_exist_ok=True)
    for p in dest.glob('*.json'):
        value=json.loads(p.read_text())
        def latin(obj):
            if isinstance(obj,dict): return {k:latin(v) for k,v in obj.items()}
            if isinstance(obj,list): return [latin(v) for v in obj]
            if isinstance(obj,str) and any('\u0600'<=c<='\u06ff' for c in obj): return 'Tube360 Demo 2026'
            return obj
        p.write_text(json.dumps(latin(value)))
