#!/usr/bin/env python3
"""Seleciona em runtime o caminho original 720p/staging ou nossas extensões, nas cópias privadas."""
from pathlib import Path
import sys

HEADER='''#pragma once
#include <stdbool.h>
extern int tube360_native_video(void) __attribute__((weak));
extern void boot_event(const char*,const char*,const char*,...) __attribute__((weak));
static inline bool tube360_native_path(void) { return tube360_native_video && tube360_native_video(); }
#define VIDEO_BOOT(phase,step,...) do { if(boot_event)boot_event(phase,step,__VA_ARGS__); } while(0)
void tube360_display_boot_done(void);
'''

def prepare(sdk,v4x):
    sdk,v4x=Path(sdk),Path(v4x)
    stamp=sdk/'tube360-video-policy-v1'
    if stamp.exists():
        if not (v4x/'tube360-video-policy-v1').exists():raise RuntimeError('SDK/V4X policy copies do not match')
        finish(sdk,v4x)
        return
    pending={}
    def replace(root,name,before,after):
        path=root/'src'/name;text=pending.get(path)
        if text is None:text=path.read_text()
        if text.count(before)!=1:raise RuntimeError(f'Incompatible video hook: {path}: {before[:70]!r}')
        pending[path]=text.replace(before,after)
    pending[sdk/'include/xsdk/tube360_video.h']=HEADER
    for root,names in ((sdk,('display.c','present.c','gpu.c')),(v4x,('display.c','physical_device.c','memory.c','image.c'))):
        for name in names:
            anchor='#include "xsdk/gpu.h"' if name=='physical_device.c' else '#include "xsdk/log.h"'
            replace(root,name,anchor,anchor+'\n#include "xsdk/tube360_video.h"')
    replace(v4x,'display.c','int tube360_set_output(int height)\n{','''int tube360_set_output(int height)
{
    if(!tube360_native_path()) {
        tube360_display_width=1280;tube360_display_height=720;
        VIDEO_BOOT("detect","display.output","path=safe requested=%d selected=1280x720; original V4X extent",height);
        return 720;
    }
    VIDEO_BOOT("begin","display.output","path=native; query dashboard before SD/1080 selection");''')
    replace(v4x,'display.c','    return (int)tube360_display_height;','    VIDEO_BOOT("end","display.output","path=native extent=%ux%u",tube360_display_width,tube360_display_height);\n    return (int)tube360_display_height;')
    replace(sdk,'present.c','\tif (mode != XSDK_DISPLAY_720P) xsdk_splash_end();','''    if(!tube360_native_path() && mode!=XSDK_DISPLAY_KERNEL)mode=XSDK_DISPLAY_720P;
    VIDEO_BOOT("begin","display.splash","mode=%d path=%s",mode,tube360_native_path()?"native":"safe");
\tif (mode != XSDK_DISPLAY_720P) xsdk_splash_end();''')
    replace(sdk,'present.c','\tconst NTSTATUS status = splash_held ? STATUS_SUCCESS : xsdk_display_open(mode, width, height);','''    VIDEO_BOOT("end","display.splash","held=%d",splash_held);
    VIDEO_BOOT("begin","display.open","splash held=%d",splash_held);
\tconst NTSTATUS status = splash_held ? STATUS_SUCCESS : xsdk_display_open(mode, width, height);
    VIDEO_BOOT(NT_SUCCESS(status)?"end":"error","display.open","status=%08x extent=%ux%u",(unsigned)status,*width,*height);''')
    replace(sdk,'present.c','\tdescribe_display(present.width, present.height);','''    VIDEO_BOOT("begin","display.describe","XAM display description");
\tdescribe_display(present.width, present.height);
    VIDEO_BOOT("end","display.describe","returned");
    VIDEO_BOOT("begin","display.buffers","allocate and clear %u buffers %ux%u",buffer_count,present.width,present.height);''')
    replace(sdk,'present.c','\t// The last buffer stands for frame -1,','    VIDEO_BOOT("end","display.buffers","cleared black");\n    VIDEO_BOOT("begin","display.first-show","before scaler/framebuffer MMIO");\n\t// The last buffer stands for frame -1,')
    replace(sdk,'present.c','\tpresent.open = true;\n\tVdSetGraphicsInterruptCallback(on_interrupt, NULL);','''    VIDEO_BOOT("end","display.first-show","returned");
    tube360_display_boot_done();
\tpresent.open = true;
    VIDEO_BOOT("begin","display.interrupt","install callback; future IRQ reads 0x1951/0x194c and flips surface; no file I/O in IRQ");
\tVdSetGraphicsInterruptCallback(on_interrupt, NULL);
    VIDEO_BOOT("end","display.interrupt","installed");''')
    replace(sdk,'display.c','static bool is_open;','''static bool is_open;
static bool tube360_display_boot;
void tube360_display_boot_done(void){tube360_display_boot=false;}''')
    replace(sdk,'display.c','\tassert(!is_open);','''\tassert(!is_open);
    tube360_display_boot=true;
    if(!tube360_native_path() && mode!=XSDK_DISPLAY_KERNEL)mode=XSDK_DISPLAY_720P;
    VIDEO_BOOT("begin","display.viewport","read MMIO 0x1961; mode=%d path=%s",mode,tube360_native_path()?"native":"safe");''')
    replace(sdk,'display.c','\tconst uint32_t viewport = gpu_registers[AVIVO_D1MODE_VIEWPORT_SIZE];','\tconst uint32_t viewport = gpu_registers[AVIVO_D1MODE_VIEWPORT_SIZE];\n    VIDEO_BOOT("end","display.viewport","value=%08x",viewport);')
    replace(sdk,'display.c','static NTSTATUS fill_lut(void)\n{','''static NTSTATUS fill_lut(void)
{
    VIDEO_BOOT("begin","display.lut","MMIO 0x1930,0x1922,0x1921,0x1927,0x1928; bounded poll 100ms");''')
    replace(sdk,'display.c','\t\tif (waited_ms == AUTOFILL_TIMEOUT_MS)\n\t\t\treturn STATUS_IO_TIMEOUT;','''        if (waited_ms == AUTOFILL_TIMEOUT_MS) {
            VIDEO_BOOT("error","display.lut","timeout");return STATUS_IO_TIMEOUT;
        }''')
    replace(sdk,'display.c','\treturn STATUS_SUCCESS;\n}\n\n// Source over output','    VIDEO_BOOT("end","display.lut","ready");\n\treturn STATUS_SUCCESS;\n}\n\n// Source over output')
    replace(sdk,'display.c','\told_surface = (Surface){','    VIDEO_BOOT("begin","display.saved-surface","read MMIO 0x1844,0x1848,0x184d,0x184e");\n\told_surface = (Surface){')
    replace(sdk,'display.c','\tVIDEO_MODE video;\n\tVdQueryVideoMode(&video);\n\tif (mode == XSDK_DISPLAY_1080P','    VIDEO_BOOT("end","display.saved-surface","saved");\n\tVIDEO_MODE video;\n\tVdQueryVideoMode(&video);\n\tif (mode == XSDK_DISPLAY_1080P')
    replace(sdk,'display.c','\tfor (uint32_t i = 0; i < SCALER_REGISTERS; i++)\n\t\told_scaler[i] = gpu_registers[scaler_registers[i]];','''    VIDEO_BOOT("begin","display.scaler-snapshot","read 10 scaler registers; source=%ux%u",width,height);
\tfor (uint32_t i = 0; i < SCALER_REGISTERS; i++)
\t\told_scaler[i] = gpu_registers[scaler_registers[i]];
    VIDEO_BOOT("end","display.scaler-snapshot","saved");''')
    replace(sdk,'display.c','static void write_scaler(const uint32_t *values)\n{','''static void write_scaler(const uint32_t *values)
{
    if(tube360_display_boot)VIDEO_BOOT("begin","display.scaler-write","lock 0x1973; write 10 registers; release lock");''')
    replace(sdk,'display.c','\tgpu_registers[AVIVO_D1SCL_UPDATE] = 0;','\tgpu_registers[AVIVO_D1SCL_UPDATE] = 0;\n    if(tube360_display_boot)VIDEO_BOOT("end","display.scaler-write","written");')
    replace(sdk,'display.c','static NTSTATUS apply(const Surface *surface)\n{','''static NTSTATUS apply(const Surface *surface)
{
    if(tube360_display_boot)VIDEO_BOOT("begin","display.surface-write","MMIO 0xa10/0xa11,0x1844/48/4d/4e,0x183b; bounded poll 100ms");''')
    replace(sdk,'display.c','\t\t\tgpu_registers[D1CRTC_DOUBLE_BUFFER_CONTROL] = 0;\n\t\t\treturn STATUS_IO_TIMEOUT;','''\t\t\tgpu_registers[D1CRTC_DOUBLE_BUFFER_CONTROL] = 0;
            if(tube360_display_boot)VIDEO_BOOT("error","display.surface-write","timeout");
\t\t\treturn STATUS_IO_TIMEOUT;''')
    replace(sdk,'display.c','\tgpu_registers[D1CRTC_DOUBLE_BUFFER_CONTROL] = 0;\n\treturn STATUS_SUCCESS;','\tgpu_registers[D1CRTC_DOUBLE_BUFFER_CONTROL] = 0;\n    if(tube360_display_boot)VIDEO_BOOT("end","display.surface-write","applied");\n\treturn STATUS_SUCCESS;')
    replace(sdk,'display.c','static void wait_for_scaler(uint32_t height)\n{','''static void wait_for_scaler(uint32_t height)
{
    if(tube360_display_boot)VIDEO_BOOT("begin","display.line-counter","read MMIO 0x194c; height=%u poll<=50000us; inherited xsdk wait",height);''')
    replace(sdk,'display.c','\txsdk_log_write("display: the line counter reached %u after %u us",','    if(tube360_display_boot)VIDEO_BOOT("end","display.line-counter","line=%u waited_us=%u",gpu_registers[R500_D1MODE_V_COUNTER],waited_us);\n\txsdk_log_write("display: the line counter reached %u after %u us",')
    replace(sdk,'display.c','\tif (scaler_applied)\n\t\twrite_scaler(old_scaler);','    VIDEO_BOOT("begin","display.restore","restore scaler/surface MMIO after callback removal");\n\tif (scaler_applied)\n\t\twrite_scaler(old_scaler);')
    replace(sdk,'display.c','\tis_open = false;','\tis_open = false;\n    VIDEO_BOOT("end","display.restore","returned");')
    # Nenhuma gravação em callbacks/por frame: só a inicialização da GPU.
    pairs=[('ring = allocate(RING_BYTES, PAGE_WRITECOMBINE);','gpu.alloc-ring','write-combined ring'),
           ('writeback = allocate(PAGE_BYTES, PAGE_NOCACHE);','gpu.alloc-writeback','uncached writeback'),
           ('stream = allocate(STREAM_BYTES, PAGE_WRITECOMBINE);','gpu.alloc-stream','write-combined stream'),
           ('xsdk_gpu_hiz_drop();','gpu.hiz','initialize HiZ state'),
           ('const uint32_t engines = VdInitializeEngines(ENGINES_FLAGS, (void *)engines_shut_down, NULL, NULL, NULL);','gpu.engines','VdInitializeEngines'),
           ('VdInitializeRingBuffer(MmGetPhysicalAddress((const void *)ring), RING_SIZE_LOG2);','gpu.ring','VdInitializeRingBuffer'),
           ('VdEnableRingBufferRPtrWriteBack(writeback_physical + RPTR_OFFSET, RPTR_BLOCK_SIZE_LOG2);','gpu.writeback','VdEnableRingBufferRPtrWriteBack'),
           ('VdSetSystemCommandBufferGpuIdentifierAddress(&writeback[SYSTEM_FENCE_OFFSET / 4]);','gpu.system-fence','VdSetSystemCommandBufferGpuIdentifierAddress'),
           ('xsdk_gpu_set_up();','gpu.setup','initial GPU packets/register setup'),
           ('const NTSTATUS status = xsdk_gpu_wait(xsdk_gpu_fence(), SET_UP_TIMEOUT_MS);','gpu.setup-fence','initial fence wait <=1000ms')]
    for call,step,detail in pairs:
        path=sdk/'src/gpu.c';text=pending[path]
        start=text.index('NTSTATUS xsdk_gpu_open(void)');end=text.index('NTSTATUS xsdk_gpu_close(void)',start)
        body=text[start:end];needle='\t'+call
        if body.count(needle)!=1:raise RuntimeError(f'Incompatible GPU open hook: {call}')
        pending[path]=text[:start]+body.replace(needle,'    VIDEO_BOOT("begin","'+step+'","'+detail+'");\n\t'+call+'\n    VIDEO_BOOT("end","'+step+'","returned");')+text[end:]
    replace(v4x,'physical_device.c','.memoryTypeCount = 3,','.memoryTypeCount = tube360_native_path() ? 3 : 2,')
    replace(v4x,'physical_device.c','if (format == VK_FORMAT_R8_UNORM)','if (tube360_native_path() && format == VK_FORMAT_R8_UNORM)')
    for file in ('physical_device.c','image.c'):
        replace(v4x,file,'V4X_LINEAR_IMAGE_USAGE','(tube360_native_path() ? V4X_LINEAR_IMAGE_USAGE : (V4X_LINEAR_IMAGE_USAGE & ~VK_IMAGE_USAGE_SAMPLED_BIT))')
    replace(v4x,'image.c','object->is_texture && !object->texture.tiled ? 4u : 0u','tube360_native_path() && object->is_texture && !object->texture.tiled ? 4u : 0u')
    replace(v4x,'memory.c','if (type != MEMORY_TYPE_GPU && type != MEMORY_TYPE_CACHED && type != 2)','if ((type != MEMORY_TYPE_GPU && type != MEMORY_TYPE_CACHED && type != 2) || (type == 2 && !tube360_native_path()))')
    for path,text in pending.items():path.write_text(text)
    stamp.write_text('safe original 720p and staging; native SD/1080/mapped opt-in; durable setup checkpoints\n')
    (v4x/'tube360-video-policy-v1').write_text(stamp.read_text())
    finish(sdk,v4x)

