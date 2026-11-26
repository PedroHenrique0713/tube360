#include "boot_log.h"
#include "video_policy.h"
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __XSDK__
#include <xsdk/kernel.h>
#endif

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static FILE* file;
static char path[1024],component[24];
static unsigned sequence;
static uint64_t began;
static int sync_warned;
static char session_path[1024];
static volatile uint32_t session_failed,session_cleaned;
#ifdef __XSDK__
static char terminate_path[1024];
static ANSI_STRING terminate_name;
static const OBJECT_ATTRIBUTES terminate_attributes={.root_directory=0,.name=&terminate_name};
static const char terminate_marker[]="clean\nterminated by system\n";
static const char terminate_pending[]="!terminated by system\n";
static const uint64_t terminate_offset=0;
static const uint64_t terminate_pending_offset=5;
static IO_STATUS_BLOCK terminate_io;
static HANDLE terminate_file;
static volatile uint64_t session_heartbeat;
static volatile uint32_t session_heartbeat_ready;
static uint64_t session_time_base(void) {
#if defined(__PPC__) || defined(__powerpc__)
    uint32_t upper,lower,check;
    do {
        __asm__ volatile("mftbu %0" : "=r"(upper));
        __asm__ volatile("mftb %0" : "=r"(lower));
        __asm__ volatile("mftbu %0" : "=r"(check));
    } while(upper!=check);
    return (uint64_t)upper<<32 | lower;
#elif defined(TUBE360_BOOT_TEST_TIME_BASE)
    extern uint64_t tube360_boot_test_time_base(void);
    return tube360_boot_test_time_base();
#else
    return 0;
#endif
}
static void session_prepare_terminate(void) {
    terminate_name.length=0;
    session_heartbeat_ready=0;
    const char* image=ExLoadedImageName?ExLoadedImageName:"";
    const char* slash=strrchr(image,'\\');
    if(strncmp(image,"\\Device\\",8) || !slash)return;
    size_t directory=(size_t)(slash-image+1);
    if(directory+sizeof("session.txt")>sizeof(terminate_path))return;
    memcpy(terminate_path,image,directory);
    memcpy(terminate_path+directory,"session.txt",sizeof("session.txt"));
    terminate_name.pointer=terminate_path;
    terminate_name.maximum_length=(uint16_t)(directory+sizeof("session.txt"));
    terminate_name.length=terminate_name.maximum_length-1;
}
#endif
void boot_session_heartbeat(void) {
#if defined(__XSDK__) && (defined(__PPC__) || defined(__powerpc__) || defined(TUBE360_BOOT_TEST_TIME_BASE))
    // Em PPC32 o callback pode congelar o título entre as duas metades da gravação de 64 bits.
    __atomic_store_n(&session_heartbeat_ready,0,__ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    session_heartbeat=session_time_base();
    __atomic_store_n(&session_heartbeat_ready,1,__ATOMIC_RELEASE);
#endif
}
void boot_session_terminated(void) {
#ifdef __XSDK__
    // Threads congeladas, handles fechados e game: removido: só kernel e dados preparados antes do registro.
    if(__atomic_load_n(&session_failed,__ATOMIC_ACQUIRE) || __atomic_load_n(&session_cleaned,__ATOMIC_ACQUIRE) ||
       !terminate_name.length || !__atomic_load_n(&session_heartbeat_ready,__ATOMIC_ACQUIRE))return;
    // Mesma time base de 49,875 MHz do xsdk: 5 s toleram quadros lentos sem ocultar laço parado.
    if(session_time_base()-session_heartbeat>UINT64_C(49875000)*5)return;
    NTSTATUS status=NtCreateFile(&terminate_file,GENERIC_WRITE|SYNCHRONIZE,&terminate_attributes,&terminate_io,NULL,
        FILE_ATTRIBUTE_NORMAL,0,FILE_OPEN,FILE_SYNCHRONOUS_IO_NONALERT|FILE_NON_DIRECTORY_FILE);
    if(!NT_SUCCESS(status))return;
    // Publica clean\n por último; o '!' impede que até 5 bytes do prefixo pareçam uma sessão limpa.
    status=NtWriteFile(terminate_file,0,NULL,NULL,&terminate_io,terminate_pending,sizeof(terminate_pending)-1,&terminate_pending_offset);
    if(NT_SUCCESS(status) && NT_SUCCESS(terminate_io.status) && terminate_io.information==sizeof(terminate_pending)-1) {
        status=NtFlushBuffersFile(terminate_file,&terminate_io);
        if(NT_SUCCESS(status) && NT_SUCCESS(terminate_io.status)) {
            status=NtWriteFile(terminate_file,0,NULL,NULL,&terminate_io,terminate_marker,6,&terminate_offset);
            if(NT_SUCCESS(status) && NT_SUCCESS(terminate_io.status) && terminate_io.information==6)
                NtFlushBuffersFile(terminate_file,&terminate_io);
        }
    }
    NtClose(terminate_file);
#endif
}
static int session_write(const char* state,const char* metadata) {
    if(!session_path[0])return 0;
    FILE* marker=fopen(session_path,"w");
    if(!marker)return 0;
    int ok=fprintf(marker,"%s\n%s\n",state,metadata?metadata:"")>=0;
    if(fflush(marker))ok=0;
    if(fsync(fileno(marker)))ok=0;
    if(fclose(marker))ok=0;
    return ok;
}
void boot_session_begin(const char* directory,const char* metadata) {
    __atomic_store_n(&session_failed,0,__ATOMIC_RELEASE);
    __atomic_store_n(&session_cleaned,0,__ATOMIC_RELEASE);
    snprintf(session_path,sizeof(session_path),"%ssession.txt",directory?directory:"");
    char detail[2048];int used=snprintf(detail,sizeof(detail),"%s\n",metadata?metadata:"");
    const char* names[]={"boot-app.log","ba.log","boot-early.log","be.log","startup.log","tube360.log","default.log"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]) && used>0 && used<(int)sizeof(detail);++i) {
        char log[1024];snprintf(log,sizeof(log),"%s%s",directory?directory:"",names[i]);
        FILE* previous=fopen(log,"rb");long offset=0;
        if(previous){if(!fseek(previous,0,SEEK_END))offset=ftell(previous);fclose(previous);}
        used+=snprintf(detail+used,sizeof(detail)-(size_t)used,"%s=%ld\n",names[i],offset<0?0:offset);
    }
    session_write("open",detail);
}
void boot_session_clean(void) {
    if(session_write("clean",""))__atomic_store_n(&session_cleaned,1,__ATOMIC_RELEASE);
}
void boot_session_resume(void) {
    __atomic_store_n(&session_cleaned,0,__ATOMIC_RELEASE);
    session_write("open","power down returned unexpectedly");
}
void boot_session_version(unsigned build,const char* version) {
    if(!session_path[0])return;
    FILE* marker=fopen(session_path,"a");
    if(marker){fprintf(marker,"app_started=1\nversion=%s build=%u\n",version,build);fflush(marker);fsync(fileno(marker));fclose(marker);}
}
static uint64_t millis(void) {
    struct timespec t={0};clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static void sync_line(void) {
    int error=fflush(file)?errno:0;
    if(!error && fsync(fileno(file)))error=errno;
    if(error && !sync_warned) {
        sync_warned=1;
        fprintf(file,"boot %s durability=failed errno=%d; userspace flush only; no power-loss guarantee\n",component,error);
        fflush(file);
    }
}
void boot_event(const char* phase,const char* step,const char* format,...) {
    // Inclui startup.fail/tela de erro, mesmo sem log ou antes de conseguir a trava.
    if(!strcmp(phase,"error"))__atomic_store_n(&session_failed,1,__ATOMIC_RELEASE);
    int saved_errno=errno;
    pthread_mutex_lock(&lock);
    if(file) {
        char detail[1536];va_list args;va_start(args,format);
        vsnprintf(detail,sizeof(detail),format,args);va_end(args);
        for(char* c=detail;*c;++c)if(*c=='\n' || *c=='\r' || *c=='\t')*c=' ';
        fprintf(file,"boot %s #%04u +%llums %s %s %s\n",component,++sequence,
                (unsigned long long)(millis()-began),phase,step,detail);
        sync_line();
    }
    pthread_mutex_unlock(&lock);
    errno=saved_errno;
}
void boot_open(const char* directory,const char* role) {
    int saved_errno=errno;
    pthread_mutex_lock(&lock);
    if(file){sync_line();fclose(file);file=NULL;}
    char folder[1024];snprintf(folder,sizeof(folder),"%s",directory?directory:"");
#ifdef __XSDK__
    if(!folder[0]) {
        const char* image=ExLoadedImageName?ExLoadedImageName:"";
        const char* slash=strrchr(image,'\\');
        if(slash && (size_t)(slash-image+1)<sizeof(folder)) {
            memcpy(folder,image,(size_t)(slash-image+1));folder[slash-image+1]=0;
        } else snprintf(folder,sizeof(folder),"game:\\");
    }
#endif
    size_t n=strlen(folder);
    if(n && folder[n-1]!='/' && folder[n-1]!='\\' && n+1<sizeof(folder)){folder[n++]='/';folder[n]=0;}
    snprintf(component,sizeof(component),"%s",role?role:"app");
    int short_name=0;
    int length=snprintf(path,sizeof(path),"%sboot-%s.log",folder,component);
#ifdef __XSDK__
    // O resolvedor POSIX do xsdk limita o caminho a 260 bytes; nunca troca USB por HDD.
    if(length>=260){short_name=1;length=snprintf(path,sizeof(path),"%sb%c.log",folder,component[0]);}
#endif
    file=length>=0 && length<(int)sizeof(path)?fopen(path,"a"):NULL;
    int error=file?0:errno;
    sequence=0;began=millis();sync_warned=0;
    if(file)setvbuf(file,NULL,_IONBF,0);
    if(!strcmp(component,"app")) {
        __atomic_store_n(&session_failed,0,__ATOMIC_RELEASE);
        __atomic_store_n(&session_cleaned,0,__ATOMIC_RELEASE);
#ifdef __XSDK__
        session_prepare_terminate();
#endif
        // O launcher já abriu o marcador antes do handoff; abrir o app diretamente também é coberto.
        snprintf(session_path,sizeof(session_path),"%ssession.txt",folder);
        FILE* marker=fopen(session_path,"r");char state[8]={0};
        if(marker){fgets(state,sizeof(state),marker);fclose(marker);}
        if(strcmp(state,"open\n"))boot_session_begin(folder,"direct app start; version in boot/main logs");
    }
    pthread_mutex_unlock(&lock);
    if(file)boot_event("begin","entry","path=%s short_name=%d sync=fflush+fsync; exploit=unknown",path,short_name);
    else fprintf(stderr,"boot log unavailable role=%s errno=%d (no fallback to another drive)\n",component,error);
    errno=saved_errno;
}
void boot_close(void) {
    int saved_errno=errno;
    pthread_mutex_lock(&lock);
    if(file){sync_line();fclose(file);file=NULL;}
    pthread_mutex_unlock(&lock);
    errno=saved_errno;
}
const char* boot_log_path(void){return path;}
void tube360_boot_early(void) {
    // Referência forte: hooks fracos do CRT não extraem boot_early.o do arquivo estático.
    extern void tube360_boot_raw_close(void);
    tube360_boot_raw_close();
    boot_open("","early");
    tube360_video_policy_init();
#ifdef __XSDK__
    boot_event("info","crt.image","image=%s",ExLoadedImageName?ExLoadedImageName:"unknown");
#endif
}
