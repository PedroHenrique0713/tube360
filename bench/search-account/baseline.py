"""Compile rc3 from git archive in the proof folder; never check out another branch."""
import json
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
work=Path(sys.argv[1]).resolve();base=work/'baseline';base.mkdir(parents=True,exist_ok=True)
src=base/'source/client/src';reuse='--reuse-objects' in sys.argv
common=['c++','-std=c++20','-O1','-DPICOJSON_USE_LOCALE=0','-I'+str(src),'-I'+str(src/'yt'),'-I'+str(src.parent/'third_party')]
if not reuse:
    archive=base/'rc3.tar'
    subprocess.run(['git','archive','--output='+str(archive),'af792a2','client/src','client/third_party','bench/search/frame_probe.hpp'],cwd=ROOT,check=True)
    (base/'source').mkdir(exist_ok=True)
    subprocess.run(['tar','-xf',str(archive),'-C',str(base/'source')],check=True)
    subprocess.run(common+['-DF47_BASELINE','-c',str(ROOT/'bench/search-account/test.cpp'),'-o',str(base/'test.o')],check=True)
    subprocess.run(common+['-c',str(src/'yt/client.cpp'),'-o',str(base/'client.o')],check=True)
    flags=(ROOT/'client/build/CMakeFiles/tube360-main.dir/flags.make').read_text().splitlines()
    args=['c++','-c',str(src/'main.cpp'),'-o',str(base/'main.o')]
    for key in ('CXX_DEFINES =','CXX_INCLUDES =','CXX_FLAGS ='):
        for arg in shlex.split(next(x for x in flags if x.startswith(key)).split('=',1)[1]):
            if arg.startswith('-I'):arg=arg.replace(str(ROOT/'client/src'),str(src)).replace(str(ROOT/'client/third_party'),str(src.parent/'third_party'))
            args.append(arg)
    subprocess.run(args,check=True)
# Replace the client implementation in a private archive, avoiding mixed inline definitions.
build=ROOT/'client/build'
shutil.copy(build/'yt/libtube360-yt.a',base/'old-yt.a')
subprocess.run(['ar','d',str(base/'old-yt.a'),'client.cpp.o'],check=True)
for target,main,output in [('tube360','main.o','tube360-replay'),('search-test','test.o','test')]:
    args=shlex.split((build/f'CMakeFiles/{target}.dir/link.txt').read_text())
    args=[str(base/'old-yt.a') if a=='yt/libtube360-yt.a' else a for a in args if not a.endswith('.o')]
    args[args.index('-o')+1]=str(base/output)
    args[1:1]=[str(base/main),str(base/'client.o')]
    if target=='tube360':args.extend([str(work/'ui/transport.o'),'-Wl,--wrap=_ZN2yt5httpsEPKSt6atomicIbE','-Wl,--wrap=avcodec_receive_frame'])
    subprocess.run(args,cwd=build,check=True)
result=subprocess.run([str(base/'test')],capture_output=True,text=True)
(base/'before.log').write_text(result.stdout+result.stderr)
assert result.returncode==1 and '33 failures' in result.stdout,result.stdout+result.stderr
print(result.stdout.strip(),flush=True)
for name in ('search-pt','music-pt','player-pt'):
    after=work/'ui'/name;case=base/name;assets=case/'assets';shutil.copytree(after/'assets',assets,dirs_exist_ok=True)
    (assets/'local-library.json').unlink(missing_ok=True)
    env={k:v for k,v in os.environ.items() if not k.startswith(('XT_','F47_'))}
    env.update(HOME=str(case/'home'),XDG_CONFIG_HOME=str(case/'config'),XDG_CACHE_HOME=str(case/'cache'),SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',LP_NUM_THREADS='2',XT_CAST_DISABLE='1',XT_ASSETS=str(assets),F47_REPLAY=str(work/'ui/replay'),XT_ACCOUNT_FIXTURES=str(work/'ui/accounts'),XT_SHOT=str(case/'screen.ppm'),XT_SHOT_MS='3000',XT_KEYS_MS='180',XT_SHOT_FRAME='2147483647')
    if name.startswith('player'):env.update(XT_PLAY='R6MlUcmOul8',XT_KEYS='....DLL')
    else:env.update(XT_NAV='search' if name.startswith('search') else 'music',XT_NAV_QUERY='astronomia' if name.startswith('search') else '')
    if name.startswith('music'):env['XT_MUSIC']='1'
    with (case/'app.log').open('w') as f:subprocess.run([str(base/'tube360-replay')],cwd=case,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=40,check=True)
    log=(case/'app.log').read_text()
    if name.startswith('search'):assert 'navegação search: 0 itens' in log,log[-3000:]
    with Image.open(case/'screen.ppm') as im:
        if name.startswith('music'):
            for x in (136,291,446):
                pixels=list(im.convert('RGB').crop((x,129,x+110,239)).get_flattened_data())
                assert sum(r>150 and g<140 and b<150 for r,g,b in pixels)==0,'rc3 unexpectedly has shortcut art'
        im.save(case/'screen.png')
    print('rc3 reproduction',name,case/'screen.png',flush=True)
