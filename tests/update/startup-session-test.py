#!/usr/bin/env python3
"""Reproduções F56d: main/SDL/Vulkan e HTTPS reais; só kernel de término e HTTP do app isolados.

Derivado de f56-final-review/reproduce.py. Nenhum console ou serviço externo é acessado.
"""
from pathlib import Path
import importlib.util, json, os, shlex, shutil, socket, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'client/build'
temporary=tempfile.TemporaryDirectory(prefix='tube360-f56d-product-')
OUT=Path(temporary.name)
SDK=ROOT/'.work/native-display/xsdk'
def readmodule(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
base=readmodule('adversarial',ROOT/'tests/update/f56-adversarial-test.py')
def replace_once(source, old, new):
 assert source.count(old)==1, ('fixture setup: expected one anchor',old)
 return source.replace(old,new)
native=base.NATIVE.split('int main(int argc,char** argv)')[0]
native=replace_once(native,'void tube360_video_policy_init(void){}','__attribute__((weak)) void tube360_video_policy_init(void){}')
native=replace_once(native,'uint64_t tube360_boot_test_time_base(void){return UINT64_C(49875000)*100;}',
    'static uint64_t ticks=UINT64_C(49875000)*100;\nuint64_t tube360_boot_test_time_base(void){return ticks;}')
native+='''
void review_prepare_image(void) {
 strcpy(image,"\\\\Device\\\\Mass0\\\\Apps\\\\Tube360\\\\tube360.xex");
 strcpy(expected,"\\\\Device\\\\Mass0\\\\Apps\\\\Tube360\\\\session.txt");
}
void review_system_exit(void) {
 assert(registration);in_callback=1;registration->notification_routine();in_callback=0;
 assert(!registration);
}
void review_advance(unsigned seconds){ticks+=UINT64_C(49875000)*seconds;}
'''
(OUT/'kernel.c').write_text(native)
inc=['-I'+str(ROOT/'client/src'),'-I'+str(ROOT/'client/third_party')]
sdkinc=['-I'+str(SDK/'include')]
def run(args,**kw):return subprocess.run([str(x) for x in args],check=True,**kw)
for source,target,extra in [(OUT/'kernel.c','kernel.o',[]),(ROOT/'client/src/boot_log.c','boot.o',['-DTUBE360_BOOT_TEST_TIME_BASE']),(SDK/'src/terminate.c','terminate.o',[])]:
 run(['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-D__XSDK__',*extra,*inc,*sdkinc,'-c',source,'-o',OUT/target])
objects=[str(OUT/name) for name in ['kernel.o','boot.o','terminate.o']]
wrap=['-Wl,--wrap=fopen','-Wl,--wrap=malloc','-Wl,--wrap=pthread_mutex_lock']
link=shlex.split((BUILD/'CMakeFiles/tube360.dir/link.txt').read_text())
libs=[str(BUILD/p) if not p.startswith('/') else p for p in link if p.endswith(('.a','.so')) or '.so.' in p]
launcher_source=OUT/'launcher.cpp';launcher_source.write_text(base.LAUNCHER)
run(['c++','-std=c++20','-O2',*inc,launcher_source,ROOT/'client/src/update/launcher.cpp','-Wl,--start-group',*libs,'-Wl,--end-group','-pthread','-o',OUT/'launcher'])
def launch(folder):
 result=run([OUT/'launcher'],input=json.dumps({'op':'launch','dir':str(folder)+'/'}),capture_output=True,text=True)
 data=json.loads(result.stdout)
 before=json.loads(data['report_before_launch']) if data['report_before_launch'] else None
 return {'posts':data['posts'],'category':before['category'] if before else None,'error':before['error'].strip() if before else None}

app_source=r'''
#include "boot_log.h"
#include "yt/client.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" void review_prepare_image();
extern "C" void review_system_exit();
extern "C" void review_advance(unsigned);
#include <vulkan/vulkan.h>
static const char* mode(){return std::getenv("F56D_MODE");}
extern "C" VkResult __real_vkWaitForFences(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t);
extern "C" VkResult __wrap_vkWaitForFences(VkDevice d,uint32_t n,const VkFence* f,VkBool32 all,uint64_t timeout) {
 if(!strcmp(mode(),"fatal-gpu"))return VK_ERROR_DEVICE_LOST;
 return __real_vkWaitForFences(d,n,f,all,timeout);
}
extern "C" void __real_boot_session_heartbeat();
extern "C" void __wrap_boot_session_heartbeat() {
 __real_boot_session_heartbeat();
 static unsigned beats=0;
 if(!strcmp(mode(),"main-heartbeat") && ++beats==3) {
  review_advance(1);review_system_exit();std::puts("PASS real main executed 3 heartbeats");std::fflush(stdout);std::_Exit(74);
 }
}
extern "C" void xsdk_on_terminate(void(*)());
extern "C" void __real_boot_open(const char*,const char*);
extern "C" void __wrap_boot_open(const char* dir,const char* role) {
 review_prepare_image();__real_boot_open(dir,role);xsdk_on_terminate(boot_session_terminated);
}
extern "C" void __real_boot_event(const char*,const char*,const char*,...);
extern "C" void __wrap_boot_event(const char* phase,const char* step,const char* format,...) {
 char detail[4096];va_list args;va_start(args,format);vsnprintf(detail,sizeof(detail),format,args);va_end(args);
 if(!strcmp(mode(),"fatal-no-log") && !strcmp(phase,"error"))boot_close();
 __real_boot_event(phase,step,"%s",detail);
 if(!strcmp(phase,"error") && !strcmp(step,"startup.fail")) {review_system_exit();std::_Exit(74);}
 if(!strcmp(phase,"begin") && !strcmp(step,"app.first-frame") && !strcmp(mode(),"main-heartbeat"))review_advance(31);
 if(!strcmp(phase,"begin") && !strcmp(step,"fonts.atlas") && strncmp(mode(),"fatal-",6) && strcmp(mode(),"main-heartbeat")) {
  if(!strcmp(mode(),"startup-at-limit"))review_advance(30);
  if(!strcmp(mode(),"startup-stalled"))review_advance(31);
  review_system_exit();std::_Exit(74);
 }
}
namespace yt {
class Offline:public Transport {public:Response request(const string&,const string&,bool)override{return {503,{},"offline fixture"};}};
extern "C" std::unique_ptr<Transport> __wrap__ZN2yt5httpsEPKSt6atomicIbE(const std::atomic_bool*){return std::make_unique<Offline>();}
}
'''
(OUT/'app.cpp').write_text(app_source)
run(['c++','-std=c++20','-O1',*inc,'-c',OUT/'app.cpp','-o',OUT/'app.o'])
run(['c++',BUILD/'CMakeFiles/tube360-main.dir/src/main.cpp.o',OUT/'app.o',*objects,*wrap,
 '-Wl,--wrap=boot_open','-Wl,--wrap=boot_event','-Wl,--wrap=boot_session_heartbeat','-Wl,--wrap=vkWaitForFences','-Wl,--wrap=_ZN2yt5httpsEPKSt6atomicIbE',
 '-Wl,--start-group',*libs,'-Wl,--end-group','-pthread','-lm','-o',OUT/'app'])
env={k:v for k,v in os.environ.items() if not k.startswith(('XT_','EXIT_SESSION_','F56D_'))}
env.update(SDL_VIDEO_DRIVER='offscreen',SDL_AUDIO_DRIVER='dummy',LP_NUM_THREADS='2',XT_CAST_DISABLE='1')
for mode,clean in [('startup-recent',True),('startup-at-limit',True),('startup-stalled',False),
                   ('main-heartbeat',True),('fatal-startup',False),('fatal-no-log',False),('fatal-gpu',False)]:
 folder=OUT/mode;folder.mkdir()
 for f in (ROOT/'client/assets').glob('*.ttf'):shutil.copyfile(f,folder/f.name)
 (folder/'settings.txt').write_text('sound=tv\n')
 case_env={**env,'HOME':str(folder),'XDG_CONFIG_HOME':str(folder),'XDG_CACHE_HOME':str(folder),
           'XT_ASSETS':str(folder),'F56D_MODE':mode}
 if mode in ('fatal-startup','fatal-no-log'):case_env['XT_STARTUP_ERROR']='sdl'
 r=subprocess.run([str(OUT/'app')],cwd=folder,env=case_env,capture_output=True,text=True,timeout=20)
 assert r.returncode==74,(mode,r.returncode,r.stdout,r.stderr)
 marker=(folder/'session.txt').read_text().splitlines()[0]
 result=launch(folder)
 assert marker==('clean' if clean else 'open'),(mode,marker)
 assert result['posts']==(0 if clean else 1),(mode,result)
 if mode=='main-heartbeat':assert 'PASS real main executed 3 heartbeats' in r.stdout,r.stdout
 if mode.startswith('fatal-') and mode!='fatal-no-log':assert 'error startup.fail' in (folder/'boot-app.log').read_text()
 print('PASS REAL APP',mode,'marker='+marker,result,flush=True)

network=r'''
#include "boot_log.h"
#include "yt/client.hpp"
#include <cassert>
#include <cstdio>
extern "C" void review_prepare_image();
extern "C" void review_system_exit();
extern "C" void review_advance(unsigned);
extern "C" void xsdk_on_terminate(void(*)());
int main(int argc,char**argv) {
 assert(argc==2);review_prepare_image();boot_open("","app");xsdk_on_terminate(boot_session_terminated);
 boot_event("end","app.first-frame","Home");boot_session_heartbeat();
 auto transport=yt::https();yt::Request req;req.url=argv[1];req.connectTimeoutMs=100;req.timeoutMs=300;
 auto response=transport->send(req);
 assert(response.retryable && !response.error.empty());
 printf("REAL HTTP result: status=%ld retryable=%d curl=%ld error=%s\n",response.status,response.retryable,response.transportCode,response.error.c_str());
 for(int i=0;i<600;i++){review_advance(1);boot_session_heartbeat();}
 review_system_exit();
}
'''
(OUT/'network.cpp').write_text(network)
run(['c++','-std=c++20','-O1',*inc,OUT/'network.cpp',*objects,*wrap,'-Wl,--start-group',*libs,'-Wl,--end-group','-pthread','-lm','-o',OUT/'network'])
folder=OUT/'exit-after-retryable-http';folder.mkdir(exist_ok=True)
with socket.socket() as bound:
 bound.bind(('127.0.0.1',0));url='https://127.0.0.1:'+str(bound.getsockname()[1])
 run([OUT/'network',url],cwd=folder,env={**env,'NO_PROXY':'*','no_proxy':'*'},timeout=5)
marker=(folder/'session.txt').read_text().splitlines()[0];result=launch(folder)
assert marker=='clean' and result['posts']==0,(marker,result)
assert 'curl=7' in (folder/'boot-app.log').read_text(), 'refused HTTPS must exercise the original recoverable error'
print('PASS RECOVERABLE HTTP + 600 healthy heartbeats: marker='+repr(marker),result,flush=True)

print('8 real app/HTTPS session scenarios passed; no console accessed.',flush=True)
