// Callback real com kernel simulado; nenhum acesso ao console ou aos dispositivos nomeados.
#include "boot_log.h"
#include <xsdk/kernel.h>
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char image[2048],expected[2048];
const char* const ExLoadedImageName=image;
static int callback,raw=-1,create_fail,write_fail,io_write_fail,short_write;
static int prefix_fail,prefix_short,flush_fail;
static EX_TITLE_TERMINATE_REGISTRATION* registration;
static unsigned registrations,removals;
static unsigned creates,writes,flushes,closes;
static uint64_t ticks=UINT64_C(49875000)*100;
static unsigned clock_reads;
static int interrupt_heartbeat;
FILE* __real_fopen(const char*,const char*);
void* __real_malloc(size_t);
int __real_pthread_mutex_lock(pthread_mutex_t*);
int __real_clock_gettime(clockid_t,struct timespec*);
FILE* __wrap_fopen(const char* path,const char* mode) {
    assert(!callback);
    const char* slash=strrchr(path,'\\');
    return __real_fopen(slash?slash+1:path,mode);
}
void* __wrap_malloc(size_t size){assert(!callback);return __real_malloc(size);}
int __wrap_pthread_mutex_lock(pthread_mutex_t* lock){assert(!callback);return __real_pthread_mutex_lock(lock);}
int __wrap_clock_gettime(clockid_t clock,struct timespec* value){assert(!callback);return __real_clock_gettime(clock,value);}
uint64_t tube360_boot_test_time_base(void) {
    ++clock_reads;
    if(interrupt_heartbeat) {
        interrupt_heartbeat=0;
        callback=1;registration->notification_routine();callback=0;
        assert(!creates && !writes && !flushes && !closes);
    }
    return ticks;
}
void tube360_boot_raw_close(void){}
void tube360_video_policy_init(void){}
void ExRegisterTitleTerminateNotification(EX_TITLE_TERMINATE_REGISTRATION* value,uint32_t create) {
    if(create){assert(!registration);registration=value;++registrations;}
    else {assert(registration==value);registration=NULL;++removals;}
}
NTSTATUS NtCreateFile(HANDLE* out,uint32_t access,const OBJECT_ATTRIBUTES* attr,IO_STATUS_BLOCK* io,
                     const uint64_t* allocation,uint32_t flags,uint32_t share,uint32_t disposition,uint32_t options) {
    assert(callback);++creates;
    assert(!attr->root_directory && !attr->attributes && !allocation && !share);
    assert(!strcmp(attr->name->pointer,expected));
    assert(attr->name->length==strlen(expected) && attr->name->maximum_length==strlen(expected)+1);
    assert(access==(GENERIC_WRITE|SYNCHRONIZE) && flags==FILE_ATTRIBUTE_NORMAL && disposition==FILE_OPEN);
    assert(options==(FILE_SYNCHRONOUS_IO_NONALERT|FILE_NON_DIRECTORY_FILE));
    if(create_fail)return -1;
    raw=open("session.txt",O_WRONLY);assert(raw>=0);
    *out=77;io->status=0;return 0;
}
NTSTATUS NtWriteFile(HANDLE file,HANDLE event,void* apc,void* context,IO_STATUS_BLOCK* io,
                     const void* bytes,uint32_t length,const uint64_t* offset) {
    assert(callback && file==77 && !event && !apc && !context && offset);++writes;
    assert(*offset==(writes==1?5:0));
    const char* expected_bytes=writes==1?"!terminated by system\n":"clean\n";
    assert(length==strlen(expected_bytes) && !memcmp(bytes,expected_bytes,length));
    if(write_fail || io_write_fail || (prefix_fail && writes==2)) {
        io->status=-1;io->information=0;return io_write_fail?0:-1;
    }
    io->status=0;
    uint32_t count=short_write?3:writes==2 && prefix_short?(uint32_t)prefix_short:length;
    io->information=(uint32_t)pwrite(raw,bytes,count,*offset);
    assert(io->information==count);return 0;
}
NTSTATUS NtFlushBuffersFile(HANDLE file,IO_STATUS_BLOCK* io) {
    assert(callback && file==77);++flushes;
    if(flush_fail){io->status=-1;return -1;}
    io->status=fsync(raw);assert(!io->status);return 0;
}
NTSTATUS NtClose(HANDLE file){assert(callback && file==77);++closes;assert(!close(raw));raw=-1;return 0;}
static void* worker_checkpoint(void* unused) {
    (void)unused;boot_event("end","tls.first-request","worker still alive");return NULL;
}
int main(int argc,char** argv) {
    assert(argc==3);
    const char* mode=argv[2];
    snprintf(image,sizeof(image),"%stube360.xex",argv[1]);
    snprintf(expected,sizeof(expected),"%ssession.txt",argv[1]);
    boot_open("","app");boot_session_version(33,"0.3.3");
    xsdk_on_terminate(boot_session_terminated);
    boot_event("begin","app.loop","fixture running");
    if(!strcmp(mode,"crash") || !strcmp(mode,"hang"))_Exit(73);
    int no_heartbeat=!strcmp(mode,"no-heartbeat");
    int interrupted=!strcmp(mode,"interrupted-heartbeat");
    int failed=!strcmp(mode,"gpu-error") || !strcmp(mode,"startup-error") || !strcmp(mode,"error-no-log");
    if(!strcmp(mode,"error-no-log"))boot_close();
    if(failed) {
        boot_session_failed();
        boot_event("error",!strcmp(mode,"startup-error")?"startup.fail":"gpu.vulkan",
                         "2: vkWaitForFences(g.dev,1,&fence,VK_TRUE,5000000000ULL)");
    }
    int cleaned=!strncmp(mode,"already-clean",13);
    if(cleaned || !strcmp(mode,"clean-resume"))boot_session_clean();
    if(!strcmp(mode,"clean-resume"))boot_session_resume();
    int startup=!strncmp(mode,"startup-",8) && !failed;
    no_heartbeat|=startup;
    int stale=!strcmp(mode,"stale") || !strcmp(mode,"over-limit") || !strcmp(mode,"long-hang") ||
        !strcmp(mode,"no-heartbeat") || !strcmp(mode,"startup-over-limit") ||
        !strcmp(mode,"startup-info-only") || !strcmp(mode,"startup-worker-only") || !strcmp(mode,"running-log-only");
    if(!strcmp(mode,"retryable-network") || !strcmp(mode,"recoverable-output"))
        boot_event("error",!strcmp(mode,"retryable-network")?"tls.first-request":"settings.output","recovered failure");
    if(!strcmp(mode,"counter-wrap"))ticks=UINT64_MAX-UINT64_C(49875000);
    if(!strcmp(mode,"counter-zero"))ticks=0;
    unsigned setup_reads=clock_reads;
    if(interrupted)boot_session_heartbeat();
    interrupt_heartbeat=interrupted;
    if(!no_heartbeat)boot_session_heartbeat();
    if(!strcmp(mode,"stale"))ticks+=UINT64_C(49875000)*6;
    else if(!strcmp(mode,"at-limit"))ticks+=UINT64_C(49875000)*5;
    else if(!strcmp(mode,"over-limit"))ticks+=UINT64_C(49875000)*5+1;
    else if(!strcmp(mode,"long-hang"))ticks+=UINT64_C(49875000)*1000;
    else if(!strcmp(mode,"refreshed")) {
        ticks+=UINT64_C(49875000)*60;
        boot_session_heartbeat();
        ticks+=UINT64_C(49875000)/2;
    } else if(!strcmp(mode,"counter-wrap"))ticks+=UINT64_C(49875000)*2;
    else if(!strcmp(mode,"no-heartbeat") || startup) {
        if(!strcmp(mode,"startup-progress")) {
            for(unsigned i=0;i<4;++i) {
                ticks+=UINT64_C(49875000)*20;
                boot_event(i%2?"end":"begin",i<2?"gpu.init":"fonts.atlas","progress");
            }
        } else if(!strcmp(mode,"startup-recent"))ticks+=UINT64_C(49875000)/2;
        else {
            ticks+=UINT64_C(49875000)*30;
            if(strcmp(mode,"startup-at-limit"))++ticks;
        }
        if(!strcmp(mode,"startup-info-only"))boot_event("info","status","not progress");
        else if(!strcmp(mode,"startup-worker-only")) {
            pthread_t thread;assert(!pthread_create(&thread,NULL,worker_checkpoint,NULL));assert(!pthread_join(thread,NULL));
        }
    } else if(!strcmp(mode,"running-log-only")) {
        ticks+=UINT64_C(49875000)*6;
        boot_event("end","tls.first-request","not main loop progress");
    } else ticks+=UINT64_C(49875000)/2;
    create_fail=!strcmp(mode,"create-fail");write_fail=!strcmp(mode,"write-fail");
    if(!strcmp(mode,"already-clean-write-fail"))write_fail=1;
    io_write_fail=!strcmp(mode,"io-write-fail");short_write=!strcmp(mode,"short-write");
    prefix_fail=!strcmp(mode,"prefix-fail");flush_fail=!strcmp(mode,"flush-fail");
    sscanf(mode,"prefix-short-%d",&prefix_short);
    // A imagem e game: já não podem ser consultados durante a notificação.
    memset(image,0,sizeof(image));
    if(!interrupted){callback=1;registration->notification_routine();callback=0;}
    assert(!registration && registrations==1 && removals==1);
    int no_path=!strncmp(argv[1],"game:",5) || strlen(expected)>=1024;
    int unavailable=no_path || interrupted || stale || failed || cleaned;
    assert(creates==(unavailable?0:1));
    unsigned opened=!unavailable && !create_fail;
    unsigned prepared=opened && !write_fail && !io_write_fail && !short_write;
    unsigned published=prepared && !flush_fail;
    assert(closes==opened && writes==opened+published);
    assert(flushes==prepared+(published && !prefix_fail && !prefix_short));
    unsigned refreshed_startup=!strcmp(mode,"startup-progress")?4:0;
    assert(clock_reads-setup_reads==(no_heartbeat?refreshed_startup+!no_path:interrupted?2:(no_path || failed || cleaned)?1:!strcmp(mode,"refreshed")?3:2));
    printf("PASS raw termination %s mode=%s create=%u write=%u flush=%u close=%u clock=%u; no stdio/malloc/lock/POSIX clock\n",
           argv[1],mode,creates,writes,flushes,closes,clock_reads);
    return 0;
}
