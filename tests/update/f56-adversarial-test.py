#!/usr/bin/env python3
"""Regressões das reproduções adversariais de ec0dd7e, com batimento atual.

Compiles the real boot logger and xsdk terminate dispatcher, plus the real launcher.
All kernel I/O is replaced by local files; no console build or console access.
"""
from pathlib import Path
import importlib.util
import json
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'client/build'
SDK = ROOT / '.work/native-display/xsdk'
spec = importlib.util.spec_from_file_location('f56_receiver', ROOT / 'tools/error-reports/receiver.py')
receiver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(receiver)

NATIVE = r'''
#include "boot_log.h"
#include <xsdk/kernel.h>
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static char image[256], expected[256];
const char* const ExLoadedImageName=image;
static EX_TITLE_TERMINATE_REGISTRATION* registration;
static int in_callback, raw=-1, write_fail, registrations, removals;
uint64_t tube360_boot_test_time_base(void){return UINT64_C(49875000)*100;}
FILE* __real_fopen(const char*,const char*);
FILE* __wrap_fopen(const char* path,const char* mode) {
    assert(!in_callback);
    const char* slash=strrchr(path,'\\');
    return __real_fopen(slash?slash+1:path,mode);
}
void* __real_malloc(size_t);
void* __wrap_malloc(size_t n){assert(!in_callback);return __real_malloc(n);}
int __real_pthread_mutex_lock(pthread_mutex_t*);
int __wrap_pthread_mutex_lock(pthread_mutex_t* m){assert(!in_callback);return __real_pthread_mutex_lock(m);}
void tube360_boot_raw_close(void){}
void tube360_video_policy_init(void){}
void ExRegisterTitleTerminateNotification(EX_TITLE_TERMINATE_REGISTRATION* value,uint32_t create) {
    if(create){assert(!registration);registration=value;++registrations;}
    else {assert(registration==value);registration=NULL;++removals;}
}
NTSTATUS NtCreateFile(HANDLE* out,uint32_t access,const OBJECT_ATTRIBUTES* attr,IO_STATUS_BLOCK* io,
                     const uint64_t* alloc,uint32_t flags,uint32_t share,uint32_t disposition,uint32_t opts) {
    assert(in_callback && !attr->root_directory && !attr->attributes && !alloc && !share);
    assert(attr->name->length==strlen(expected) && attr->name->maximum_length==strlen(expected)+1);
    assert(!memcmp(attr->name->pointer,expected,attr->name->maximum_length));
    assert(access==(GENERIC_WRITE|SYNCHRONIZE) && flags==FILE_ATTRIBUTE_NORMAL);
    assert(disposition==FILE_OPEN && opts==(FILE_SYNCHRONOUS_IO_NONALERT|FILE_NON_DIRECTORY_FILE));
    raw=open("session.txt",O_WRONLY);assert(raw>=0);
    *out=77;io->status=0;return 0;
}
NTSTATUS NtWriteFile(HANDLE h,HANDLE ev,void* apc,void* context,IO_STATUS_BLOCK* io,
                     const void* bytes,uint32_t count,const uint64_t* offset) {
    assert(in_callback && h==77 && !ev && !apc && !context && offset);
    assert((*offset==5 && count==strlen("!terminated by system\n") && !memcmp(bytes,"!terminated by system\n",count)) ||
           (*offset==0 && count==6 && !memcmp(bytes,"clean\n",count)));
    if(write_fail){io->status=(NTSTATUS)0xc0000185;io->information=0;return io->status;}
    io->information=pwrite(raw,bytes,count,*offset);assert(io->information==count);io->status=0;return 0;
}
NTSTATUS NtFlushBuffersFile(HANDLE h,IO_STATUS_BLOCK* io){assert(in_callback && h==77);io->status=fsync(raw);return io->status;}
NTSTATUS NtClose(HANDLE h){assert(in_callback && h==77);assert(!close(raw));raw=-1;return 0;}
int main(int argc,char** argv) {
    assert(argc==3);
    snprintf(image,sizeof(image),"%stube360.xex",argv[1]);
    snprintf(expected,sizeof(expected),"%ssession.txt",argv[1]);
    boot_open("","app");boot_session_version(33,"0.3.3");
    // Same registration as main.cpp; exercise xsdk's real dispatch/unregister too.
    xsdk_on_terminate(boot_session_terminated);
    boot_event("end","app.first-frame","fixture reached Home");
    boot_event("begin","app.loop","fixture running");
    boot_session_heartbeat();
    const char* mode=argv[2];
    if(!strncmp(mode,"gpu-error-",10))
        boot_event("error","gpu.vulkan","2: vkWaitForFences(g.dev,1,&g.fences[g.fi],VK_TRUE,5000000000ULL)");
    if(!strcmp(mode,"already-clean") || !strcmp(mode,"already-clean-write-fail"))boot_session_clean();
    write_fail=!strcmp(mode,"already-clean-write-fail") || !strcmp(mode,"write-fail");
    if(!strcmp(mode,"during-write")) {
        // Model XAM's contract: discard the frozen title's unflushed stdio and close its handle.
        FILE* pending=__real_fopen("session.txt","w");assert(pending);
        fprintf(pending,"open\napp_started=1\n");
        assert(!close(fileno(pending)));
    }
    if(strcmp(mode,"gpu-error-without-home")) {
        // The dispatcher must not consult ExLoadedImageName or newlib during notification.
        memset(image,0,sizeof(image));
        assert(registration);in_callback=1;registration->notification_routine();in_callback=0;
        assert(!registration && registrations==1 && removals==1);
    }
    // _Exit avoids accidentally flushing the intentionally abandoned FILE above.
    _Exit(0);
}
'''

