#!/usr/bin/env python3
"""SponsorBlock num vídeo enviado pelo lounge nativo; telefone/YouTube em loopback."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
from urllib.parse import parse_qs, urlsplit
from http.server import ThreadingHTTPServer
from e2e import ROOT, WORK, MEDIA, VIDEO, prepare

spec = importlib.util.spec_from_file_location('cast_e2e', ROOT/'tests/cast/e2e.py')
cast = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cast)


class Handler(cast.Handler):
    def get(self):
        if urlsplit(self.path).path == '/api/skipSegments/5f6b':
            query = parse_qs(urlsplit(self.path).query)
            assert json.loads(query['categories'][0]) == ['sponsor', 'selfpromo', 'interaction'], query
            assert json.loads(query['actionTypes'][0]) == ['skip'] and 'videoID' not in query, query
            self.lounge.sponsor_calls += 1
            self.response((MEDIA/'public-prefix.json').read_bytes())
        else:
            super().get()

    def post(self):
        if urlsplit(self.path).path == '/youtubei/v1/player':
            request = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            assert request['videoId'] == VIDEO
            self.response(dict(playabilityStatus=dict(status='OK'),
                videoDetails=dict(videoId=VIDEO, title='SponsorBlock / phone Demo', lengthSeconds='667'),
                streamingData=dict(expiresInSeconds='3600', formats=[dict(itag=22, height=720, width=1280,
                    fps=30, mimeType='video/mp4; codecs="avc1.64001f, mp4a.40.2"',
                    url='https://fixture.googlevideo.com/sample.mp4')])) )
        else:
            super().post()


def run(undo):
    name = 'phone-undo' if undo else 'phone-auto'
    assets = WORK/name
    assets.mkdir(exist_ok=True)
    for font in (ROOT/'client/assets').glob('inter-*.ttf'):
        shutil.copyfile(font, assets/font.name)
    (assets/'settings.txt').write_text('lang=pt\nsound=tv\nsponsorblock=auto\n')
    lounge = cast.Lounge(MEDIA)
    lounge.sponsor_calls = 0
    server = ThreadingHTTPServer(('127.0.0.1', 8234), Handler)
    server.daemon_threads = True
    server.lounge = lounge
    thread = threading.Thread(target=server.serve_forever)
    thread.start()
    env = {k:v for k,v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(assets), XDG_CACHE_HOME=str(assets/'.cache'), SDL_VIDEO_DRIVER='offscreen',
        SDL_AUDIO_DRIVER='dummy', LP_NUM_THREADS='2', XT_ASSETS=str(assets), XT_SHOT=str(WORK/(name+'.ppm')), XT_SHOT_MS='6500', XT_KEYS_MS='100',
        XT_KEYS='.'*34+'Y' if undo else '')
    process = None
    try:
        with (WORK/(name+'.log')).open('w') as log:
            process = subprocess.Popen([str(ROOT/'client/build/cast-test-app')],cwd=ROOT,env=env,stdout=log,stderr=log)
            cast.wait_for(lambda:lounge.code_count, 'phone code')
            lounge.message('loungeStatus',dict(devices=json.dumps([dict(id='F34-PHONE',type='REMOTE_CONTROL',name='Telefone Demo')])) )
            lounge.message('setPlaylist',dict(videoIds=VIDEO,videoId=VIDEO,currentIndex='0',currentTime='8'))
            cast.wait_for(lambda:lounge.seen('nowPlaying',lambda d:d.get('videoId')==VIDEO and d.get('state')=='1' and float(d.get('currentTime',0))>=20.8), 'phone learns automatic seek')
            if undo:
                mark=lounge.mark()
                cast.wait_for(lambda:lounge.seen('nowPlaying',lambda d:d.get('videoId')==VIDEO and d.get('state')=='1' and 7.7<=float(d.get('currentTime',0))<15,mark), 'phone learns Y return')
            assert process.wait(timeout=25)==0
        text=(WORK/(name+'.log')).read_text()
        assert not lounge.errors, lounge.errors
        assert lounge.sponsor_calls==1 and text.count('SponsorBlock: skip ')==1, text
        assert ('SponsorBlock: undo ' in text)==undo, text
        (WORK/(name+'-events.json')).write_text(json.dumps(lounge.events,indent=2))
        subprocess.run(['magick',str(WORK/(name+'.ppm')),str(WORK/(name+'.png'))],check=True)
        print('PASS',name,'native lounge, seek and phone clock, single API request',flush=True)
    finally:
        (WORK/(name+'-events.json')).write_text(json.dumps(lounge.events,indent=2))
        if process and process.poll() is None:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        with lounge.cv:
            lounge.disconnect=True
            lounge.cv.notify_all()
        server.shutdown()
        server.server_close()
        thread.join(3)


if __name__=='__main__':
    prepare()
    if not (MEDIA/'sample.mp4').exists():
        subprocess.run(['ffmpeg','-v','error','-i',str(MEDIA/'video.mp4'),'-i',str(MEDIA/'audio.m4a'),
            '-c','copy','-movflags','+faststart',str(MEDIA/'sample.mp4')],check=True)
    if not (MEDIA/'thumbnail.jpg').exists():
        subprocess.run(['ffmpeg','-v','error','-i',str(MEDIA/'sample.mp4'),'-frames:v','1','-vf','scale=320:180',
            '-threads','1',str(MEDIA/'thumbnail.jpg')],check=True)
    run(False)
    run(True)
