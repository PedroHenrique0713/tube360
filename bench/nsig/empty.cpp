// Linha de base do tamanho do XEX da bancada nsig: mesma estrutura de main, sem motor JS nem redutor.
#include <pthread.h>
#ifdef YT_XBOX
#include <xsdk/log.h>
#endif
static void* run(void*) {
#ifdef YT_XBOX
    xsdk_log_write("nsig-empty");
#endif
    return nullptr;
}
int main() {
    pthread_t th; pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, 1024 * 1024);
    if (pthread_create(&th, &attr, run, nullptr) != 0) return 1;
    pthread_join(th, nullptr);
    return 0;
}