LAUNCHER = r'''
#include "update/launcher.hpp"
#include <iostream>
#include <iterator>
using namespace updates;
struct Host:LauncherHost {
    int posts=0,launches=0;string posted;
    void log(const string&)override{}
    bool screen()override{return true;}
    bool closeScreen()override{return true;}
    int launch(const string&)override{++launches;return -1;}
    bool randomBytes(unsigned char* b,size_t n)override{for(size_t i=0;i<n;++i)b[i]=i+1;return true;}
    yt::Response postReport(const yt::Request& r)override{++posts;posted=r.body;return {503,{},"offline fixture"};}
};
int main() {
    string input{std::istreambuf_iterator<char>(std::cin),{}};J request;if(!parse(input,request))return 2;
    auto op=text(request,"op");O out;
    if(op=="launch") {
        auto dir=text(request,"dir");Host h;auto before=reports::make(dir,string(32,'1'));
        out["report_before_launch"]=J(before);launchApp(dir,"",h);
        out["posts"]=J(double(h.posts));out["posted"]=J(h.posted);out["launches"]=J(double(h.launches));
    } else {
        auto error=text(request,"error"),log=text(request,"log");
        out["raw_category"]=J(reports::category(error,log));
        out["scrubbed_log"]=J(reports::scrub(log));
        out["scrubbed_category"]=J(reports::category(reports::scrub(error),reports::scrub(log)));
        O legacy{{"version",J(string("0.3.3"))},{"build",J(string("33"))},{"hardware",J(string("Corona"))},
                 {"screen",J(string("Player"))},{"error",J(error)},{"log",J(log)}};
        out["wire"]=J(reports::sanitize(J(legacy).serialize(),string(32,'1')));
    }
    std::cout<<J(out).serialize();
}
'''

LIFECYCLE = r'''
#include <xsdk/kernel.h>
#include <xsdk/terminate.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
extern void xsdk_terminate_close(void);
extern void xsdk_exit_launching(const char*);
extern void reviewed_sdk_exit(int) __attribute__((noreturn));
extern pid_t reviewed_sdk_kill(pid_t,int);
static EX_TITLE_TERMINATE_REGISTRATION* registration;
static jmp_buf finished;
static int registrations,removals,callbacks,closes;
static const char* mode;
void ExRegisterTitleTerminateNotification(EX_TITLE_TERMINATE_REGISTRATION* r,uint32_t create) {
    if(create){assert(!registration);registration=r;++registrations;}
    else {assert(registration==r);registration=NULL;++removals;}
}
static void callback(void){++callbacks;}
void xsdk_present_close(void){assert(!registration);++closes;}
void xsdk_display_close(void){assert(!registration);++closes;}
NTSTATUS xsdk_gpu_close(void){assert(!registration);++closes;return 0;}
void xsdk_log_write(const char* format,...){(void)format;}
void xsdk_log_close(void){assert(!registration);++closes;}
NTSTATUS xsdk_thread_sleep(uint32_t ms){(void)ms;assert(0);return 0;}
void XamLoaderLaunchTitle(const char* path,uint32_t flags) {
    assert(!registration && !callbacks && closes==4 && !flags);
    assert(!strcmp(path,"game:\\tube360.xex"));longjmp(finished,1);
}
void XamLoaderTerminateTitle(void) {
    // Model the kernel notification; xsdk's registration and removal code is real.
    if(registration)registration->notification_routine();
    assert(!registration);longjmp(finished,1);
}
// Host adapter for newlib's final call into xsdk's _exit; no host process exits here.
void reviewed_newlib_exit(int status){reviewed_sdk_exit(status);}
int main(int argc,char** argv) {
    assert(argc==2);mode=argv[1];
    if(!setjmp(finished)) {
        xsdk_on_terminate(callback);
        if(!strcmp(mode,"system") || !strcmp(mode,"direct-xam"))XamLoaderTerminateTitle();
        if(!strcmp(mode,"launch"))xsdk_exit_launching("game:\\tube360.xex");
        if(!strcmp(mode,"abort"))reviewed_sdk_kill(1,6);
        reviewed_newlib_exit(0);
    }
    assert(registrations==1 && removals==1 && !registration);
    int external=!strcmp(mode,"system") || !strcmp(mode,"direct-xam");
    assert(callbacks==external && closes==(external?0:4));
    xsdk_terminate_close();assert(removals==1);
    printf("PASS xsdk lifecycle %s: registered=%d removed=%d callback=%d\n",mode,registrations,removals,callbacks);
}
'''


