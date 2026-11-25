"""Contrato mínimo board-sim e caminho SD do app real; sem simular sinal físico."""
import os
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
base=Path(sys.argv[1]).resolve();base.mkdir(parents=True,exist_ok=True)
fixture=r'''
#include "board_sim.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
const char* boot_log_path(void){return "boot-app.log";}
void boot_event(const char* p,const char* s,const char* f,...){printf("%s %s ",p,s);va_list a;va_start(a,f);vprintf(f,a);va_end(a);puts("");}
int main(int argc,char** argv){
 assert(argc==5);tube360_board_sim_init();tube360_board_sim_log();
 int driver=tube360_board_sim_driver(),w=0,h=0;
 assert(driver==atoi(argv[1]) && tube360_board_sim_active()==(driver>=0));
 int extent=tube360_board_sim_extent(1080,driver>0,&w,&h);
 assert(w==atoi(argv[2]) && h==atoi(argv[3]) && !strcmp(tube360_board_sim_name(),argv[4]));
 assert(extent==(driver>=0) && tube360_board_sim_display_ok(driver>0));
 FILE* changed=fopen("board-sim.txt","w");assert(changed);fputs("board=corona\ndriver=native\nmode=1080p\ncable=hdmi\n",changed);fclose(changed);
 tube360_board_sim_init();assert(tube360_board_sim_driver()==driver);
 printf("PASS board-sim parser driver=%d extent=%dx%d immutable=1\n",driver,w,h);
}
'''
cases=[('absent',None,-1,0,0,'none'),
 ('native','board=corona\ndriver=native\nmode=480i\ncable=component\n',1,640,480,'corona'),
 ('safe','board=corona\ndriver=safe\nmode=480i\ncable=component\n',0,640,480,'corona'),
 ('pal','board=corona\ndriver=safe\nmode=576i\ncable=component\n',0,640,576,'corona'),
 ('e-component','board=corona-e\ndriver=native\nmode=480i\ncable=component\n',0,1280,720,'unknown'),
 ('vga-interlaced','board=corona\nmode=480i\ncable=vga\n',0,1280,720,'unknown'),
 ('unsupported-field','board=corona\nunknown_field=20\n',0,1280,720,'unknown'),
 ('duplicate','board=corona\ndriver=native\ndriver=safe\n',0,1280,720,'unknown'),
 ('nul','board=corona\x00\n',0,1280,720,'unknown'),
 ('oversize','board=corona\n'+' '*1100,0,1280,720,'unknown')]
with tempfile.TemporaryDirectory(prefix='tube360-board-sim-') as tmp:
    work=Path(tmp);(work/'test.c').write_text(fixture)
    subprocess.run(['cc','-std=c11','-I'+str(ROOT/'client/src'),str(work/'test.c'),str(ROOT/'client/src/board_sim.c'),'-o',str(work/'test')],check=True)
    for name,config,driver,w,h,board in cases:
        folder=base/name;folder.mkdir(exist_ok=True);path=folder/'board-sim.txt';path.unlink(missing_ok=True)
        (folder/'input.json').write_text(json.dumps(dict(config=config,driver=driver,width=w,height=h,board=board),indent=2)+'\n')
        if config is not None:path.write_text(config)
        run=subprocess.run([str(work/'test'),str(driver),str(w),str(h),board],cwd=folder,capture_output=True,text=True)
        (folder/'test.log').write_text(run.stdout+run.stderr)
        assert run.returncode==0,(name,run.stdout,run.stderr)
        print('PASS parser',name,flush=True)
for driver in ('native','safe'):
    folder=base/('app-'+driver);assets=folder/'assets';assets.mkdir(parents=True,exist_ok=True)
    for font in (ROOT/'client/assets').glob('inter-*.ttf'):shutil.copy(font,assets)
    (assets/'board-sim.txt').write_text(f'board=corona\ndriver={driver}\nmode=480i\ncable=component\n')
    (assets/'settings.txt').write_text('lang=pt\nserver=off\noutput_height=1080\n')
    (folder/'error.txt').write_text('network');shot=folder/'screen.ppm';shot.unlink(missing_ok=True)
    env={k:v for k,v in os.environ.items() if not k.startswith('XT_')}
    env.update(HOME=str(folder),XDG_CACHE_HOME=str(folder/'cache'),SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',XT_CAST_DISABLE='1',XT_ASSETS=str(assets),XT_YT_REPLAY=str(folder),XT_SCREEN='settings',XT_KEYS='D'*9+'RRR',XT_KEYS_MS='100',XT_SHOT=str(shot),XT_SHOT_MS='4000')
    run=subprocess.run([str(ROOT/'client/build/tube360')],cwd=folder,env=env,capture_output=True,text=True,timeout=25)
    (folder/'player.log').write_text(run.stdout+run.stderr)
    assert run.returncode==0 and shot.exists(),(driver,run.stdout[-3000:])
    boot=(assets/'boot-app.log').read_text()
    assert f'profile=corona family=5 driver={driver} mode=480i cable=component simulated=1' in boot,boot
    assert 'end gpu.swapchain extent=640x480' in boot and 'begin settings.output-retire' not in boot,boot
    assert boot.count('begin settings.output ')==3,boot
    with Image.open(shot) as image:assert image.size==(640,480);image.save(folder/'screen.png')
    shot.unlink();print('PASS app Corona mode=480i cable=component',driver,'640x480 fixed output',flush=True)
