#include "yozora_clock.h"

#include <stddef.h>

yozora_clock_status yozora_clock_ticks_to_ns(uint64_t ticks, uint32_t numer,
                                         uint32_t denom, uint64_t *nanoseconds)
{
    uint64_t whole;
    uint64_t fraction;
    if (!nanoseconds)
        return YOZORA_CLOCK_INVALID;
    *nanoseconds = 0;
    if (!numer || !denom)
        return YOZORA_CLOCK_INVALID;
    whole = ticks / denom;
    fraction = (ticks % denom) * numer / denom;
    if (whole > (UINT64_MAX - fraction) / numer)
        return YOZORA_CLOCK_OVERFLOW;
    *nanoseconds = whole * numer + fraction;
    return YOZORA_CLOCK_OK;
}

yozora_clock_status yozora_clock_timeval_us(int64_t seconds, int64_t microseconds,
                                        uint64_t *result)
{
    if (!result)
        return YOZORA_CLOCK_INVALID;
    *result = 0;
    if (seconds < 0 || microseconds < 0 || microseconds >= 1000000)
        return YOZORA_CLOCK_INVALID;
    if ((uint64_t)seconds > (UINT64_MAX - (uint64_t)microseconds) / 1000000)
        return YOZORA_CLOCK_OVERFLOW;
    *result = (uint64_t)seconds * 1000000 + (uint64_t)microseconds;
    return YOZORA_CLOCK_OK;
}

yozora_clock_status yozora_clock_elapsed_us(uint64_t wall, uint64_t boot_before,
                                        uint64_t boot_after, uint64_t *result)
{
    if (!result)
        return YOZORA_CLOCK_INVALID;
    *result = 0;
    /* xnu adjusts kern.boottime when the calendar clock changes */
    if (boot_before != boot_after)
        return YOZORA_CLOCK_RETRY;
    if (wall < boot_before)
        return YOZORA_CLOCK_INVALID;
    *result = wall - boot_before;
    return YOZORA_CLOCK_OK;
}
