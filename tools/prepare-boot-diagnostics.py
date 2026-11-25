#!/usr/bin/env python3
"""Hooks fracos somente na cópia privada do xsdk: CRT e política de sockets."""
from pathlib import Path
import sys


def prepare(sdk):
    sdk = Path(sdk)
    stamp = sdk / 'tube360-boot-v1'
    if stamp.exists():
        prepare_raw(sdk)
        prepare_dbgprint(sdk)
        prepare_policy_report(sdk)
        return
    def replace(name, before, after):
        path = sdk / 'src' / name
        text = path.read_text()
        if text.count(before) != 1:
            raise RuntimeError(f'Incompatible SDK hook: {name}: {before[:70]!r}')
        path.write_text(text.replace(before, after))
    replace('log.c', 'NTSTATUS xsdk_log_open(void)\n{\n\tRtlInitializeCriticalSection(&log_lock);', '''void tube360_log_prepare(void)
{
    RtlInitializeCriticalSection(&log_lock);
}
NTSTATUS xsdk_log_open(void)
{
    tube360_log_prepare();''')
    declarations = '''
extern void tube360_log_prepare(void);
extern void tube360_boot_early(void) __attribute__((weak));
extern void boot_event(const char*,const char*,const char*,...) __attribute__((weak));
#define TUBE_BOOT(phase,step,text) do { if(boot_event)boot_event(phase,step,"%s",text); } while(0)
'''
    replace('start.c', 'int main(int argc, char **argv);', declarations + '\nint main(int argc, char **argv);')
    replace('start.c', '\txsdk_reent_init();\n\t(void)xsdk_log_open();\n\txsdk_system_set_time_zone();\n\t__register_frame_info(__eh_frame_start, eh_frame_object);', '''\txsdk_reent_init();
    tube360_log_prepare();
    if(tube360_boot_early)tube360_boot_early();
    TUBE_BOOT("begin","crt.sdk-log","default SDK log, optional dbgprint/net_log markers");
    (void)xsdk_log_open();
    TUBE_BOOT("end","crt.sdk-log","opened or optional fallback; boot log independent");
    TUBE_BOOT("begin","crt.timezone","XAM timezone query");
    xsdk_system_set_time_zone();
    TUBE_BOOT("end","crt.timezone","returned");
    TUBE_BOOT("begin","crt.unwind","register exception frames");
    __register_frame_info(__eh_frame_start, eh_frame_object);
    TUBE_BOOT("end","crt.unwind","registered");
    TUBE_BOOT("begin","crt.constructors","preinit and C++ static constructors");''')
    replace('start.c', '\t// Registered first, so it runs after the static destructors', '    TUBE_BOOT("end","crt.constructors","returned");\n\t// Registered first, so it runs after the static destructors')
    replace('start.c', '\texit(main(argc, arguments));', '    TUBE_BOOT("begin","crt.main","calling main");\n\texit(main(argc, arguments));')
    replace('socket.c', '#include "xsdk/thread.h"', '#include "xsdk/thread.h"\nextern void boot_event(const char*,const char*,const char*,...) __attribute__((weak));')
    replace('socket.c', 'static int network_error;', 'static int network_error;\nstatic unsigned policy_reported,policy_failed;')
    replace('socket.c', '\tconst int32_t started = NetDll_XNetStartup(XNCALLER_TITLE, params);', '''    if(boot_event)boot_event("begin","network.xnet","custom limits UDP=%u TCP=%u RX=%u TX=%u KiB",limits.datagram_sockets,limits.stream_sockets,limits.receive_kb,limits.send_kb);
    int32_t started = NetDll_XNetStartup(XNCALLER_TITLE, params);
    if(boot_event)boot_event(started?"error":"end","network.xnet","status=%d",(int)started);
    // WSAEINVAL / WSAEOPNOTSUPP: o ambiente pode recusar limites customizados.
    if(started==10022 || started==10045) {
        if(boot_event)boot_event("begin","network.xnet.default","custom limits unsupported; retrying documented defaults");
        started=NetDll_XNetStartup(XNCALLER_TITLE,NULL);
        if(boot_event)boot_event(started?"error":"end","network.xnet.default","status=%d",(int)started);
    }''')
    replace('socket.c', '\tconst int32_t error = NetDll_WSAStartup(XNCALLER_TITLE, WINSOCK_VERSION, data);', '''    if(boot_event)boot_event("begin","network.wsa","version=0202");
    const int32_t error = NetDll_WSAStartup(XNCALLER_TITLE, WINSOCK_VERSION, data);
    if(boot_event)boot_event(error?"error":"end","network.wsa","status=%d",(int)error);''')
    replace('socket.c', '''\t\txsdk_log_write("socket: marking it insecure failed: %d", (int)mark_error);
\t\t(void)NetDll_closesocket(XNCALLER_TITLE, handle);
\t\trelease_slot(slot);
\t\terrno = errno_from_winsock(mark_error);
\t\treturn -1;
\t}
\topen_slot(slot, handle, false);''', '''        // Apenas opção inexistente/não suportada: tenta socket padrão, sem mudar TLS.
        if(mark_error==10042 || mark_error==10045) {
            if(!__atomic_exchange_n(&policy_failed,1,__ATOMIC_RELAXED) && boot_event)
                boot_event("detect","network.socket-policy","option=5801 unsupported status=%d; trying standard socket; TLS unchanged",(int)mark_error);
        } else {
            if(!__atomic_exchange_n(&policy_failed,1,__ATOMIC_RELAXED) && boot_event)
                boot_event("error","network.socket-policy","option=5801 status=%d; permission errors not bypassed",(int)mark_error);
            xsdk_log_write("socket: marking it insecure failed: %d",(int)mark_error);
            (void)NetDll_closesocket(XNCALLER_TITLE,handle);
            release_slot(slot);errno=errno_from_winsock(mark_error);return -1;
        }
    } else if(!__atomic_exchange_n(&policy_reported,1,__ATOMIC_RELAXED) && boot_event)
        boot_event("detect","network.socket-policy","option=5801 accepted; no Xbox Live login; TLS unchanged");
    open_slot(slot, handle, false);''')
    stamp.write_text('durable boot hooks; unsupported socket options tolerate standard sockets, not access denial\n')
    prepare_raw(sdk)
    prepare_dbgprint(sdk)
    prepare_policy_report(sdk)


