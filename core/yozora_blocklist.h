#ifndef YOZORA_BLOCKLIST_H
#define YOZORA_BLOCKLIST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when the host is a listed domain or a subdomain of one. the host is canonical ASCII without port, case
   and one trailing dot are ignored. matched by host only */
int yozora_blocklist_matches(const char *host, size_t length);

/* the sorted entries, for tests and diagnostics */
size_t yozora_blocklist_count(void);
const char *yozora_blocklist_entry(size_t index);

#ifdef __cplusplus
}
#endif
#endif
