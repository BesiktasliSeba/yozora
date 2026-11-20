#ifndef YOZORA_CLOCK_H
#define YOZORA_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YOZORA_CLOCK_OK = 0,
    YOZORA_CLOCK_INVALID,
    YOZORA_CLOCK_OVERFLOW,
    YOZORA_CLOCK_RETRY,
    YOZORA_CLOCK_SYSTEM
} yozora_clock_status;

yozora_clock_status yozora_clock_ticks_to_ns(uint64_t ticks, uint32_t numer,
                                         uint32_t denom, uint64_t *nanoseconds);
yozora_clock_status yozora_clock_timeval_us(int64_t seconds, int64_t microseconds,
                                        uint64_t *result);
yozora_clock_status yozora_clock_elapsed_us(uint64_t wall, uint64_t boot_before,
                                        uint64_t boot_after, uint64_t *result);

#ifdef __cplusplus
}
#endif
#endif