def prepare_policy_report(sdk):
    stamp=sdk/'tube360-boot-v4'
    if stamp.exists():return
    path=sdk/'src/start.c';text=path.read_text()
    needle='extern void tube360_boot_early(void) __attribute__((weak));'
    assert text.count(needle)==1
    text=text.replace(needle,needle+'\nextern void tube360_video_policy_normal_log(void) __attribute__((weak));')
    needle='    TUBE_BOOT("end","crt.sdk-log","opened or optional fallback; boot log independent");'
    assert text.count(needle)==1
    text=text.replace(needle,needle+'\n    if(tube360_video_policy_normal_log)tube360_video_policy_normal_log();')
    path.write_text(text);stamp.write_text('board/path summary after SDK file/UDP log setup, before constructors\n')


def prepare_dbgprint(sdk):
    stamp=sdk/'tube360-boot-v3'
    if stamp.exists():return
    path=sdk/'src/log.c';text=path.read_text()
    needle='static bool log_dbgprint;'
    assert text.count(needle)==1
    text=text.replace(needle,needle+'''
typedef NTSTATUS (*TubeDbgPrint)(const char*,...);
static TubeDbgPrint tube_dbgprint;
extern void boot_event(const char*,const char*,const char*,...) __attribute__((weak));
static void tube_find_dbgprint(void) {
    HANDLE module=0;void* address=NULL;
    if(!XexGetModuleHandle("xboxkrnl.exe",&module) &&
       NT_SUCCESS(XexGetProcedureAddress(module,0x003,&address)))tube_dbgprint=(TubeDbgPrint)address;
}
''')
    needle='if (log_dbgprint)\n\t\t(void)DbgPrint("%s", line);'
    assert text.count(needle)==1
    text=text.replace(needle,'if (log_dbgprint && tube_dbgprint)\n\t\t(void)tube_dbgprint("%s", line);')
    needle='\tchar net_note[NET_NOTE_BYTES];'
    assert text.count(needle)==1
    text=text.replace(needle,'''    if(log_dbgprint)tube_find_dbgprint();
    if(boot_event)boot_event("detect","environment.dbgprint","requested=%d export_present=%d optional=1; no XBDM dependency",log_dbgprint,tube_dbgprint!=NULL);
'''+needle)
    needle='bool xsdk_log_set_dbgprint(bool on)\n{'
    assert text.count(needle)==1
    text=text.replace(needle,needle+'\n    if(on && !tube_dbgprint)tube_find_dbgprint();')
    path.write_text(text);stamp.write_text('DbgPrint dynamically resolved only on opt-in; not a required XEX import\n')


