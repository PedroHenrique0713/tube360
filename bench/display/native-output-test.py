"""Exercita a seleção de framebuffer gerada para Xbox, com o painel substituído por dados."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
source=(root/'.work/native-display/vulkan4xenon/src/display.c').read_text()
start=source.index('uint32_t tube360_display_width =')
end=source.index('VKAPI_ATTR',start)
source=source[start:end]
header='''#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
typedef struct { int display_width,display_height,is_interlaced,is_widescreen; } VIDEO_MODE;
static VIDEO_MODE panel;
void VdQueryVideoMode(VIDEO_MODE* out) { *out=panel; }
void xsdk_log_write(const char* format,...) { (void)format; }
'''
tests='''
void verify(VIDEO_MODE mode,int wanted,unsigned w,unsigned h) {
    panel=mode; assert(tube360_set_output(wanted)==(int)h);
    assert(tube360_display_width==w && tube360_display_height==h);
}
int main(void) {
    verify((VIDEO_MODE){640,480,1,0},720,640,480);
    verify((VIDEO_MODE){640,480,0,0},720,640,480);
    verify((VIDEO_MODE){720,480,1,1},720,848,480);
    verify((VIDEO_MODE){1280,720,0,0},1080,640,480);
    verify((VIDEO_MODE){1280,720,0,1},720,1280,720);
    verify((VIDEO_MODE){1920,1080,0,1},1080,1920,1080);
    verify((VIDEO_MODE){1920,1080,1,1},1080,1280,720);
    verify((VIDEO_MODE){1280,720,0,1},480,848,480);
}
'''
with tempfile.TemporaryDirectory(prefix='tube360-native-output-') as tmp:
    c=Path(tmp)/'test.c'; c.write_text(header+source+tests)
    exe=Path(tmp)/'test'
    subprocess.run(['cc','-std=c17','-Wall','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('8 dashboard/output combinations passed (no physical display test)')
