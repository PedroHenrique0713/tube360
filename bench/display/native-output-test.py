"""Exercita a seleção de framebuffer gerada para Xbox, com o painel substituído por dados.

Compila o código real de `tube360_display_mode`/`tube360_display_kernel_extent` (xsdk) e `tube360_set_output`
(vulkan4xenon) das cópias preparadas por tools/build-xbox.sh. TUBE360_NATIVE_DISPLAY aponta outra pasta com
`xsdk` e `vulkan4xenon` já preparados (padrão: .work/native-display)."""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
base=Path(os.environ.get('TUBE360_NATIVE_DISPLAY',root/'.work/native-display'))
sdk=(base/'xsdk/src/display.c').read_text()
a=sdk.index('XsdkDisplayMode tube360_display_mode(XsdkDisplayMode requested)')
b=sdk.index('NTSTATUS xsdk_display_open',a)
mode_source=sdk[a:b]
v4x=(base/'vulkan4xenon/src/display.c').read_text()
start=v4x.index('uint32_t tube360_display_width =')
end=v4x.index('VKAPI',start)
header='''#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <math.h>
typedef struct { uint32_t display_width,display_height,is_interlaced,is_widescreen,is_hi_def; float refresh_rate; uint32_t video_standard,reserved[5]; } VIDEO_MODE;
typedef enum { XSDK_DISPLAY_720P, XSDK_DISPLAY_KERNEL, XSDK_DISPLAY_1080P, XSDK_DISPLAY_480P, XSDK_DISPLAY_480W } XsdkDisplayMode;
static VIDEO_MODE panel;
static uint32_t registers[0x20000/4];
static volatile uint32_t* gpu_registers=registers;
#define AVIVO_D1MODE_VIEWPORT_SIZE 0x1961
static bool native_path=true;
static bool tube360_native_path(void){return native_path;}
#define VIDEO_BOOT(phase,...) ((void)(phase))
int (*tube360_board_sim_sd)(void);
float (*tube360_board_sim_aspect)(void);
int (*tube360_board_sim_extent)(int,int,int*,int*);
void VdQueryVideoMode(VIDEO_MODE* out) { *out=panel; }
void xsdk_log_write(const char* format,...) { (void)format; }
'''
tests='''
static void verifyvp(const char* name,VIDEO_MODE mode,unsigned viewport_width,int wanted,unsigned w,unsigned h,float aspect) {
    panel=mode; registers[AVIVO_D1MODE_VIEWPORT_SIZE]=viewport_width<<16|480;
    int got=tube360_set_output(wanted);
    float a=tube360_output_aspect();
    if(got!=(int)h || tube360_display_width!=w || tube360_display_height!=h || fabsf(a-aspect)>.001f) {
        printf("FAIL %s: got %ux%u aspect %.3f, wanted %ux%u aspect %.3f\\n",name,tube360_display_width,tube360_display_height,a,w,h,aspect);
        failed=1;
    }
    // O resolve do Xenos exige largura múltipla de 32 em qualquer caminho, inclusive o do kernel.
    if(tube360_display_width%32) { printf("FAIL %s: width %u is not a multiple of 32\\n",name,tube360_display_width); failed=1; }
}
static void verify(const char* name,VIDEO_MODE mode,int wanted,unsigned w,unsigned h,float aspect) { verifyvp(name,mode,640,wanted,w,h,aspect); }
int main(void) {
    // (largura, altura, entrelaçado, wide, hi_def, ...)
    verify("CRT 480i 4:3 (issue 43): viewport do kernel",(VIDEO_MODE){640,480,1,0},720,640,480,4.f/3);
    verify("480p 4:3",(VIDEO_MODE){640,480,0,0},720,640,480,4.f/3);
    verify("480p wide: viewport do kernel",(VIDEO_MODE){720,480,1,1},720,640,480,16.f/9);
    verifyvp("480i wide com viewport 852 (issue 7): SD proprio",(VIDEO_MODE){720,480,1,1},852,720,864,480,16.f/9);
    verifyvp("480p wide com viewport 852",(VIDEO_MODE){720,480,0,1},852,720,864,480,16.f/9);
    verifyvp("1080i wide com viewport 852",(VIDEO_MODE){1920,1080,1,1},852,1080,864,480,16.f/9);
    verifyvp("1080i wide com viewport 640: kernel",(VIDEO_MODE){1920,1080,1,1},640,1080,640,480,16.f/9);
    verify("720p com Xbox em 4:3 (sem wide): tela 4:3",(VIDEO_MODE){1280,720,0,0},1080,640,480,4.f/3);
    verify("TV 720p, saida 720",(VIDEO_MODE){1280,720,0,1},720,1280,720,0);
    verify("TV 1080p, saida 1080",(VIDEO_MODE){1920,1080,0,1},1080,1920,1080,0);
    verify("TV 720p, saida 480 pedida (issue 7: era 848, nao multiplo de 32)",(VIDEO_MODE){1280,720,0,1},480,864,480,16.f/9);
    verify("TV 1080p, saida 480 pedida",(VIDEO_MODE){1920,1080,0,1},480,864,480,16.f/9);
    verify("monitor 1280x1024 5:4, saida 720: 640x480 com pixel 5:4 (issue 7)",(VIDEO_MODE){1280,1024,0,0},720,640,480,1280.f/1024);
    verify("monitor 1280x1024 5:4, saida 1080 pedida",(VIDEO_MODE){1280,1024,0,0},1080,640,480,1280.f/1024);
    verify("monitor 1280x1024 5:4, saida 480 pedida",(VIDEO_MODE){1280,1024,0,0},480,640,480,1280.f/1024);
    verify("VGA 1360x768 sem wide: pelo raster, nao 4:3 (16:9 de monitor)",(VIDEO_MODE){1360,768,0,0},720,640,480,1360.f/768);
    verify("VGA 1366x768 sem wide",(VIDEO_MODE){1366,768,0,0},720,640,480,1366.f/768);
    verify("1080p sem wide: tela 4:3 como o xsdk",(VIDEO_MODE){1920,1080,0,0},1080,640,480,4.f/3);
    verify("monitor 1024x768 4:3",(VIDEO_MODE){1024,768,0,0},720,640,480,4.f/3);
    native_path=false;
    verify("safe 720p wide",(VIDEO_MODE){1280,720,0,1},480,1280,720,0);
    verifyvp("safe com viewport 852 wide: SD proprio, nao 1280x720 sobre 852",(VIDEO_MODE){720,480,1,1},852,720,864,480,16.f/9);
    verify("safe 5:4 segue 1280x720 original (letterbox do xsdk)",(VIDEO_MODE){1280,1024,0,0},720,1280,720,0);
    puts(failed?"FAILED":"all dashboard/output combinations passed (no physical display test)");
    return failed;
}
'''
mode_decl='static int failed;\n'
with tempfile.TemporaryDirectory(prefix='tube360-native-output-') as tmp:
    c=Path(tmp)/'test.c'
    c.write_text(header.replace('#include <math.h>','#include <math.h>\n#include <stdio.h>')+mode_decl+mode_source
                 +v4x[start:end]+tests)
    exe=Path(tmp)/'test'
    subprocess.run(['cc','-std=c17','-Wall','-Werror','-Wno-unused-function',str(c),'-o',str(exe),'-lm'],check=True)
    subprocess.run([str(exe)],check=True)
