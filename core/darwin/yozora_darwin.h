#ifndef YOZORA_DARWIN_H
#define YOZORA_DARWIN_H

#include "yozora_clock.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

yozora_clock_status yozora_darwin_wall_us(uint64_t *result);
yozora_clock_status yozora_darwin_uptime_ns(uint64_t *result);
yozora_clock_status yozora_darwin_continuous_ns(uint64_t *result);
yozora_clock_status yozora_darwin_thread_cpu_us(uint64_t *result);
yozora_clock_status yozora_darwin_memory_bytes(size_t *result);
yozora_clock_status yozora_darwin_kernel_page_size(size_t *result);
int yozora_darwin_random_bytes(void *buffer, size_t capacity);
void *yozora_darwin_aligned_alloc(size_t alignment, size_t size);
int yozora_darwin_open_temporary(char *path, size_t capacity);
int yozora_darwin_image_uuid(const void *address, uint8_t *uuid, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
