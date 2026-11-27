"""Capture the rc4 production UI with isolated account/network fixtures, never a personal store."""
from pathlib import Path
import copy, hashlib, json, os, re, shutil, subprocess, sys
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/'site/.work/rc4-review'
LANG=os.environ.get('NATIVE_LANG','pt-BR')
if LANG not in ('pt-BR','en'):raise ValueError('NATIVE_LANG must be pt-BR or en')
REPLAY=WORK/'replay';ACCOUNTS=WORK/'accounts';NATIVE=WORK/('native' if LANG=='pt-BR' else 'native-en')
for p in (REPLAY,ACCOUNTS,NATIVE):p.mkdir(parents=True,exist_ok=True)
source=Path(os.environ.get('NATIVE_REPLAY_DIR',ROOT/'site/.work/app/search-account'))
if not (WORK/'tube360-replay').exists():shutil.copy2(source/'tube360-replay',WORK/'tube360-replay')
shutil.copytree(source/'replay',REPLAY,dirs_exist_ok=True)
shutil.copytree(source/'accounts',ACCOUNTS,dirs_exist_ok=True)
if (ROOT/'site/src/assets/demo/catalog-art.png').exists():shutil.copy2(ROOT/'site/src/assets/demo/catalog-art.png',REPLAY/'art.png')
manifest=json.loads((REPLAY/'responses.json').read_text());rules=manifest['requests']
def route(url,file,**extra):rules.insert(0,dict(url=url,file=file,**extra))
# Fictional Music catalogue, with the same art supplied to the production renderer and browser.
music_titles=['Noite de jazz','Piano ao entardecer','Batidas leves','Acústico em casa','Bossa na varanda','Clássicos ao piano','Jazz instrumental','Sessão acústica','Sons da noite','Voz e violão','Café e música','Piano para ler']
artists=['Trio Horizonte','Marina Alves','Estúdio Norte','Duo da Serra']
colors=['#233d49','#55422a','#354035','#46374a','#3b464b','#504038']
font=ImageFont.truetype(str(ROOT/'client/assets/inter-700.ttf'),22)
small=ImageFont.truetype(str(ROOT/'client/assets/inter-400.ttf'),12)
artdir=ROOT/'site/src/assets/demo';artdir.mkdir(exist_ok=True)
for i in range(6):
 im=Image.new('RGB',(320,180),colors[i]);d=ImageDraw.Draw(im)
 # Album sleeves are repo-native graphic artwork, with no third-party image or service.
 for radius in (68,48,25):d.ellipse((160-radius,90-radius,160+radius,90+radius),outline='#d0cbb6',width=2)
 d.rectangle((24+i*5,34,29+i*5,126),fill='#f5f5f7');d.ellipse((145,75,175,105),fill='#ff5a52')
 im.save(artdir/f'music-{i}.png',optimize=True);shutil.copy2(artdir/f'music-{i}.png',REPLAY/f'music-{i}.png')
base=json.loads((REPLAY/'account-home.json').read_text())
def tiles(x):
 if isinstance(x,dict):
  if 'tileRenderer' in x:yield x['tileRenderer']
  else:
   for v in x.values():yield from tiles(v)
 elif isinstance(x,list):
  for v in x:yield from tiles(v)
template=copy.deepcopy(next(tiles(base)));shelves=[];music=[]
for row,heading in enumerate(['Para ouvir agora','Jazz e instrumentais','Mais música para você']):
 items=[]
 for col in range(4):
  i=row*4+col;tile=copy.deepcopy(template);vid=f'F49audio{i:03}';url=f'https://i.ytimg.com/vi/{vid}/mqdefault.jpg'
  tile['contentId']=vid;tile['onSelectCommand']={'watchEndpoint':{'videoId':vid,'playlistId':'RD'+vid}}
  tile['header']['tileHeaderRenderer']['thumbnail']={'thumbnails':[{'url':url,'width':320,'height':180}]}
  tile['metadata']['tileMetadataRenderer']={'title':{'simpleText':music_titles[i]},'lines':[{'lineRenderer':{'items':[{'lineItemRenderer':{'text':{'simpleText':artists[i%4]}}}]}}]}
  items.append({'tileRenderer':tile});route(url,f'music-{i%6}.png')
  music.append(dict(id=vid,title=music_titles[i],channel=artists[i%4],art=f'/assets/demo/music-{i%6}.png',section=heading))
 shelves.append({'shelfRenderer':{'title':{'runs':[{'text':heading}]},'content':{'horizontalListRenderer':{'items':items}}}})
