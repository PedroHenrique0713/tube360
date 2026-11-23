#include <stddef.h>
#include <time.h>
#include <mbedtls/platform_time.h>
#include <xsdk/kernel.h>

int mbedtls_hardware_poll(void *context, unsigned char *out, size_t len, size_t *written) {
    (void)context;
    XeCryptRandom(out, len);
    *written = len;
    return 0;
}

mbedtls_ms_time_t mbedtls_ms_time(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (mbedtls_ms_time_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
