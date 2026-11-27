#!/usr/bin/env python3
"""Falha depois de SDL_Quit no main real: o launcher precisa encontrar a sessão aberta."""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT/'client/build'
source = r'''
#include "yt/client.hpp"
#include <atomic>
#include <cstdlib>
static std::atomic_bool stopped{false};
extern "C" void __real_SDL_Quit();
extern "C" void __wrap_SDL_Quit(){__real_SDL_Quit();stopped=true;}
namespace yt {
class ExitTransport final:public Transport {
public:
 ~ExitTransport(){if(stopped && std::getenv("EXIT_SESSION_FAIL"))std::_Exit(73);}
 Response request(const string&,const string&,bool)override{return {503,{},"offline fixture"};}
 Response send(const Request&)override{return {503,{},"offline fixture"};}
};
extern "C" std::unique_ptr<Transport> __wrap__ZN2yt5httpsEPKSt6atomicIbE(const std::atomic_bool*){return std::make_unique<ExitTransport>();}
}
'''
with tempfile.TemporaryDirectory(prefix='tube360-exit-session-') as temporary:
    base=Path(temporary);cpp=base/'transport.cpp';cpp.write_text(source);obj=base/'transport.o';binary=base/'app'
    link=shlex.split((build/'CMakeFiles/tube360.dir/link.txt').read_text())
    subprocess.run([link[0],'-std=c++20','-O1','-I'+str(ROOT/'client/src'),'-I'+str(ROOT/'client/third_party'),'-c',str(cpp),'-o',str(obj)],check=True)
    link[link.index('-o')+1]=str(binary)
    link.extend([str(obj),'-Wl,--wrap=_ZN2yt5httpsEPKSt6atomicIbE','-Wl,--wrap=SDL_Quit'])
    subprocess.run(link,cwd=build,check=True)
    for mode,expected_code,expected_state in [('normal',0,'clean'),('late-failure',73,'open')]:
        assets=base/mode;assets.mkdir()
        for font in (ROOT/'client/assets').glob('*.ttf'):shutil.copyfile(font,assets/font.name)
        (assets/'settings.txt').write_text('sound=tv\n')
        env={k:v for k,v in os.environ.items() if not k.startswith(('XT_','EXIT_SESSION_'))}
        env.update(HOME=str(assets),XDG_CONFIG_HOME=str(assets),XDG_CACHE_HOME=str(assets),
                   SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',LP_NUM_THREADS='2',XT_CAST_DISABLE='1',
                   XT_NEST_NETWORK='127.77.53.1/255.255.255.252',XT_ASSETS=str(assets),
                   XT_SHOT=str(assets/'screen.ppm'),XT_SHOT_MS='300',XT_SHOT_FRAME='2147483647')
        if mode=='late-failure':env['EXIT_SESSION_FAIL']='1'
        result=subprocess.run([str(binary)],cwd=ROOT,env=env,capture_output=True,text=True,timeout=15)
        assert result.returncode==expected_code,(mode,result.returncode,result.stdout,result.stderr)
        state=(assets/'session.txt').read_text().splitlines()[0]
        assert state==expected_state,(mode,'session marker',state,'expected',expected_state)
        print('PASS app exit session:',mode,'marker='+state,'exit='+str(result.returncode),flush=True)
print('2 actual app teardown scenarios passed; all HTTP replaced by an offline fixture.')
