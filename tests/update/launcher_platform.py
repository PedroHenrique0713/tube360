#!/usr/bin/env python3
"""Executa o main do Xbox com falhas de SDK simuladas; nao simula GPU nem XAM reais."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1]).resolve()
header = r'''
#pragma once
#include <cstdint>
#include <cstddef>
using NTSTATUS=int32_t;
using HANDLE=uint32_t;
#define NT_SUCCESS(s) ((s)>=0)
inline constexpr int XSDK_DISPLAY_720P=0;
inline constexpr int INPUT_GAMEPAD_B=0x2000;
struct INPUT_GAMEPAD {uint16_t buttons;};
struct INPUT_STATE {INPUT_GAMEPAD gamepad;};
uint32_t XamInputGetState(uint32_t,uint32_t,INPUT_STATE*);
extern const char* ExLoadedImageName;
NTSTATUS xsdk_gpu_open(); NTSTATUS xsdk_gpu_close();
NTSTATUS xsdk_present_open(int,uint32_t,uint32_t,uint32_t*,uint32_t*);
NTSTATUS xsdk_present_begin(uint32_t*,uint32_t);
void xsdk_present_end(); void xsdk_present_close();
NTSTATUS xsdk_text_open(); void xsdk_text_close();
void xsdk_text_measure(const char*,uint32_t,uint32_t*,uint32_t*);
void xsdk_text_draw(int32_t,int32_t,uint32_t,uint32_t,const char*);
void xsdk_gpu_resolve(uint32_t,uint32_t,uint32_t,uint32_t);
void xsdk_gpu_set_target(uint32_t,uint32_t);
uint32_t xsdk_gpu_fence(); NTSTATUS xsdk_gpu_wait(uint32_t,uint32_t);
void xsdk_log_write(const char*,...);
using XsdkThreadEntry=uint32_t(*)(void*);
NTSTATUS xsdk_thread_create_on(HANDLE*,uint32_t,XsdkThreadEntry,void*,uint32_t);
NTSTATUS xsdk_thread_sleep(uint32_t);
void XeCryptRandom(void*,uint32_t);
void XamLoaderTerminateTitle(); int xsdk_launch(const char*,const void*,size_t);
'''
fake = r'''
#include "xsdk/gpu.h"
#include "yt/client.hpp"
#include <ctime>
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace yt {
struct ReportTransport:Transport {
    Response request(const string&,const string&,bool) override {return {};}
    Response send(const Request& r) override {
        assert(r.url=="https://tube360.hypermind.space/api/report" && r.agent=="Tube360/1.0");
        assert(r.timeoutMs==1500 && r.connectTimeoutMs==1000 && r.maxBytes==1024);
        const char* mode=std::getenv("SDK_REPORT");
        if(mode && !std::strcmp(mode,"dns-hang")){timespec delay{10,0};nanosleep(&delay,nullptr);return {};}
        FILE* f=std::fopen("posted.json","w");assert(f);std::fputs(r.body.c_str(),f);std::fclose(f);
        return {200,"{\"code\":\"T360-7K2Q\"}",{}};
    }
};
std::unique_ptr<Transport> https(const std::atomic_bool*) {return std::make_unique<ReportTransport>();}
}
void XeCryptRandom(void* data,uint32_t n){auto* bytes=static_cast<unsigned char*>(data);for(uint32_t i=0;i<n;++i)bytes[i]=i+1;}
const char* ExLoadedImageName="game:\\default.xex";
static bool gpu=false,present=false,text=false;
static XsdkThreadEntry deadline=nullptr;
static void* deadlineData=nullptr;
static unsigned polls=0;
static uint32_t slept=0;
static bool mode(const char* s) {const char* m=std::getenv("SDK_FAIL");return m && !std::strcmp(m,s);}
static void mark(const char* file,const char* value) {FILE* f=std::fopen(file,"a");assert(f);std::fprintf(f,"%s\n",value);std::fclose(f);}
void xsdk_log_write(const char* f,...) {char line[2048];va_list a;va_start(a,f);std::vsnprintf(line,sizeof(line),f,a);va_end(a);mark("sdk.log",line);}
NTSTATUS xsdk_gpu_open(){gpu=!mode("gpu");return gpu?0:-1;}
NTSTATUS xsdk_gpu_close(){gpu=false;return mode("close")?-1:0;}
NTSTATUS xsdk_present_open(int,uint32_t,uint32_t,uint32_t* w,uint32_t* h){assert(gpu);*w=1280;*h=720;present=!mode("present");return present?0:-1;}
NTSTATUS xsdk_present_begin(uint32_t* a,uint32_t ms){assert(present && (ms==1000 || ms==100));*a=32;return mode("begin")?-1:0;}
void xsdk_present_end(){assert(present && text);}
void xsdk_present_close(){present=false;}
NTSTATUS xsdk_text_open(){text=!mode("text");return text?0:-1;}
void xsdk_text_close(){text=false;}
void xsdk_text_measure(const char*,uint32_t,uint32_t* w,uint32_t* h){*w=360;*h=40;}
void xsdk_text_draw(int32_t x,int32_t y,uint32_t,uint32_t,const char* s){assert(x>=0 && y>=0 && text);mark("draw.txt",s);}
void xsdk_gpu_resolve(uint32_t,uint32_t,uint32_t,uint32_t){assert(gpu && present);}
void xsdk_gpu_set_target(uint32_t,uint32_t){assert(gpu);}
uint32_t xsdk_gpu_fence(){return 1;}
NTSTATUS xsdk_gpu_wait(uint32_t,uint32_t ms){assert((ms==1000 || ms==100));return mode("wait")?-1:0;}
NTSTATUS xsdk_thread_create_on(HANDLE* h,uint32_t,XsdkThreadEntry fn,void* data,uint32_t hw){assert(!gpu && !present && !text && hw==5);*h=1;deadline=fn;deadlineData=data;return mode("thread")?-1:0;}
uint32_t XamInputGetState(uint32_t user,uint32_t,INPUT_STATE* state){if(user)return 1;if(!polls && deadline && !mode("hang"))deadline(deadlineData);state->gamepad.buttons=++polls>1?INPUT_GAMEPAD_B:0;return 0;}
NTSTATUS xsdk_thread_sleep(uint32_t ms){slept=ms;return 0;}
void XamLoaderTerminateTitle(){assert(!gpu && !present && !text && slept==20000);mark("terminated.txt","deadline 20000");std::_Exit(124);}
int xsdk_launch(const char* path,const void*,size_t){
    assert(!gpu && !present && !text && deadline);
    assert(!std::strcmp(path,"game:\\tube360.xex"));
    mark("launched.txt",path);
    if(mode("hang"))deadline(deadlineData);
    errno=ENOEXEC;return -1;
}
'''

with tempfile.TemporaryDirectory(prefix='tube360-launcher-sdk-') as temp:
    base = Path(temp)
    include = base / 'xsdk'
    include.mkdir()
    for name in ('display', 'gpu', 'log', 'present', 'text', 'thread', 'xam'):
        (include / (name + '.h')).write_text('#include "sim.h"\n')
    (include / 'sim.h').write_text(header)
    (base / 'fake.cpp').write_text(fake)
    link_file=build / 'CMakeFiles/yt-launcher-test.dir/link.txt'
    if not link_file.exists():link_file=build / 'CMakeFiles/tube360.dir/link.txt'
    link = shlex.split(link_file.read_text())
    libs = [str(build / x) for x in link if x.endswith('.a')]
    binary = base / 'launcher'
    subprocess.run([link[0], '-std=c++17', '-D__XSDK__', '-I' + str(base), '-I' + str(root / 'client/src'),
                    '-I' + str(root / 'client/third_party'), str(root / 'client/src/update/launcher_x360.cpp'),
                    str(root / 'client/src/update/launcher.cpp'), str(base / 'fake.cpp'), *libs,
                    '-o', str(binary)], check=True)
    cases = [('pt', '', 'abrindo'), ('en', '', 'opening'), ('es', '', 'abriendo')]
    cases += [('pt', fail, '') for fail in ('gpu', 'present', 'text', 'begin', 'wait', 'close', 'thread', 'hang')]
    for index, (lang, fail, label) in enumerate(cases):
        folder = base / str(index)
        folder.mkdir()
        (folder / 'settings.txt').write_text('lang=' + lang + '\n')
        (folder / 'tube360.xex').write_bytes(b'XEX2simulated')
        for weight in ('300','400','600','700'):
            (folder / f'inter-{weight}.ttf').write_bytes(b'\0\1\0\0')
        (folder / 'default.log').write_text('app sentinel')
        result = subprocess.run([str(binary)], cwd=folder, env={**os.environ, 'SDK_FAIL': fail}, timeout=4)
        assert result.returncode == 1, (fail, result.returncode)
        log = (folder / 'launcher.log').read_text()
        sdk = (folder / 'sdk.log').read_text()
        assert all(line in sdk for line in log.splitlines()), 'all launcher events must reach SDK network logger'
        assert (folder/'startup.log').exists(), 'failure remains visible and persists until B'
        assert (folder / 'default.log').read_text() == 'app sentinel'
        if not fail or fail == 'hang':
            assert (folder / 'launched.txt').read_text().strip() == r'game:\tube360.xex'
        else:
            assert not (folder / 'launched.txt').exists(), fail
        if label:
            assert label in (folder / 'draw.txt').read_text()
        if fail == 'hang':
            assert not (folder/'terminated.txt').exists()
            assert '20 segundos' in (folder/'draw.txt').read_text()
            assert 'launch retorno=' not in log
        elif not fail:
            assert 'launch retorno=-1' in log
        print('PASS launcher Xbox main:', lang, fail or 'launch error', flush=True)
    for mode in ('receipt','dns-hang','opt-out'):
        folder=base/('report-'+mode);folder.mkdir()
        (folder/'tube360.xex').write_bytes(b'XEX2simulated')
        (folder/'session.txt').write_text('open\n')
        (folder/'boot-app.log').write_text('boot app error gpu.fence status=dead\n')
        if mode=='opt-out':(folder/'no-error-reports.txt').touch()
        for weight in ('300','400','600','700'):(folder/f'inter-{weight}.ttf').write_bytes(b'\0\1\0\0')
        began=time.monotonic()
        result=subprocess.run([str(binary)],cwd=folder,env={**os.environ,'SDK_FAIL':'','SDK_REPORT':mode},timeout=6)
        elapsed=time.monotonic()-began
        assert result.returncode==1 and (folder/'launched.txt').exists(),mode
        if mode=='receipt':
            assert 'T360-7K2Q' in (folder/'draw.txt').read_text()
            assert not (folder/'error-report.json').exists()
        elif mode=='dns-hang':
            assert 1.4<elapsed<3 and (folder/'error-report.json').exists(),elapsed
            assert not (folder/'posted.json').exists()
        else:
            assert not (folder/'posted.json').exists()
        print('PASS launcher report:',mode,f'{elapsed:.2f}s to handoff',flush=True)
print('14 SDK simulations passed; physical drawing, timing, controller and UDP delivery remain unverified.')
