// Runtime exclusivo da bancada de demux. Não simula agendamento, kernel ou codecs.
#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <pthread.h>
#include <errno.h>

static char heap[64*1024*1024];
static unsigned cursor;
void* sbrk(int count) {
    if(count<0 || (unsigned)count>sizeof(heap)-cursor){errno=ENOMEM;return (void*)-1;}
    void* result=heap+cursor;cursor+=count;return result;
}
// O runner lê resultados diretamente pela QMP; stdio não acessa dispositivos do host.
int write(int fd,const void* data,unsigned count){return count;}
int read(int fd,void* data,unsigned count){return 0;}
int close(int fd){return -1;}
off_t lseek(int fd,off_t offset,int whence){return -1;}
int fstat(int fd,struct stat* info){info->st_mode=S_IFCHR;return 0;}
int isatty(int fd){return 1;}
int open(const char* path,int flags,...){return -1;}
int link(const char* from,const char* to){return -1;}
int mkdir(const char* path,mode_t mode){return -1;}
int rmdir(const char* path){return -1;}
int unlink(const char* path){return -1;}
int stat(const char* path,struct stat* info){return -1;}
int fcntl(int fd,int command,...){return -1;}
int kill(int pid,int signal){return -1;}
int getpid(void){return 1;}
void _exit(int code){for(;;)__asm__ volatile("nop");}

int pthread_setcancelstate(int state,int* old){if(old)*old=0;return 0;}
int pthread_mutex_init(pthread_mutex_t* mutex,const pthread_mutexattr_t* attr){return 0;}
int pthread_mutex_destroy(pthread_mutex_t* mutex){return 0;}
int pthread_mutex_lock(pthread_mutex_t* mutex){return 0;}
int pthread_mutex_unlock(pthread_mutex_t* mutex){return 0;}
int pthread_once(pthread_once_t* once,void(*callback)(void)) {
    if(!*(int*)once){*(int*)once=1;callback();}return 0;
}
// Nenhum worker é permitido: um caminho que passe a precisar de threads deve falhar.
int pthread_create(pthread_t* thread,const pthread_attr_t* attr,void*(*entry)(void*),void* arg){return ENOSYS;}
int pthread_join(pthread_t thread,void** result){return ENOSYS;}
int pthread_cond_init(pthread_cond_t* cond,const pthread_condattr_t* attr){return 0;}
int pthread_cond_destroy(pthread_cond_t* cond){return 0;}
int pthread_cond_wait(pthread_cond_t* cond,pthread_mutex_t* mutex){return ENOSYS;}
int pthread_cond_signal(pthread_cond_t* cond){return 0;}
int pthread_cond_broadcast(pthread_cond_t* cond){return 0;}
long sysconf(int name){return 1;}
int nanosleep(const struct timespec* delay,struct timespec* remaining){return 0;}
int gettimeofday(struct timeval* value,void* zone){value->tv_sec=1;value->tv_usec=0;return 0;}
int clock_gettime(clockid_t clock,struct timespec* value){value->tv_sec=1;value->tv_nsec=0;return 0;}
clock_t times(struct tms* buffer){return 0;}
void ffmpeg_video_work(void){}
