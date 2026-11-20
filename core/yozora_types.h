#ifndef YOZORA_TYPES_H
#define YOZORA_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YOZORA_URL_CAP 4096
#define YOZORA_TITLE_CAP 512

typedef enum {
    YOZORA_OK = 0,
    YOZORA_INVALID,
    YOZORA_LIMIT,
    YOZORA_NOMEM,
    YOZORA_NOT_FOUND,
    YOZORA_STALE,
    YOZORA_STATE,
    YOZORA_BUSY,
    YOZORA_IO,
    YOZORA_STORAGE,
    YOZORA_CORRUPT,
    YOZORA_SCHEMA
} yozora_status;

typedef struct {
    char url[YOZORA_URL_CAP];
    char title[YOZORA_TITLE_CAP];
    int64_t time_ms;
    int64_t visits;
} yozora_record;

const char *yozora_status_string(yozora_status status);
/* text checks bound storage; WebKit owns URL parsing and origin decisions */
yozora_status yozora_text_check(const char *text, size_t capacity, int allow_empty);
yozora_status yozora_url_text_check(const char *url);

#ifdef __cplusplus
}
#endif
#endif