def finish(sdk,v4x):
    finish_present(sdk)
    stamp=sdk/'tube360-video-policy-v2'
    if stamp.exists():
        finish_sd(sdk,v4x)
        finish_sim_display(sdk,v4x)
        return
    display=sdk/'src/display.c';text=display.read_text()
    needle='\tVIDEO_MODE video;\n\tVdQueryVideoMode(&video);\n\tif (mode == XSDK_DISPLAY_1080P'
    assert text.count(needle)==1
    text=text.replace(needle,'\tVIDEO_MODE video={0};\n    if(tube360_native_path())VdQueryVideoMode(&video);\n\tif (mode == XSDK_DISPLAY_1080P')
    memory=v4x/'src/memory.c';body=memory.read_text()
    for name in ('vkFlushMappedMemoryRanges','vkInvalidateMappedMemoryRanges'):
        start=body.index('VKAPI_ATTR VkResult VKAPI_CALL '+name+'(');brace=body.index('{',start)
        body=body[:brace+1]+'\n    if(!tube360_native_path())return VK_SUCCESS;'+body[brace+1:]
    display.write_text(text);memory.write_text(body)
    stamp.write_text('safe cache operations remain original no-ops; no extra native-mode query\n')
    (v4x/'tube360-video-policy-v2').write_text(stamp.read_text())
    finish_sd(sdk,v4x)
    finish_sim_display(sdk,v4x)

