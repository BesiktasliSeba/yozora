#ifndef YOZORA_HTTPCACHE_H
#define YOZORA_HTTPCACHE_H

#include "yozora_types.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a disk cache of http responses: one file per url, written atomically, bounded in total size. one cache
   belongs to one thread, the caller serializes calls. the body is stored decoded and the header block holds
   "Name: value\r\n" lines without Set-Cookie and transfer headers */

#define YOZORA_HTTPCACHE_HEADERS_CAP 16384
#define YOZORA_HTTPCACHE_BODY_CAP (4 * 1024 * 1024)

typedef struct yozora_httpcache yozora_httpcache;

typedef struct {
    char url[YOZORA_URL_CAP];
    int status;
    /* when the response arrived, milliseconds since the epoch */
    int64_t response_ms;
    char *headers;
    size_t headers_length;
    uint8_t *body;
    size_t body_length;
} yozora_cache_entry;

/* directory must exist, belong to the caller and have mode 0700 */
yozora_status yozora_httpcache_open(yozora_httpcache **cache, const char *directory, uint64_t max_bytes);
void yozora_httpcache_close(yozora_httpcache *cache);
/* YOZORA_NOT_FOUND when nothing is stored, YOZORA_CORRUPT when the file was damaged and has been removed */
yozora_status yozora_httpcache_load(yozora_httpcache *cache, const char *url, yozora_cache_entry *entry);
yozora_status yozora_httpcache_store(yozora_httpcache *cache, const yozora_cache_entry *entry);
yozora_status yozora_httpcache_clear(yozora_httpcache *cache);
void yozora_cache_entry_release(yozora_cache_entry *entry);

typedef struct {
    int storable;
    /* seconds a response may be used without asking the server, already reduced by its Age */
    int64_t lifetime_s;
    int has_validator;
} yozora_cache_policy;

/* decides from a 200 response whether it is worth storing and for how long it stays fresh */
void yozora_cache_policy_parse(int status, const char *headers, size_t length, yozora_cache_policy *policy);

typedef enum {
    /* ordinary navigation and subresources */
    YOZORA_CACHE_NORMAL,
    /* back and forward: any stored copy is shown */
    YOZORA_CACHE_HISTORY,
    /* the caller asked for the network */
    YOZORA_CACHE_RELOAD
} yozora_cache_mode;

typedef enum {
    YOZORA_CACHE_USE_NETWORK,
    YOZORA_CACHE_USE_STORED,
    YOZORA_CACHE_USE_VALIDATE
} yozora_cache_decision;

yozora_cache_decision yozora_cache_decide(const yozora_cache_entry *entry, yozora_cache_mode mode, int64_t now_ms);

/* copies the value of a header from a stored block, 1 when found and it fits */
int yozora_cache_header(const char *headers, size_t length, const char *name, char *value, size_t capacity);
/* IMF-fixdate such as "Sun, 06 Nov 1994 08:49:37 GMT", 1 when valid */
int yozora_http_date(const char *text, size_t length, int64_t *epoch_ms);

/* bytes that may wait for the cache thread, the one response limit does not bound a queue of them. reserve
   runs on one thread and release on another */
typedef struct {
    size_t limit;
    size_t used;
} yozora_byte_budget;

void yozora_byte_budget_init(yozora_byte_budget *budget, size_t limit);
/* 1 and the bytes are counted, 0 when they would pass the limit */
int yozora_byte_budget_reserve(yozora_byte_budget *budget, size_t bytes);
void yozora_byte_budget_release(yozora_byte_budget *budget, size_t bytes);

#ifdef __cplusplus
}
#endif
#endif
