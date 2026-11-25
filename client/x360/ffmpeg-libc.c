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
#ifdef __XSDK__
#include <xsdk/audio.h>
NTSTATUS __real_xsdk_audio_next_frame(uint32_t timeout_ms,float** frame);
NTSTATUS __wrap_xsdk_audio_next_frame(uint32_t timeout_ms,float** frame) {
    NTSTATUS result=__real_xsdk_audio_next_frame(timeout_ms,frame);
    static int reported;
    if (!reported) {
        reported=1;
        int scheduling=0;struct sched_param priority={0};
        int query=pthread_getschedparam(pthread_self(),&scheduling,&priority);
        xsdk_log_write("app-audio: HW %u effective_priority %d query_status %d",
                       xsdk_thread_hardware_thread(),priority.sched_priority,query);
    }
    return result;
}
#endif

// O wrapper só distribui as threads criadas por avcodec_open2 no decodificador.
static atomic_uint owner;
static unsigned worker;
static int spread;
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
    if (active) { worker = 0; spread = active == 2; }
    atomic_store(&owner, active ? (unsigned)pthread_self() : 0);
}
int __real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
int __wrap_pthread_create(pthread_t* id, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
    int result = __real_pthread_create(id, attr, fn, arg);
    unsigned parent = atomic_load(&owner);
    if (!result && parent && parent == (unsigned)pthread_self()) {
        // Fontes menores conservam dois núcleos; 1080p30 usa os seis HW medidos na F39.
        static const unsigned hw[] = {2, 4, 3, 5};
        static const unsigned all_hw[] = {0, 2, 4, 1, 3, 5};
        unsigned core = spread ? all_hw[worker++ % 6] : hw[worker++ % 4];
        NTSTATUS status = xsdk_thread_place(xsdk_pthread_handle(*id), core);
        NTSTATUS priority = xsdk_thread_set_priority(xsdk_pthread_handle(*id),0,NULL);
        xsdk_log_write("decod-worker: %u HW %u, afinidade %08x prioridade 0 status %08x spread %d",
                       worker,core,(unsigned)status,(unsigned)priority,spread);
    }
    return result;
}
