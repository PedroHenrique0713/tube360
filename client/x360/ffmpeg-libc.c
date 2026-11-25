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
#include <xsdk/log.h>
#include <stdatomic.h>

// O wrapper só distribui as threads criadas por avcodec_open2 no decodificador.
static atomic_uint owner;
static unsigned worker;
static atomic_uint video_us, video_frames;
void ffmpeg_video_work(unsigned us, int frame) {
    atomic_fetch_add(&video_us, us);
    if (frame) atomic_fetch_add(&video_frames, 1);
}
void ffmpeg_work_metrics(unsigned* us, unsigned* frames) {
    *us = atomic_exchange(&video_us, 0);
    *frames = atomic_exchange(&video_frames, 0);
}
void ffmpeg_workers(int active) {
    if (active) worker = 0;
    atomic_store(&owner, active ? (unsigned)pthread_self() : 0);
}
int __real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
int __wrap_pthread_create(pthread_t* id, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
    int result = __real_pthread_create(id, attr, fn, arg);
    unsigned parent = atomic_load(&owner);
    if (!result && parent && parent == (unsigned)pthread_self()) {
        // Núcleo 0 (HW 0/1) reservado ao desenho, demux/AAC e SDL; H.264 nos núcleos 1 e 2.
        static const unsigned hw[] = {2, 4, 3, 5};
        unsigned core = hw[worker++ % 4];
        NTSTATUS status = xsdk_thread_place(xsdk_pthread_handle(*id), core);
        int scheduling = 0; struct sched_param priority = {0};
        int query = pthread_getschedparam(*id, &scheduling, &priority);
        xsdk_log_write("decod-worker: %u HW %u, afinidade %08x prioridade %d consulta %d",worker,core,(unsigned)status,query ? -999 : priority.sched_priority,query);
    }
    return result;
}
