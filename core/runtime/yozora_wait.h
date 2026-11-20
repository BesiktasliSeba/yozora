#ifndef YOZORA_WAIT_H
#define YOZORA_WAIT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* zero timeout waits without a deadline, collisions may wake other addresses */
int yozora_wait_address(const void *address, const void *previous, size_t size, uint64_t timeout_ns);
int yozora_wake_address(const void *address);

#ifdef __cplusplus
}
#endif
#endif
