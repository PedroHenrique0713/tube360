"""Exerce tube360_set_output real do V4X preparado com o parser real, sem GPU/MMIO."""
from pathlib import Path
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('evidence', type=Path)
p.add_argument('--source', type=Path, default=ROOT / '.work/native-display/vulkan4xenon/src/display.c')
a = p.parse_args()
base = a.evidence.resolve()
base.mkdir(parents=True, exist_ok=True)
sdk = ROOT / '.work/native-display/xsdk'
source = a.source.read_text()
start = source.index('int tube360_set_output(int height)')
brace = source.index('{', start)
end, depth = brace + 1, 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
function = source[start:end]
prefix = '''#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "xsdk/kernel.h"
#include "xsdk/tube360_video.h"
#include "board_sim.h"
uint32_t tube360_display_width=1280,tube360_display_height=720;
static int native;
int tube360_native_video(void){return native;}
const char* boot_log_path(void){return "boot-app.log";}
void boot_event(const char* p,const char* s,const char* f,...){printf("%s %s ",p,s);va_list a;va_start(a,f);vprintf(f,a);va_end(a);puts("");}
void xsdk_log_write(const char* f,...){(void)f;}
void VdQueryVideoMode(VIDEO_MODE* m){*m=(VIDEO_MODE){.display_width=1920,.display_height=1080,.is_widescreen=1};
 if(getenv("FIXTURE_DASHBOARD_720")){m->display_width=1280;m->display_height=720;}}
'''
main = '''
int main(int argc,char** argv){assert(argc==5);native=atoi(argv[1]);tube360_board_sim_init();
 int h=tube360_set_output(1080);assert(h==atoi(argv[3]));
 assert(tube360_display_width==(unsigned)atoi(argv[2]) && tube360_display_height==(unsigned)atoi(argv[3]));
 assert(tube360_board_sim_mapped(native)==atoi(argv[4]));
 printf("PASS real V4X output %ux%u mapped_allowed=%d\\n",tube360_display_width,tube360_display_height,tube360_board_sim_mapped(native));}
'''
cases = [
    ('unchanged-native', None, 1, 1920, 1080, 1),
    ('safe-over-native-request', 'board=jasper\n', 0, 1280, 720, 0),
    ('native-contract-720', 'board=xenon\ndriver=native\n', 1, 1280, 720, 1),
    ('native-contract-4x3', 'board=falcon\ndriver=native\nmode=480p\n', 1, 640, 480, 1),
    ('native-contract-wide-sd', 'board=corona-e\ndriver=native\nmode=480w\n', 1, 848, 480, 1),
    ('native-staging', 'board=corona-e\ndriver=native\nmapped=off\n', 1, 1920, 1080, 0),
    ('real-kernel-limit', 'board=corona-e\ndriver=native\nmode=1080p\n', 1, 1280, 720, 1),
]
with tempfile.TemporaryDirectory(prefix='tube360-board-driver-') as tmp:
    work = Path(tmp)
    (work / 'driver.c').write_text(prefix + function + main)
    exe = work / 'driver'
    subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-I' + str(sdk / 'include'),
                    '-I' + str(ROOT / 'client/src'), str(work / 'driver.c'), str(ROOT / 'client/src/board_sim.c'),
                    '-o', str(exe)], check=True)
    for name, config, native, w, h, mapped in cases:
        folder = base / name
        folder.mkdir(exist_ok=True)
        file = folder / 'board-sim.txt'
        file.unlink(missing_ok=True)
        if config is not None:
            file.write_text(config)
        env = {k: v for k, v in os.environ.items() if k not in ('XT_BOARD', 'FIXTURE_DASHBOARD_720')}
        if name == 'real-kernel-limit':
            env['FIXTURE_DASHBOARD_720'] = '1'
        run = subprocess.run([str(exe), str(native), str(w), str(h), str(mapped)], cwd=folder, env=env,
                             capture_output=True, text=True)
        (folder / 'result.log').write_text(run.stdout + run.stderr)
        assert run.returncode == 0, (name, run.stdout, run.stderr)
        print('PASS driver', name, w, h, flush=True)
