// O FFmpeg referencia exclusão de diretório e contabilidade POSIX. O kit só lê arquivos.
#include <errno.h>
#include <sys/times.h>
#include <unistd.h>

int rmdir(const char *path) {
    (void)path;
    errno = ENOSYS;
    return -1;
}
clock_t times(struct tms *buf) {
    (void)buf;
    errno = ENOSYS;
    return (clock_t)-1;
}

#include <xsdk/thread.h>
#include <stdatomic.h>

// O wrapper só distribui as threads criadas por avcodec_open2 no decodificador.
static atomic_uint owner;
static unsigned worker;
void ffmpeg_workers(int active) {
    if (active) worker = 0;
    atomic_store(&owner, active ? (unsigned)pthread_self() : 0);
}
int __real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
int __wrap_pthread_create(pthread_t* id, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
    int result = __real_pthread_create(id, attr, fn, arg);
    unsigned parent = atomic_load(&owner);
    if (!result && parent && parent == (unsigned)pthread_self()) {
        static const unsigned hw[] = {2, 4, 1, 3, 5, 0};
        xsdk_thread_place(xsdk_pthread_handle(*id), hw[worker++ % 6]);
    }
    return result;
}
