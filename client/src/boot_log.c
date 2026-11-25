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