(REPLAY/'account-home.json').write_text(json.dumps({'contents':{'sectionListRenderer':{'contents':shelves}}},ensure_ascii=False))
(WORK/'music.json').write_text(json.dumps(music,ensure_ascii=False,indent=2))
# Use the existing site sample videos for Home/account lists, with identical thumbnail pixels.
js=(ROOT/'site/src/main.js').read_text();videos=json.loads(js.split('  const videos = ',1)[1].split(';\n',1)[0])
account_rules=json.loads((ACCOUNTS/'responses.json').read_text())
with Image.open(ROOT/'site/src/assets/img/home.jpg') as atlas:
 for i,v in enumerate(videos):
  atlas.crop((v['x'],v['y'],v['x']+252,v['y']+142)).resize((320,180)).save(REPLAY/f'home-{i}.png')
  route('https://i.ytimg.com/vi/'+v['id']+'/mqdefault.jpg',f'home-{i}.png')
def tile_video(v):
 tile=copy.deepcopy(template)
 tile['contentId']=v['id']
 tile['onSelectCommand']={'watchEndpoint':{'videoId':v['id']}}
 tile['header']['tileHeaderRenderer']={
  'thumbnail':{'thumbnails':[{'url':'https://i.ytimg.com/vi/'+v['id']+'/mqdefault.jpg','width':320,'height':180}]},
  'thumbnailOverlays':[{'thumbnailOverlayTimeStatusRenderer':{'text':{'simpleText':'8:07'},'style':'DEFAULT'}}]
 }
 tile['metadata']['tileMetadataRenderer']={'title':{'simpleText':v['title']},'lines':[{'lineRenderer':{'items':[{'lineItemRenderer':{'text':{'simpleText':v['channel']}}}]}}]}
 return {'tileRenderer':tile}
home={'contents':[tile_video(v) for v in videos]}
public=[]
for v in videos:
 entry={'videoId':v['id'],'title':{'simpleText':v['title']},'longBylineText':{'runs':[{'text':v['channel']}]},'lengthText':{'simpleText':'8:07'}}
 entry['thumbnail']={'thumbnails':[{'url':'https://i.ytimg.com/vi/'+v['id']+'/mqdefault.jpg','width':320,'height':180}]}
 public.append({'videoRenderer':entry})
(REPLAY/'review-home.json').write_text(json.dumps({'contents':public}))
route('https://www.youtube.com/youtubei/v1/browse?prettyPrint=false','review-home.json',fields={'browseId':'FEwhat_to_watch'})
for r in account_rules:
 if 'FEwhat_to_watch' in r.get('contains','') or r.get('contains')=='"clientName":"WEB"':r['json']=home
 if 'FEsubscriptions' in r.get('contains',''):
  for tile in tiles(r.get('json',{})):
   vid=tile.get('contentId','');tile.setdefault('header',{}).setdefault('tileHeaderRenderer',{})['thumbnail']={'thumbnails':[{'url':'https://i.ytimg.com/vi/'+vid+'/mqdefault.jpg','width':320,'height':180}]}
   route('https://i.ytimg.com/vi/'+vid+'/mqdefault.jpg','art.png')
 if '/accounts_list' in r['url']:
  r['json']={'contents':[{'accountItemRenderer':{'accountName':{'simpleText':'Conta de demonstração'},'isSelected':True}}]}
account_rules.sort(key=lambda r: -len(r.get('contains','')))
(ACCOUNTS/'responses.json').write_text(json.dumps(account_rules,ensure_ascii=False))
# Add the real channel Shorts reply to the common replay.
shorts=json.loads((ROOT/'site/.work/app/local/replay/channel-shorts.json').read_text())
def image_urls(x):
 if isinstance(x,dict):
  for key,value in x.items():
   if key=='url' and isinstance(value,str) and 'ytimg.com/' in value:yield value
   else:yield from image_urls(value)
 elif isinstance(x,list):
  for value in x:yield from image_urls(value)