class Adversarial(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='tube360-f56-adversarial-')
        cls.base = Path(cls.temporary.name)
        native = cls.base / 'native.c'
        native.write_text(NATIVE)
        cls.native = cls.base / 'native'
        subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-D__XSDK__',
                        '-DTUBE360_BOOT_TEST_TIME_BASE',
                        '-I' + str(ROOT / 'client/src'), '-I' + str(SDK / 'include'),
                        str(native), str(ROOT / 'client/src/boot_log.c'), str(SDK / 'src/terminate.c'),
                        '-pthread', '-Wl,--wrap=fopen', '-Wl,--wrap=malloc',
                        '-Wl,--wrap=pthread_mutex_lock', '-o', str(cls.native)], check=True)
        cpp = cls.base / 'launcher.cpp'
        cpp.write_text(LAUNCHER)
        cls.launcher = cls.base / 'launcher'
        libraries = []
        for name in ('libtube360-update-core.a', 'libtube360-boot.a', 'libmbedcrypto.a'):
            matches = list(BUILD.rglob(name))
            assert len(matches) == 1, (name, matches)
            libraries.append(str(matches[0]))
        subprocess.run(['c++', '-std=c++20', '-O2', '-I' + str(ROOT / 'client/src'),
                        '-I' + str(ROOT / 'client/third_party'), str(cpp),
                        str(ROOT / 'client/src/update/launcher.cpp'), '-Wl,--start-group',
                        *libraries, '-Wl,--end-group', '-pthread', '-o', str(cls.launcher)], check=True)
        lifecycle = cls.base / 'lifecycle.c'
        lifecycle.write_text(LIFECYCLE)
        cls.lifecycle = cls.base / 'lifecycle'
        subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                        '-D_exit=reviewed_sdk_exit', '-Dexit=reviewed_newlib_exit', '-Dkill=reviewed_sdk_kill',
                        '-I' + str(SDK / 'include'), str(lifecycle), str(SDK / 'src/terminate.c'),
                        str(SDK / 'src/exit.c'), '-o', str(cls.lifecycle)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def query(self, **request):
        result = subprocess.run([str(self.launcher)], input=json.dumps(request), capture_output=True, text=True, check=True)
        return json.loads(result.stdout)

    def terminate(self, mode, root='\\Device\\Mass0\\Apps with spaces\\Tube360\\'):
        folder = Path(tempfile.mkdtemp(dir=self.base, prefix=mode + '-'))
        (folder / 'tube360.xex').write_bytes(b'XEX2fixture')
        subprocess.run([str(self.native), root, mode], cwd=folder, check=True)
        marker = (folder / 'session.txt').read_text()
        result = self.query(op='launch', dir=str(folder) + '/')
        print(f'EVIDENCE {mode}: marker={marker!r} posts={result["posts"]}', flush=True)
        return marker, result

    def test_known_gpu_error_survives_system_exit(self):
        marker, result = self.terminate('gpu-error-home')
        self.assertEqual(result['posts'], 1, 'known vkWaitForFences failure erased by system termination')

    def test_control_gpu_error_without_system_exit_is_reported(self):
        marker, result = self.terminate('gpu-error-without-home')
        self.assertTrue(marker.startswith('open\n'))
        self.assertEqual(result['posts'], 1)
        self.assertEqual(json.loads(result['posted'])['category'], 'crash')

    def test_already_clean_survives_callback_io_failure(self):
        marker, result = self.terminate('already-clean-write-fail')
        self.assertEqual(marker, 'clean\n\n')
        self.assertEqual(result['posts'], 0, 'callback truncated a durable clean marker before failed write')

    def test_failed_write_preserves_open_marker(self):
        marker, result = self.terminate('write-fail')
        self.assertTrue(marker.startswith('open\n'))
        self.assertEqual(result['posts'], 1)

    def test_system_exit_hdd_usb_and_in_progress_writer(self):
        for mode, root in [('system', '\\Device\\Harddisk0\\Partition1\\Apps\\Tube360\\'),
                           ('already-clean', '\\Device\\Mass0\\Apps with spaces\\Tube360\\'),
                           ('during-write', '\\Device\\Mass0\\Apps with spaces\\Tube360\\')]:
            with self.subTest(mode=mode):
                marker, result = self.terminate(mode, root)
                if mode == 'already-clean':
                    self.assertEqual(marker, 'clean\n\n')
                else:
                    self.assertTrue(marker.startswith('clean\nterminated by system\n'))
                self.assertEqual(result['posts'], 0)

    def test_fence_information_survives_wire_sanitization(self):
        info = 'boot app #0001 +1ms begin frame.fence first-frame timeout=5000ms\n'
        result = self.query(op='classify', error='diagnostic', log=info)
        wire = json.loads(result['wire'])
        self.assertIn('frame.fence [video] timeout=', wire['log'])
        self.assertEqual(result['scrubbed_category'], 'other')
        self.assertEqual(wire['category'], 'other')
        observed = receiver.classify(wire)
        print(f'EVIDENCE info wire: {wire["log"]!r}; launcher={wire["category"]} receiver={observed}', flush=True)
        self.assertEqual(observed, ('other', 'other'))

    def test_legacy_scrubbed_fence_information(self):
        result = self.query(op='classify', error='diagnostic',
                            log='boot app #0001 +1ms begin frame.fence first-frame timeout=5000ms\n')
        legacy = json.loads(result['wire'])
        legacy.pop('category')
        legacy.pop('installation_id')
        observed = receiver.normalize(legacy)
        print(f'EVIDENCE legacy info: category={observed[0]["category"]} point={observed[-1]}', flush=True)
        self.assertEqual(observed[-1], 'other')

    def test_launcher_receiver_gpu_separator_parity(self):
        for error in ('gpu.fence failed', 'gpu fence failed', 'gpu_fence failed', 'gpu-fence failed'):
            with self.subTest(error=error):
                result = self.query(op='classify', error=error, log='')
                wire = json.loads(result['wire'])
                actual = receiver.classify(wire)[0]
                print(f'EVIDENCE separator {error!r}: launcher={wire["category"]} receiver={actual}', flush=True)
                self.assertEqual(wire['category'], actual)

    def test_xsdk_registration_is_removed_on_exit_launch_abort_and_notification(self):
        for mode in ('normal', 'launch', 'abort', 'system', 'direct-xam'):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.lifecycle), mode], capture_output=True, text=True, check=True)
                print(result.stdout.strip(), flush=True)

    def registration_mutant(self, replacement):
        mutant = Path(tempfile.mkdtemp(dir=self.base, prefix='missing-registration-'))
        source = mutant / 'client/src'
        source.mkdir(parents=True)
        for item in (ROOT / 'client/src').iterdir():
            if item.name == 'main.cpp':
                content = item.read_text()
                needle = 'xsdk_on_terminate(boot_session_terminated);'
                assert content.count(needle) == 1
                content = content.replace(needle, replacement)
                if not replacement:
                    anchor = '  boot_environment();'
                    assert content.count(anchor) == 1
                    content = content.replace(anchor, anchor + '\n  ' + needle)
                (source / item.name).write_text(content)
            else:
                (source / item.name).symlink_to(item)
        (mutant / 'client/third_party').symlink_to(ROOT / 'client/third_party')
        (mutant / 'client/build').symlink_to(BUILD)
        (mutant / '.work').symlink_to(ROOT / '.work')
        tests = mutant / 'tests/update'
        tests.mkdir(parents=True)
        for name in ('terminate-session.c', 'terminate-session-test.py'):
            shutil.copyfile(ROOT / 'tests/update' / name, tests / name)
        result = subprocess.run(['python3', str(tests / 'terminate-session-test.py')],
                                capture_output=True, text=True)
        print(f'EVIDENCE registration mutant: exit={result.returncode}; {result.stderr.strip()}', flush=True)
        self.assertNotEqual(result.returncode, 0, 'original suite remained green with main registration deleted')
        self.assertIn('FAIL main termination registration:', result.stderr)

    def test_original_suite_detects_deleted_registration(self):
        self.registration_mutant('/* mutation: no registration */')

    def test_original_suite_detects_commented_registration(self):
        self.registration_mutant('/* xsdk_on_terminate(boot_session_terminated); */')

    def test_original_suite_detects_late_registration(self):
        self.registration_mutant('')


if __name__ == '__main__':
    unittest.main(verbosity=2)
