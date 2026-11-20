#ifndef YOZORA_SITEGRANTS_H
#define YOZORA_SITEGRANTS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* what the person decided for each site (origin or host) in a text file: a header line naming kind and
   version, then "key<TAB>granted" or "key<TAB>denied". another header is not read, so a changed format makes
   sites ask again. at most `limit` sites, a new one past it replaces the oldest decision */

typedef enum {
    YOZORA_GRANT_NONE = 0,
    YOZORA_GRANT_ALLOW = 1,
    YOZORA_GRANT_DENY = 2
} yozora_grant;

#define YOZORA_GRANT_KEY_MAX 512

typedef struct yozora_sitegrants yozora_sitegrants;

/* header is copied, limit is at least 1. NULL when memory is out */
yozora_sitegrants *yozora_sitegrants_create(const char *header, size_t limit);
void yozora_sitegrants_destroy(yozora_sitegrants *grants);

yozora_grant yozora_sitegrants_get(const yozora_sitegrants *grants, const char *key);
/* 0 stored, -1 for a key that cannot be kept (empty, too long, a control character) or a grant of NONE, -2 when memory is out */
int yozora_sitegrants_set(yozora_sitegrants *grants, const char *key, yozora_grant grant);
/* 1 when the site was in the table */
int yozora_sitegrants_remove(yozora_sitegrants *grants, const char *key);
void yozora_sitegrants_clear(yozora_sitegrants *grants);

/* the sites in the order they were decided, oldest first */
size_t yozora_sitegrants_count(const yozora_sitegrants *grants);
const char *yozora_sitegrants_key_at(const yozora_sitegrants *grants, size_t index);
yozora_grant yozora_sitegrants_grant_at(const yozora_sitegrants *grants, size_t index);

/* replaces the table with the file. a missing file or another header leaves it empty and returns 0, -1 is a
   failed read or memory that ran out */
int yozora_sitegrants_load(yozora_sitegrants *grants, const char *path);
/* written beside the file and renamed over it, so a crash in the middle keeps the old table. 0 or -1 */
int yozora_sitegrants_save(const yozora_sitegrants *grants, const char *path);

#ifdef __cplusplus
}
#endif
#endif
