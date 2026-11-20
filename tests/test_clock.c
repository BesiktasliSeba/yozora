#include "yozora_clock.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    uint64_t value = 42;
    assert(yozora_clock_ticks_to_ns(1, 125, 3, &value) == YOZORA_CLOCK_OK && value == 41);
    assert(yozora_clock_ticks_to_ns(UINT64_MAX, 1, 1, &value) == YOZORA_CLOCK_OK && value == UINT64_MAX);
    assert(yozora_clock_ticks_to_ns(UINT64_MAX, 125, 3, &value) == YOZORA_CLOCK_OVERFLOW && !value);
    assert(yozora_clock_ticks_to_ns(UINT64_MAX, UINT32_MAX, UINT32_MAX, &value) == YOZORA_CLOCK_OK && value == UINT64_MAX);
    assert(yozora_clock_ticks_to_ns(1, 0, 1, &value) == YOZORA_CLOCK_INVALID && !value);
    assert(yozora_clock_ticks_to_ns(1, 1, 0, &value) == YOZORA_CLOCK_INVALID && !value);
    assert(yozora_clock_ticks_to_ns(1, 1, 1, NULL) == YOZORA_CLOCK_INVALID);
    assert(yozora_clock_timeval_us(1, 999999, &value) == YOZORA_CLOCK_OK && value == 1999999);
    assert(yozora_clock_timeval_us(-1, 0, &value) == YOZORA_CLOCK_INVALID && !value);
    assert(yozora_clock_timeval_us(0, -1, &value) == YOZORA_CLOCK_INVALID && !value);
    assert(yozora_clock_timeval_us(0, 1000000, &value) == YOZORA_CLOCK_INVALID && !value);
    assert(yozora_clock_timeval_us(INT64_MAX, 0, &value) == YOZORA_CLOCK_OVERFLOW && !value);
    assert(yozora_clock_elapsed_us(150, 100, 100, &value) == YOZORA_CLOCK_OK && value == 50);
    assert(yozora_clock_elapsed_us(150, 100, 101, &value) == YOZORA_CLOCK_RETRY && !value);
    assert(yozora_clock_elapsed_us(99, 100, 100, &value) == YOZORA_CLOCK_INVALID && !value);
    assert(yozora_clock_elapsed_us(150, 100, 100, NULL) == YOZORA_CLOCK_INVALID);
    /* a whole-second calendar step moves both wall and boot time */
    assert(yozora_clock_elapsed_us(3600000150ULL, 3600000100ULL, 3600000100ULL, &value) == YOZORA_CLOCK_OK && value == 50);
    assert(yozora_clock_elapsed_us(500, 100, 100, &value) == YOZORA_CLOCK_OK && value == 400);
    puts("clock: checked tick conversion, timeval bounds, calendar steps and suspend elapsed time passed");
    return 0;
}
