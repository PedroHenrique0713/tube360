#if !defined(__XSDK__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stddef.h>
#include <time.h>
#include <mbedtls/platform_time.h>
#include <mbedtls/entropy.h>
#ifdef __XSDK__
#include <xsdk/kernel.h>
#else
#include <errno.h>
#include <sys/random.h>
#endif

int mbedtls_hardware_poll(void *context, unsigned char *out, size_t len, size_t *written) {
    (void)context;
#ifdef __XSDK__
    XeCryptRandom(out, len);
    *written = len;
#else
    // A bancada usa a mesma configuração criptográfica; só troca a fonte de entropia do SO.
    *written = 0;
    while (*written < len) {
        ssize_t count = getrandom(out + *written, len - *written, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        *written += (size_t)count;
    }
#endif
    return 0;
}

mbedtls_ms_time_t mbedtls_ms_time(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (mbedtls_ms_time_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
