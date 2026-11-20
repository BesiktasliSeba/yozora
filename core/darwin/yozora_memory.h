#ifndef YOZORA_MEMORY_H
#define YOZORA_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define YOZORA_MEMORY_MAX_SIZE UINT64_C(67108864)

typedef enum {
    YOZORA_MEMORY_OK = 0,
    YOZORA_MEMORY_INVALID,
    YOZORA_MEMORY_LIMIT,
    YOZORA_MEMORY_ACCESS,
    YOZORA_MEMORY_SYSTEM
} yozora_memory_status;

#ifdef __cplusplus
extern "C" {
#endif

yozora_memory_status yozora_memory_round_size(uint64_t size, uint64_t page_size, uint64_t *rounded);

#if defined(__APPLE__)
yozora_memory_status yozora_memory_allocate(uint64_t size, void **data, uint32_t *port, int32_t *kernel_error);
yozora_memory_status yozora_memory_map(uint32_t port, uint64_t size, int writable, int copy_on_write, void **data, int32_t *kernel_error);
yozora_memory_status yozora_memory_export(uint32_t parent, uint64_t size, int writable, uint32_t *port, int32_t *kernel_error);
yozora_memory_status yozora_memory_share(void *data, uint64_t size, int writable, uint32_t *port, int32_t *kernel_error);
yozora_memory_status yozora_memory_deallocate(void *data, uint64_t size, int32_t *kernel_error);
#endif

#ifdef __cplusplus
}
#endif
#endif