for url in image_urls(shorts):route(url,'art.png')
(REPLAY/'channel-shorts.json').write_text(json.dumps(shorts))
route('https://www.youtube.com/youtubei/v1/browse?prettyPrint=false','channel-shorts.json',fields={'browseId':'UC_x5XG1OV2P6uZZ5FSM9Ttw','params':'SHORTS_FIXTURE'})
for name in ('channel.json','channel-videos.json','channel-live.json','channel-playlists.json'):
 p=REPLAY/name
 if p.exists():
  data=json.loads(p.read_text());tabs=data.get('contents',{}).get('twoColumnBrowseResultsRenderer',{}).get('tabs',[])
  if tabs and not any('Shorts'==t.get('tabRenderer',{}).get('title') for t in tabs):tabs.insert(2,{'tabRenderer':{'title':'Shorts','endpoint':{'commandMetadata':{'webCommandMetadata':{'url':'/@GoogleDevelopers/shorts'}},'browseEndpoint':{'browseId':'UC_x5XG1OV2P6uZZ5FSM9Ttw','params':'SHORTS_FIXTURE'}}}})
  for tab in tabs:
   renderer=tab.get('tabRenderer',{})
   if renderer.get('title')=='Shorts':renderer['endpoint']={'commandMetadata':{'webCommandMetadata':{'url':'/@GoogleDevelopers/shorts'}},'browseEndpoint':{'browseId':'UC_x5XG1OV2P6uZZ5FSM9Ttw','params':'SHORTS_FIXTURE'}}
  p.write_text(json.dumps(data))
(REPLAY/'responses.json').write_text(json.dumps(manifest))
# Encoding uses only the existing still frame; no internet/video service is contacted.
wanted_cases=set(sys.argv[1:])
if not wanted_cases or wanted_cases.intersection({'player','quality','quality-bottom'}):
 for height,width in [(1080,1920),(480,854),(360,640),(240,426),(144,256)]:
  output=WORK/('review-video.mp4' if height==1080 else f'review-{height}.mp4')
  if not output.exists():subprocess.run(['ffmpeg','-v','error','-y','-loop','1','-framerate','1','-i',str(ROOT/'site/src/assets/img/player-clean.jpg'),'-vf',f'scale={width}:{height},fps=30','-t','487','-c:v','libx264','-threads','2','-preset','ultrafast','-tune','stillimage','-crf','35','-g','300','-pix_fmt','yuv420p','-movflags','+faststart',str(output)],check=True)
# Identical still-frame media and complete offered qualities for the player comparison.
if (WORK/'review-video.mp4').exists():
 audio=WORK/'review-audio.m4a'
 if not audio.exists():subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','sine=frequency=440:sample_rate=44100','-t','487','-ac','2','-c:a','aac','-b:a','64k',str(audio)],check=True)
 player=json.loads((REPLAY/'wide-player.json').read_text());player['videoDetails'].update(title=videos[0]['title'],author=videos[0]['channel'],lengthSeconds='487')
 formats=[]
 for itag,height,width in [(137,1080,1920),(135,480,854),(134,360,640),(133,240,426),(160,144,256)]:
  source=WORK/('review-video.mp4' if height==1080 else f'review-{height}.mp4')
  if not source.exists():continue
  file=f'review-{height}.mp4';shutil.copy2(source,REPLAY/file)
  url='https://fixture.googlevideo.com/'+file;route(url,file,range=True)
  formats.append(dict(itag=itag,width=width,height=height,fps=30,mimeType='video/mp4; codecs="avc1.640028"',url=url))
 shutil.copy2(audio,REPLAY/'review-audio.m4a');route('https://fixture.googlevideo.com/review-audio.m4a','review-audio.m4a',range=True)
 formats.append(dict(itag=140,mimeType='audio/mp4; codecs="mp4a.40.2"',url='https://fixture.googlevideo.com/review-audio.m4a',bitrate=64000))
 player['streamingData']['adaptiveFormats']=formats;(REPLAY/'wide-player.json').write_text(json.dumps(player))
 segments=[dict(category='sponsor',segment=[start,end],videoDuration=487) for start,end in [(5,24),(185,219),(394,414)]]
 (REPLAY/'sponsor-demo.json').write_text(json.dumps([dict(videoID='R6MlUcmOul8',segments=segments)]))
 (REPLAY/'responses.json').write_text(json.dumps(manifest))