def finish_sim_display(sdk,v4x):
    stamp=sdk/'tube360-video-policy-v5'
    if stamp.exists():return
    header=sdk/'include/xsdk/tube360_video.h'
    header.write_text(header.read_text()+'''extern int tube360_board_sim_sd(void) __attribute__((weak));
extern float tube360_board_sim_aspect(void) __attribute__((weak));
''')
    path=sdk/'src/display.c';text=path.read_text()
    before='if(video.is_interlaced || (video.display_height && video.display_height<=576))selected=XSDK_DISPLAY_KERNEL;'
    after='if(video.is_interlaced || (video.display_height && video.display_height<=576) || (tube360_board_sim_sd && tube360_board_sim_sd()))selected=XSDK_DISPLAY_KERNEL;'
    if text.count(before)!=1:raise RuntimeError('Incompatible simulated SD display hook')
    path.write_text(text.replace(before,after))
    path=v4x/'src/display.c';text=path.read_text()
    before='tube360_display_aspect=video.is_widescreen ? 16.0f/9.0f : 4.0f/3.0f;'
    after=before+'\n        if(tube360_board_sim_aspect && tube360_board_sim_aspect()>0)tube360_display_aspect=tube360_board_sim_aspect();'
    if text.count(before)!=1:raise RuntimeError('Incompatible simulated SD aspect hook')
    path.write_text(text.replace(before,after))
    stamp.write_text('board-sim SD selects kernel path in safe and native; real viewport/timings preserved\n')

