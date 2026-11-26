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
static int callback,raw=-1,create_fail,write_fail;
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
        callback=1;boot_session_terminated();callback=0;
        assert(!creates && !writes && !flushes && !closes);
    }
    return ticks;
}
void tube360_boot_raw_close(void){}
void tube360_video_policy_init(void){}
NTSTATUS NtCreateFile(HANDLE* out,uint32_t access,const OBJECT_ATTRIBUTES* attr,IO_STATUS_BLOCK* io,
                     const uint64_t* allocation,uint32_t flags,uint32_t share,uint32_t disposition,uint32_t options) {
    assert(callback);++creates;
    assert(!attr->root_directory && !attr->attributes && !allocation && !share);
    assert(!strcmp(attr->name->pointer,expected));
    assert(attr->name->length==strlen(expected) && attr->name->maximum_length==strlen(expected)+1);
    assert(access==(GENERIC_WRITE|SYNCHRONIZE) && flags==FILE_ATTRIBUTE_NORMAL && disposition==FILE_OVERWRITE_IF);
    assert(options==(FILE_SYNCHRONOUS_IO_NONALERT|FILE_NON_DIRECTORY_FILE));
    if(create_fail)return -1;
    raw=open("session.txt",O_WRONLY|O_CREAT|O_TRUNC,0600);assert(raw>=0);
    *out=77;io->status=0;return 0;
}
NTSTATUS NtWriteFile(HANDLE file,HANDLE event,void* apc,void* context,IO_STATUS_BLOCK* io,
                     const void* bytes,uint32_t length,const uint64_t* offset) {
    assert(callback && file==77 && !event && !apc && !context && !offset);++writes;
    assert(length==sizeof("clean\nterminated by system\n")-1);
    assert(!memcmp(bytes,"clean\nterminated by system\n",length));
    if(write_fail)return -1;
    io->information=(uint32_t)write(raw,bytes,length);assert(io->information==length);return 0;
}
NTSTATUS NtFlushBuffersFile(HANDLE file,IO_STATUS_BLOCK* io) {
    assert(callback && file==77);++flushes;io->status=fsync(raw);assert(!io->status);return 0;
}
NTSTATUS NtClose(HANDLE file){assert(callback && file==77);++closes;assert(!close(raw));raw=-1;return 0;}
int main(int argc,char** argv) {
    assert(argc==3);
    const char* mode=argv[2];
    snprintf(image,sizeof(image),"%stube360.xex",argv[1]);
    snprintf(expected,sizeof(expected),"%ssession.txt",argv[1]);
    boot_open("","app");boot_session_version(33,"0.3.3");
    boot_event("begin","app.loop","fixture running");
    if(!strcmp(mode,"crash") || !strcmp(mode,"hang"))_Exit(73);
    int no_heartbeat=!strcmp(mode,"no-heartbeat");
    int interrupted=!strcmp(mode,"interrupted-heartbeat");
    int stale=!strcmp(mode,"stale") || !strcmp(mode,"over-limit") || !strcmp(mode,"long-hang");
    if(!strcmp(mode,"counter-wrap"))ticks=UINT64_MAX-UINT64_C(49875000);
    if(!strcmp(mode,"counter-zero"))ticks=0;
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
    else ticks+=UINT64_C(49875000)/2;
    create_fail=!strcmp(mode,"create-fail");write_fail=!strcmp(mode,"write-fail");
    // A imagem e game: já não podem ser consultados durante a notificação.
    memset(image,0,sizeof(image));
    if(!interrupted){callback=1;boot_session_terminated();callback=0;}
    int no_path=!strncmp(argv[1],"game:",5) || strlen(expected)>=1024;
    int unavailable=no_path || no_heartbeat || interrupted || stale;
    assert(creates==(unavailable?0:1));
    assert(writes==(!unavailable && !create_fail?1:0) && flushes==writes && closes==writes);
    assert(clock_reads==(no_heartbeat?0:interrupted?2:no_path?1:!strcmp(mode,"refreshed")?3:2));
    printf("PASS raw termination %s mode=%s create=%u write=%u flush=%u close=%u clock=%u; no stdio/malloc/lock/POSIX clock\n",
           argv[1],mode,creates,writes,flushes,closes,clock_reads);
    return 0;
}
