/* F30 experiment; changes to FFmpeg remain LGPL-2.1-or-later. */
#ifndef F30_STATS_H
#define F30_STATS_H
#include <stdint.h>
#define F30_MAGIC UINT32_C(0x46333031)
typedef struct F30Stats {
    uint32_t magic, parsed, reconstructed, slices;
    uint64_t syntax_hash, slice_ns, nnz_hash, coeff_hash, prediction_hash;
} F30Stats;
#endif