# A single command can regenerate selected captures after fixture calibration.
cases=[('home',{},''),('search',{'XT_SCREEN':'search','XT_SEARCH_TEXT':'astronomia'},''),('filters',{'XT_SCREEN':'search','XT_SEARCH_TEXT':'astronomia'},'....['),('subscriptions',{'XT_NAV':'subscriptions'},''),('library',{},'....LDDDA'),('music',{'XT_NAV':'music','XT_MUSIC':'1'},''),('phone',{},'LDDDDDA'),('settings-top',{'XT_SCREEN':'settings'},''),('settings-middle',{'XT_SCREEN':'settings'},'D'*8),('settings-bottom',{'XT_SCREEN':'settings'},'D'*17),('player',{'XT_PLAY':'R6MlUcmOul8'},'.'*40+'A'),('quality',{'XT_PLAY':'R6MlUcmOul8'},'.'*40+'A.Y.Y'),('quality-bottom',{'XT_PLAY':'R6MlUcmOul8'},'.'*40+'A.Y.Y'+'D'*5),('channel',{'XT_NAV':'channel','XT_NAV_ID':'UC_x5XG1OV2P6uZZ5FSM9Ttw'},''),('shorts',{'XT_NAV':'channel','XT_NAV_ID':'UC_x5XG1OV2P6uZZ5FSM9Ttw'},'....URD')]
wanted=set(sys.argv[1:])
for name,extra,keys in cases:
 if wanted and name not in wanted:continue
 case=NATIVE/name;assets=case/'assets';assets.mkdir(parents=True,exist_ok=True)
 for p in (ROOT/'client/assets').glob('*.ttf'):shutil.copy2(p,assets/p.name)
 (assets/'settings.txt').write_text(f'lang={"pt" if LANG=="pt-BR" else "en"}\ncols=4\nsound=tv\nvideo_mode=auto\n')
 (assets/'yt-console-account.json').write_text(json.dumps({'access_token':'console-fixture-token','refresh_token':'fixture-refresh','expires_at':4070908800,'client':{'client_id':'fixture-client','client_secret':'fixture-secret'},'name':'Conta de demonstração'}))
 saved=lambda i:dict(id=videos[i]['id'],title=videos[i]['title'],channel=videos[i]['channel'],duration=487,position=84 if i==1 else 0,order=i+1)
 (assets/'local-library.json').write_text(json.dumps(dict(version=1,later=[saved(0)],history=[saved(1)])))
 env={k:v for k,v in os.environ.items() if not k.startswith(('XT_','F47_'))}
 env.update(HOME=str(case/'home'),XDG_CONFIG_HOME=str(case/'config'),XDG_CACHE_HOME=str(case/'cache'),LP_NUM_THREADS='2',SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',XT_CAST_DISABLE='1',XT_ASSETS=str(assets),F47_REPLAY=str(REPLAY),XT_ACCOUNT_FIXTURES=str(ACCOUNTS),XT_SHOT=str(case/'screen.ppm'),XT_SHOT_MS=str(max(2600,len(keys)*180+1000)),XT_SHOT_FRAME='2147483647',XT_KEYS=keys,XT_KEYS_MS='180',**extra)
 with (case/'app.log').open('w') as log:subprocess.run([str(WORK/'tube360-replay')],env=env,cwd=case,stdout=log,stderr=log,timeout=45,check=True)
 with Image.open(case/'screen.ppm') as im:im.save(case/'screen.png')
 assert 'captura:' in (case/'app.log').read_text(),name
 print(name,'captured',flush=True)
