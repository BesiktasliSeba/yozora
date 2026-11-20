#ifndef YOZORA_DEBUG_H
#define YOZORA_DEBUG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* bytes the process moved over the network: the loader, downloads and the media proxy add what they read and
   write, the debug overlay shows the total. any thread */
void yozora_traffic_add(unsigned long long received, unsigned long long sent);
void yozora_traffic_read(unsigned long long *received, unsigned long long *sent);

/* the name of a phone from the system identifier, the identifier itself when the table does not know it. the
   simulator reports its host as "x86_64" or "arm64", which stays. out always ends in a zero byte */
void yozora_device_model_name(const char *identifier, char *out, size_t capacity);

/* a size for people: "512 B", "1.5 KB", "12.3 MB", "1.20 GB", at most 10 characters up to 16 EB. capacity is at least 12 */
void yozora_format_bytes(unsigned long long bytes, char *out, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
