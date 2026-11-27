#!/usr/bin/env python3
"""Run the app offline against preserved F33 media; synthetic AAC, never a console gate."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
cli=argparse.ArgumentParser()
cli.add_argument('source',type=Path,help='F33 quality/media containing 1080.mp4, 720.mp4, audio.m4a and player.json')
cli.add_argument('--work',type=Path,default=ROOT/'.work/f40-offline')
cli.add_argument('--case',choices=('primary','full','lower-source'),default='primary')
cli.add_argument('--results-dir',type=Path,default=ROOT/'bench/1080p-app/results')
args=cli.parse_args()
source=args.source.resolve();work=args.work.resolve()/args.case
if not work.is_relative_to(ROOT):cli.error('work directory must be inside this worktree')
media=work/'media';assets=work/'assets';media.mkdir(parents=True,exist_ok=True);assets.mkdir(exist_ok=True)
seconds,warmup=(120,10) if args.case=='primary' else (10,2)
length=seconds+warmup+20
for name in ('1080.mp4','720.mp4','audio.m4a'):
    if not (media/name).exists():
        subprocess.run(['ffmpeg','-v','error','-stream_loop','9','-i',str(source/name),'-t',str(length),'-c','copy','-movflags','+faststart',str(media/name)],check=True)
data=json.loads((source/'player.json').read_text())
data['videoDetails'].update(title='F40 offline — 137 preserved/looped, synthetic AAC',lengthSeconds=str(length))
formats=data['streamingData']['adaptiveFormats']
if any(not f['url'].startswith('https://fixture.googlevideo.com/') for f in formats):cli.error('only offline fixture URLs allowed')
if args.case=='lower-source':formats=[f for f in formats if f['itag']!=137]
for fmt in formats:fmt['bitrate']=int((media/fmt['url'].rsplit('/',1)[1]).stat().st_size*8/length)
data['streamingData']['adaptiveFormats']=formats
(media/'player.json').write_text(json.dumps(data))
for font in (ROOT/'client/assets').glob('inter-*.ttf'):shutil.copy2(font,assets)
(assets/'settings.txt').write_text('lang=pt\nsound=tv\nvideo_mode=1080p30\noutput_height=720\nh264_loop_filter='+('full' if args.case=='full' else 'auto')+'\n')
(assets/'autoplay.txt').write_text('4p_9OPKgcDE 0\n')
(assets/'f40-playback.txt').write_text(f'{seconds} {warmup} 137 0 {0 if args.case=="full" else 48} 4p_9OPKgcDE\n')
env={k:v for k,v in os.environ.items() if not k.startswith('XT_')}
env.update(HOME=str(work/'home'),XDG_CACHE_HOME=str(work/'cache'),SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',XT_CAST_DISABLE='1',XT_ASSETS=str(assets),XT_QUALITY_FIXTURES=str(media),XT_QUALITY_RENDER_MS='8')
args.results_dir.mkdir(parents=True,exist_ok=True)
result=args.results_dir/f'app-pc-{args.case}.txt'
with result.open('w') as output:
    subprocess.run([str(ROOT/'client/build/tube360-quality-fixture')],cwd=work,env=env,stdout=output,stderr=subprocess.STDOUT,timeout=seconds+warmup+95,check=True)
subprocess.run(['python3',str(ROOT/'bench/1080p-app/summarize.py'),str(result),str(result.with_suffix('.json'))],check=True)
summary=json.loads(result.with_suffix('.json').read_text())
expected='FAIL' if args.case=='lower-source' else 'PASS'
if summary['result']!=expected or summary['console_gate']!='pending_console':raise SystemExit('unexpected offline result')
print(args.case,expected,'console_gate=pending_console')