def finish_sd(sdk,v4x):
    stamp=sdk/'tube360-video-policy-v4'
    if stamp.exists():return
    pending={}
    def replace(path,before,after):
        text=pending.get(path,path.read_text())
        if text.count(before)!=1:raise RuntimeError(f'Incompatible SD hook: {path}: {before[:70]!r}')
        pending[path]=text.replace(before,after)
    header=sdk/'include/xsdk/tube360_video.h'
    pending[header]=header.read_text()+'''#include "xsdk/display.h"
XsdkDisplayMode tube360_display_mode(XsdkDisplayMode requested);
bool tube360_display_kernel_extent(uint32_t *width,uint32_t *height);
'''
    display=sdk/'src/display.c'
    replace(display,'NTSTATUS xsdk_display_open(XsdkDisplayMode mode, uint32_t *width, uint32_t *height)', '''// SD/entrelaçado conserva o viewport e o scaler que o painel já programou.
XsdkDisplayMode tube360_display_mode(XsdkDisplayMode requested)
{
    VIDEO_MODE video={0};
    VIDEO_BOOT("begin","display.signal","query dashboard; signal timings unchanged");
    VdQueryVideoMode(&video);
    XsdkDisplayMode selected=requested;
    if(video.is_interlaced || (video.display_height && video.display_height<=576))selected=XSDK_DISPLAY_KERNEL;
    else if(!tube360_native_path() && requested!=XSDK_DISPLAY_KERNEL)selected=XSDK_DISPLAY_720P;
    VIDEO_BOOT("end","display.signal","dashboard=%ux%u interlaced=%u wide=%u hz=%.3f standard=%u requested=%d selected=%d kernel=%d",
        video.display_width,video.display_height,video.is_interlaced,video.is_widescreen,(double)video.refresh_rate,video.video_standard,requested,selected,selected==XSDK_DISPLAY_KERNEL);
    return selected;
}

bool tube360_display_kernel_extent(uint32_t *width,uint32_t *height)
{
    VIDEO_BOOT("begin","display.kernel-viewport","read MMIO 0x1961; preserve dashboard scaler");
    const uint32_t viewport=gpu_registers[AVIVO_D1MODE_VIEWPORT_SIZE];
    *width=viewport>>16;*height=viewport&0xffff;
    bool valid=*width>=320 && *width<=1920 && *height>=240 && *height<=1080;
    VIDEO_BOOT(valid?"end":"error","display.kernel-viewport","raw=%08x extent=%ux%u valid=%d",viewport,*width,*height,valid);
    if(!valid)*width=*height=0;
    return valid;
}

NTSTATUS xsdk_display_open(XsdkDisplayMode mode, uint32_t *width, uint32_t *height)''')
    for path in (display,sdk/'src/present.c'):
        replace(path,'    if(!tube360_native_path() && mode!=XSDK_DISPLAY_KERNEL)mode=XSDK_DISPLAY_720P;',
                '    mode=tube360_display_mode(mode);')
    output=v4x/'src/display.c'
    replace(output,'uint32_t tube360_display_width = 1280, tube360_display_height = 720;', '''uint32_t tube360_display_width = 1280, tube360_display_height = 720;
int tube360_display_kernel;
static float tube360_display_aspect;
int tube360_output_fixed(void){return !tube360_native_path() || tube360_display_kernel;}
float tube360_output_aspect(void){return tube360_display_kernel ? tube360_display_aspect : 0;}''')
    replace(output,'int tube360_set_output(int height)\n{','''int tube360_set_output(int height)
{
    tube360_display_kernel=tube360_display_mode(XSDK_DISPLAY_720P)==XSDK_DISPLAY_KERNEL;
    if(tube360_display_kernel) {
        VIDEO_MODE video={0};VdQueryVideoMode(&video);
        tube360_display_aspect=video.is_widescreen ? 16.0f/9.0f : 4.0f/3.0f;
        bool valid=tube360_display_kernel_extent(&tube360_display_width,&tube360_display_height);
        xsdk_log_write("display: dashboard framebuffer %ux%u; kernel viewport, scaler and signal unchanged",tube360_display_width,tube360_display_height);
        VIDEO_BOOT(valid?"detect":"error","display.output","path=%s requested=%d selected=%ux%u kernel=1; dashboard scaler unchanged",tube360_native_path()?"native":"safe",height,tube360_display_width,tube360_display_height);
        return (int)tube360_display_height;
    }''')
    replace(v4x/'src/objects.h','extern uint32_t tube360_display_width, tube360_display_height;',
            'extern uint32_t tube360_display_width, tube360_display_height;\nextern int tube360_display_kernel;')
    replace(v4x/'src/swapchain.c','xsdk_present_open(V4X_DISPLAY_HEIGHT == 480 ?',
            'xsdk_present_open(tube360_display_kernel ? XSDK_DISPLAY_KERNEL : V4X_DISPLAY_HEIGHT == 480 ?')
    for path,text in pending.items():path.write_text(text)
    stamp.write_text('SD/interlaced uses kernel viewport in SDK, display surface and swapchain; no custom scaler\n')
    (v4x/'tube360-video-policy-v4').write_text(stamp.read_text())

