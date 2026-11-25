"""Pipeline de preparo real, display contra o original e capacidades/alocação Vulkan reais no host."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('evidence',type=Path);p.add_argument('--source-sdk',type=Path,required=True);p.add_argument('--source-v4x',type=Path,required=True);a=p.parse_args();a.evidence=a.evidence.resolve();a.evidence.mkdir(parents=True,exist_ok=True)
def run(args,**kw):return subprocess.run(list(map(str,args)),check=True,**kw)
def extract(source,signature):
    start=source.index(signature);brace=source.index('{',start);depth=1;end=brace+1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
with tempfile.TemporaryDirectory(prefix='tube360-video-driver-') as tmp:
    work=Path(tmp);(work/'tools').mkdir()
    for name in ('prepare-native-display.py','prepare-video-memory.py','prepare-boot-diagnostics.py','prepare-video-policy.py'):shutil.copy(ROOT/'tools'/name,work/'tools'/name)
    sdk=work/'.work/native-display/xsdk';v4x=work/'.work/native-display/vulkan4xenon'
    run(['python3',work/'tools/prepare-native-display.py',a.source_sdk.resolve(),a.source_v4x.resolve()],stdout=subprocess.DEVNULL)
    run(['python3',work/'tools/prepare-video-memory.py',v4x]);run(['python3',work/'tools/prepare-boot-diagnostics.py',sdk,v4x]);run(['python3',work/'tools/prepare-video-policy.py',sdk,v4x])
    files=[*sdk.glob('src/*.c'),*v4x.glob('src/*'),sdk/'include/xsdk/tube360_video.h']
    first={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files if f.is_file()}
    run(['python3',work/'tools/prepare-video-policy.py',sdk,v4x])
    assert first=={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files if f.is_file()}
    print('PASS full preparation from original sources and policy idempotence',flush=True)
    include=['-idirafter',str(sdk/'include'),'-I'+str(sdk/'src'),'-I'+str(v4x/'src'),'-I'+str(v4x/'include'),'-I'+str(v4x/'third_party/vulkan-headers/include')]
    common=['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-DVK_USE_64_BIT_PTR_DEFINES=0','-ffunction-sections','-fdata-sections',*include]
    names='force_letterbox letterboxed output plan open flip show show_sized flip_sized shown_address close'.split()
    renamed=['-Dxsdk_display_'+name+'=reference_xsdk_display_'+name for name in names]
    run([*common,'-Dclock_gettime=fixture_clock_gettime',*renamed,'-c',a.source_sdk.resolve()/'src/display.c','-o',work/'reference.o'])
    run([*common,'-Dclock_gettime=fixture_clock_gettime','-c',sdk/'src/display.c','-o',work/'display.o'])
    run([*common,ROOT/'tests/reddit/display_policy.c',work/'reference.o',work/'display.o','-Wl,--gc-sections','-o',work/'display'])
    for case in ('safe-hd','safe-sd','safe-interlaced','safe-counter-stopped','native-hd','native-sd'):
        result=run([work/'display',case],capture_output=True,text=True);(a.evidence/(case+'.log')).write_text(result.stdout)
        assert 'begin display.viewport' in result.stdout and 'begin display.lut' in result.stdout
        assert result.stdout.index('begin display.viewport')<result.stdout.index('end display.viewport')<result.stdout.index('begin display.lut')
        if 'counter-stopped' in case:assert 'line=0 waited_us=50000' in result.stdout
        if 'interlaced' not in case and 'sd' not in case:
            assert result.stdout.index('begin display.scaler-write')<result.stdout.index('begin display.surface-write')<result.stdout.index('begin display.line-counter')
        print(result.stdout.splitlines()[-1],flush=True)
    # Funções integrais reais; não compila as rotinas PPC de cache no compilador x86.
    source=(v4x/'src/memory.c').read_text()
    enum=source[source.index('enum {'):source.index('};',source.index('enum {'))+2]
    functions=extract(source,'VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(')
    for name in ('vkFlushMappedMemoryRanges','vkInvalidateMappedMemoryRanges'):
        functions+='\n'+extract(source,'VKAPI_ATTR VkResult VKAPI_CALL '+name+'(')
    # Só as instruções PPC são fronteiras simuladas; guards, intervalos e loops são os originais.
    for instruction in ('dcbst','dcbf'):
        needle='__asm__ volatile("'+instruction+' 0, %0" :: "r"(p) : "memory");'
        assert functions.count(needle)==1
        functions=functions.replace(needle,'fixture_cache(p);')
    functions=functions.replace('__asm__ volatile("sync" ::: "memory");','fixture_sync();')
    (work/'allocate.c').write_text('#include <malloc.h>\n#include <vulkan/vulkan.h>\n#include "host.h"\n#include "objects.h"\n#include "xsdk/gpu.h"\n#include "xsdk/kernel.h"\n#include "xsdk/log.h"\n#include "xsdk/tube360_video.h"\nvoid fixture_cache(uintptr_t);void fixture_sync(void);\n'+enum+'\n'+functions)
    objects=[]
    for source in (v4x/'src/physical_device.c',v4x/'src/format.c',v4x/'src/image.c',work/'allocate.c'):
        obj=work/(source.stem+'.o');run([*common,'-c',source,'-o',obj]);objects.append(obj)
    run([*common,ROOT/'tests/reddit/video_memory.c',*objects,'-Wl,--gc-sections','-o',work/'memory'])
    for case in ('safe','native'):
        result=run([work/'memory',case],capture_output=True,text=True);(a.evidence/('memory-'+case+'.log')).write_text(result.stdout);print(result.stdout.strip(),flush=True)
    source=(sdk/'src/present.c').read_text()
    enum=source[source.index('enum {'):source.index('};',source.index('enum {'))+2]
    structs=source[source.index('typedef struct {'):source.index('static Present present;')+len('static Present present;')]
    function=extract(source,'NTSTATUS xsdk_present_open(')
    assert function.count('__asm__ volatile("sync" ::: "memory");')==1
    function=function.replace('__asm__ volatile("sync" ::: "memory");','fixture_barrier();')
    prefix='#include <assert.h>\n#include "gpu_private.h"\n#include "xsdk/display.h"\n#include "xsdk/splash.h"\n#include "xsdk/present.h"\n#include "xsdk/tube360_video.h"\nvoid fixture_barrier(void);\nstatic void on_interrupt(uint32_t s,void* p){(void)s;(void)p;}\nstatic void describe_display(uint32_t w,uint32_t h){(void)w;(void)h;}\n'
    (work/'present.c').write_text(prefix+enum+'\n'+structs+'\n'+function)
    run([*common,ROOT/'tests/reddit/present_open.c',work/'present.c','-Wl,--gc-sections','-o',work/'present'])
    for case in ('success','fail','letterbox-fail'):
        result=run([work/'present',case],capture_output=True,text=True);(a.evidence/('present-'+case+'.log')).write_text(result.stdout);print(result.stdout.splitlines()[-1],flush=True)
    # Liga setter, enumeração/capacidades, swapchain, present e display reais juntos.
    source=(v4x/'src/swapchain.c').read_text()
    functions='\n'.join(extract(source,sig) for sig in ('static VkResult check_create_info(', 'static void describe_image(', 'VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR('))
    (work/'swapchain.c').write_text('#include <assert.h>\n#include "host.h"\n#include "objects.h"\n#include "xsdk/present.h"\n#include "xsdk/log.h"\nstatic bool swapchain_exists;\nenum {FIFO_SWAP_INTERVAL=1};\n'+functions)
    run([*common,ROOT/'tests/reddit/sd_swapchain.c',v4x/'src/display.c',work/'swapchain.c',work/'present.c',work/'display.o','-Wl,--gc-sections','-o',work/'sd'])
    for case in ('safe-480i','safe-576i','safe-480p','safe-576i-wide','native-480i','native-576i','native-480p','safe-invalid'):
        result=run([work/'sd',case],capture_output=True,text=True);(a.evidence/(case+'.log')).write_text(result.stdout)
        assert 'begin display.kernel-viewport' in result.stdout and 'kernel=1' in result.stdout
        print(result.stdout.splitlines()[-1],flush=True)
    run([*common,'-D__XSDK__','-DBOARD_SIM_POLICY','-I'+str(ROOT/'client/src'),ROOT/'tests/reddit/sd_swapchain.c',ROOT/'client/src/video_policy.c',ROOT/'client/src/board_sim.c',v4x/'src/display.c',work/'swapchain.c',work/'present.c',work/'display.o','-Wl,--gc-sections','-o',work/'corona'])
    for driver in ('native','safe'):
        folder=a.evidence/('corona-480i-component-'+driver);folder.mkdir(exist_ok=True)
        config=f'board=corona\ndriver={driver}\nmode=480i\ncable=component\n'
        (folder/'board-sim.txt').write_text(config)
        # A simulação explícita tem precedência sobre um override conflitante.
        (folder/'video-driver.txt').write_text('safe' if driver=='native' else 'native')
        for signal in ('real-hd','real-sd'):
            result=run([work/'corona',driver+'-480i-'+signal],cwd=folder,capture_output=True,text=True)
            (folder/(signal+'-boot.log')).write_text(result.stdout)
            assert 'model=Corona;' in result.stdout and 'profile=corona' in result.stdout
            assert 'mode=480i cable=component simulated=1' in result.stdout
            assert f'selected={driver}' in result.stdout and 'surface=swapchain=present=640x480' in result.stdout
            assert ('dashboard=1920x1080 interlaced=0' if signal=='real-hd' else 'dashboard=640x480 interlaced=1') in result.stdout
            print('PASS Corona board-sim mode=480i cable=component driver='+driver+' query='+signal+' surface=swapchain=present=640x480 buffers=2 IRQ=1',flush=True)
    # Sem o hook sintético, o caso com query HD precisa falhar na seleção da saída.
    text=(sdk/'src/display.c').read_text();needle=' || (tube360_board_sim_sd && tube360_board_sim_sd())'
    assert text.count(needle)==1
    (work/'mutant.c').write_text(text.replace(needle,''))
    run([*common,'-Dclock_gettime=fixture_clock_gettime','-c',work/'mutant.c','-o',work/'mutant.o'])
    run([*common,'-D__XSDK__','-DBOARD_SIM_POLICY','-I'+str(ROOT/'client/src'),ROOT/'tests/reddit/sd_swapchain.c',ROOT/'client/src/video_policy.c',ROOT/'client/src/board_sim.c',v4x/'src/display.c',work/'swapchain.c',work/'present.c',work/'mutant.o','-Wl,--gc-sections','-o',work/'mutant'])
    for driver in ('native','safe'):
        folder=a.evidence/('corona-480i-component-'+driver)
        bad=subprocess.run([str(work/'mutant'),driver+'-480i'],cwd=folder,capture_output=True,text=True,timeout=10)
        (folder/'mutant.log').write_text(bad.stdout+bad.stderr)
        assert bad.returncode!=0 and 'tube360_set_output(1080)' in bad.stderr,bad.stderr
        print('PASS calibration: removing board-sim SD hook is detected in '+driver,flush=True)
