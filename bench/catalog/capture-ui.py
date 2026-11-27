import pathlib,subprocess,os
import sys
root=pathlib.Path(__file__).resolve().parents[2]; proof=pathlib.Path(sys.argv[1]).resolve(); replay=proof/'replay'
base={**os.environ,'XT_CAST_DISABLE':'1','SDL_VIDEO_DRIVER':'offscreen','SDL_AUDIO_DRIVER':'dummy','LP_NUM_THREADS':'2','XT_ASSETS':str(replay/'assets'),'XT_SERVER':'127.0.0.1:8132','XT_CATALOG_REPLAY':str(replay),'XT_SHOT_FRAME':'2147483647'}
cases=[
 ('music-home',{'XT_NAV':'music','XT_MUSIC':'1','XT_SHOT_MS':'1000'}),
 ('charts',{'XT_NAV':'music','XT_NAV_QUERY':'charts','XT_MUSIC':'1','XT_SHOT_MS':'1000'}),
 ('mixes',{'XT_NAV':'music','XT_NAV_QUERY':'mixes','XT_MUSIC':'1','XT_SHOT_MS':'1000'}),
 ('music-search',{'XT_NAV':'music_search','XT_NAV_QUERY':'jazz instrumental','XT_MUSIC':'1','XT_SHOT_MS':'1000'}),
 ('album',{'XT_NAV':'album','XT_NAV_ID':'MPREb_GuNCgmKDLZT','XT_MUSIC':'1','XT_SHOT_MS':'1000'}),
 ('artist',{'XT_NAV':'artist','XT_NAV_ID':'UCMJqedJ0OZgh9mjdRf26Xzg','XT_MUSIC':'1','XT_SHOT_MS':'1000'}),
 ('channel',{'XT_NAV':'channel','XT_NAV_ID':'UC_x5XG1OV2P6uZZ5FSM9Ttw','XT_SHOT_MS':'1200'}),
 ('channel-live',{'XT_NAV':'channel','XT_NAV_ID':'UC_x5XG1OV2P6uZZ5FSM9Ttw','XT_KEYS':'URRD','XT_KEYS_MS':'300','XT_SHOT_MS':'1500'}),
 ('channel-playlists',{'XT_NAV':'channel','XT_NAV_ID':'UC_x5XG1OV2P6uZZ5FSM9Ttw','XT_KEYS':'URRRD','XT_KEYS_MS':'300','XT_SHOT_MS':'1800'}),
 ('audio',{'XT_NAV':'radio','XT_NAV_ID':'F12audio001','XT_MUSIC':'1','XT_KEYS':'UA','XT_KEYS_MS':'400','XT_SHOT_MS':'2000'}),
 ('lyrics',{'XT_NAV':'radio','XT_NAV_ID':'F12audio001','XT_MUSIC':'1','XT_KEYS':'UARRA','XT_KEYS_MS':'350','XT_SHOT_MS':'2400'}),
 ('sequence',{'XT_NAV':'radio','XT_NAV_ID':'F12audio001','XT_MUSIC':'1','XT_KEYS':'UA','XT_KEYS_MS':'400','XT_SHOT_MS':'10000'}),
 ('playlist-sequence',{'XT_NAV':'playlist','XT_NAV_ID':'PLF12','XT_KEYS':'UA','XT_KEYS_MS':'400','XT_SHOT_MS':'10000'}),
 ('show-video',{'XT_NAV':'radio','XT_NAV_ID':'F12audio001','XT_MUSIC':'1','XT_KEYS':'UARRRA','XT_KEYS_MS':'300','XT_SHOT_MS':'2700'}),
 ('return-audio',{'XT_NAV':'radio','XT_NAV_ID':'F12audio001','XT_MUSIC':'1','XT_KEYS':'UARRRA..BB','XT_KEYS_MS':'300','XT_SHOT_MS':'3400'}),
 ('video-roundtrip',{'XT_NAV':'radio','XT_NAV_ID':'F12audio001','XT_MUSIC':'1','XT_KEYS':'UARRRABBA','XT_KEYS_MS':'300','XT_SHOT_MS':'4300'}),
 ('paused-video',{'XT_NAV':'radio','XT_NAV_ID':'F12audio001','XT_MUSIC':'1','XT_KEYS':'UAARRRAA','XT_KEYS_MS':'300','XT_SHOT_MS':'3300'}),
 ('paused-return',{'XT_NAV':'radio','XT_NAV_ID':'F12audio001','XT_MUSIC':'1','XT_KEYS':'UARRRAABBLLLA','XT_KEYS_MS':'300','XT_SHOT_MS':'4200'}),
 ('skip',{'XT_NAV':'radio','XT_NAV_ID':'F12audio001','XT_MUSIC':'1','XT_KEYS':'UARA','XT_KEYS_MS':'300','XT_SHOT_MS':'2000'}),
 ('channel-more',{'XT_NAV':'channel','XT_NAV_ID':'UC_x5XG1OV2P6uZZ5FSM9Ttw','XT_KEYS':'DD','XT_KEYS_MS':'400','XT_SHOT_MS':'1800'}),
 ('back',{'XT_NAV':'radio','XT_NAV_ID':'F12audio001','XT_MUSIC':'1','XT_KEYS':'UAB','XT_KEYS_MS':'400','XT_SHOT_MS':'1800'}),
]
wanted=set(sys.argv[2:])
for name,config in cases:
 if wanted and name not in wanted:continue
 env={**base,**config,'XT_SHOT':str(proof/(name+'.ppm'))}
 with (proof/(name+'.log')).open('w') as log:
  r=subprocess.run([str(proof/'tube360-replay')],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=45)
 if r.returncode:raise RuntimeError(name+' exit '+str(r.returncode))
 log=(proof/(name+'.log')).read_text()
 if name=='video-roundtrip':
  assert log.count('itags 22/0')==2,'video reused the audio-only format selection'
  assert log.count('primeiro quadro:')==2,'return to video did not present its picture'
 if name=='paused-video':assert 'primeiro quadro:' in log,'paused video did not resume'
 if name=='paused-return':assert log.count('musica: primeiro audio')==2,'paused audio did not resume'
 subprocess.run(['magick',str(proof/(name+'.ppm')),str(proof/(name+'.png'))],check=True)
 print(name,flush=True)