def finish_present(sdk):
    stamp=sdk/'tube360-video-policy-v3'
    if stamp.exists():return
    path=sdk/'src/present.c';text=path.read_text()
    before='''\tif (!splash_held && letterbox)
\t\t(void)xsdk_display_show_sized(present.addresses[buffer_count - 1], &present.own_size);
\telse if (!splash_held)
\t\t(void)xsdk_display_show(present.addresses[buffer_count - 1], *width);
    VIDEO_BOOT("end","display.first-show","returned");'''
    after='''    NTSTATUS shown=STATUS_SUCCESS;
\tif (!splash_held && letterbox)
\t\tshown=xsdk_display_show_sized(present.addresses[buffer_count - 1], &present.own_size);
\telse if (!splash_held)
\t\tshown=xsdk_display_show(present.addresses[buffer_count - 1], *width);
    if(!NT_SUCCESS(shown)) {
        VIDEO_BOOT("error","display.first-show","status=%08x; no interrupt installed",(unsigned)shown);
        for(uint32_t i=0;i<buffer_count;++i)xsdk_gpu_free(present.buffers[i]);
        xsdk_display_close();
        if(splash_held)xsdk_splash_free();
        return shown;
    }
    VIDEO_BOOT("end","display.first-show","status=%08x",(unsigned)shown);'''
    assert text.count(before)==1
    path.write_text(text.replace(before,after));stamp.write_text('propagate initial display error; free buffers before callback installation\n')

if __name__=='__main__':prepare(sys.argv[1],sys.argv[2])