def prepare_raw(sdk):
    stamp=sdk/'tube360-boot-v2'
    if stamp.exists():return
    path=sdk/'src/start.c';text=path.read_text()
    needle='extern void tube360_boot_early(void) __attribute__((weak));'
    assert text.count(needle)==1
    text=text.replace(needle,needle+"\nextern void tube360_boot_raw(const char*,const char*) __attribute__((weak));\nextern void tube360_boot_raw_close(void) __attribute__((weak));\n#define RAW_BOOT(phase,step) do { if(tube360_boot_raw)tube360_boot_raw(phase,step); } while(0)")
    for call,step in [('xsdk_lock_init();','crt.locks'),('xsdk_microcode_init();','crt.microcode'),('xsdk_pthread_init();','crt.pthread'),('xsdk_reent_init();','crt.newlib')]:
        assert text.count(call)==1
        text=text.replace(call,'RAW_BOOT("begin","'+step+'");\n    '+call+'\n    RAW_BOOT("end","'+step+'");')
    text=text.replace('\txsdk_lock_init();','\txsdk_lock_init();')
    text=text.replace('void xsdk_start(void)\n{','void xsdk_start(void)\n{\n    RAW_BOOT("begin","crt.entry");')
    text=text.replace('    tube360_log_prepare();','    RAW_BOOT("end","crt.runtime-ready");\n    if(tube360_boot_raw_close)tube360_boot_raw_close();\n    tube360_log_prepare();')
    path.write_text(text);stamp.write_text('raw kernel writes + NtFlushBuffersFile before SDK locks/newlib\n')


def prepare_v4x(root):
    root=Path(root);stamp=root/'tube360-boot-v1'
    if stamp.exists():
        prepare_timeout(root)
        return
    path=root/'src/swapchain.c';text=path.read_text()
    needle='#include "xsdk/present.h"'
    assert text.count(needle)==1
    text=text.replace(needle,needle+'\nextern void boot_event(const char*,const char*,const char*,...) __attribute__((weak));')
    needle='\tconst uint64_t ms = (timeout_ns + NANOSECONDS_PER_MILLISECOND - 1) / NANOSECONDS_PER_MILLISECOND;'
    assert text.count(needle)==1
    text=text.replace(needle,'\tif (timeout_ns == UINT64_MAX) return LONGEST_WAIT_MS;\n'+needle)
    needle='\tfor (uint32_t i = 0; i < object->image_count; i++) {\n\t\tif (object->images[i].address == address)'
    assert text.count(needle)==1
    text=text.replace(needle,'''    if (!NT_SUCCESS(status)) {
        if(boot_event)boot_event("error","gpu.acquire-native","status=%08x; no image acquired",(unsigned)status);
        return VK_ERROR_DEVICE_LOST;
    }
    bool found=false;
\tfor (uint32_t i = 0; i < object->image_count; i++) {
\t\tif (object->images[i].address == address && (found=true))''')
    needle='\tobject->acquired = true;'
    assert text.count(needle)==1
    text=text.replace(needle,'\tif (!found) return VK_ERROR_OUT_OF_DATE_KHR;\n'+needle)
    path.write_text(text);stamp.write_text('bounded timeout conversion; native present failures cannot return a valid image\n')
    prepare_timeout(root)


def prepare_timeout(root):
    stamp=root/'tube360-boot-v2'
    if stamp.exists():return
    path=root/'src/swapchain.c';text=path.read_text()
    needle='if (timeout_ns == UINT64_MAX) return LONGEST_WAIT_MS;'
    assert text.count(needle)==1
    text=text.replace(needle,'if (timeout_ns >= (uint64_t)LONGEST_WAIT_MS * NANOSECONDS_PER_MILLISECOND) return LONGEST_WAIT_MS;')
    path.write_text(text);stamp.write_text('saturate before rounding, including near UINT64_MAX\n')


if __name__ == '__main__':
    prepare(sys.argv[1])
    if len(sys.argv)>2:prepare_v4x(sys.argv[2])
