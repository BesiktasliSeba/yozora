#ifndef YOZORA_STORE_H
#define YOZORA_STORE_H

#include "yozora_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YOZORA_HISTORY_LIMIT 4096
#define YOZORA_BOOKMARK_LIMIT 1024
#define YOZORA_STORE_PAGE_LIMIT 64

typedef struct yozora_store yozora_store;

/* serialize calls per store on a worker thread; SQLite waits at most 250 ms for locks */
/* directory must be absolute, owned by this uid, mode 0700, with trusted parents */
yozora_status yozora_store_open(const char *directory, yozora_store **out);
/* private stores never open a database file */
yozora_status yozora_store_open_private(yozora_store **out);
/* close frees the handle only on success */
yozora_status yozora_store_close(yozora_store *store);
int yozora_store_sqlite_error(const yozora_store *store);

/* call once per committed top-level document, never for provisional loads */
yozora_status yozora_store_visit(yozora_store *store, const char *url, const char *title, int64_t time_ms);
yozora_status yozora_store_bookmark(yozora_store *store, const char *url, const char *title, int64_t time_ms);
yozora_status yozora_store_remove_bookmark(yozora_store *store, const char *url);
yozora_status yozora_store_remove_history(yozora_store *store, const char *url);
yozora_status yozora_store_clear_history(yozora_store *store);
/* pages are ordered by time descending, then URL ascending; output is empty on failure */
yozora_status yozora_store_history(yozora_store *store, size_t offset,
                                 yozora_record *records, size_t capacity, size_t *count);
yozora_status yozora_store_bookmarks(yozora_store *store, size_t offset,
                                   yozora_record *records, size_t capacity, size_t *count);

#ifdef __cplusplus
}
#endif
#endif
