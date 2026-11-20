#ifndef YOZORA_NAVIGATION_H
#define YOZORA_NAVIGATION_H

#include "yozora_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YOZORA_NAVIGATION_ENTRIES 32
#define YOZORA_NAVIGATION_REDIRECTS 20

typedef struct yozora_navigation yozora_navigation;

typedef enum {
    YOZORA_NAV_IDLE,
    YOZORA_NAV_PROVISIONAL,
    YOZORA_NAV_COMMITTED,
    YOZORA_NAV_FINISHED,
    YOZORA_NAV_FAILED,
    YOZORA_NAV_CANCELLED
} yozora_navigation_phase;

typedef struct {
    uint64_t token;
    yozora_navigation_phase phase;
    yozora_status failure;
    uint32_t redirects;
    uint32_t entry_count;
    uint32_t current_index;
    int can_go_back;
    int can_go_forward;
    char pending_url[YOZORA_URL_CAP];
    yozora_record document;
} yozora_navigation_snapshot;

/* one owner thread serializes all calls, including core callbacks */
yozora_status yozora_navigation_create(yozora_navigation **out);
void yozora_navigation_destroy(yozora_navigation *navigation);
/* a successful begin invalidates the previous token; the caller cancels its core load */
yozora_status yozora_navigation_begin(yozora_navigation *navigation, const char *url, uint64_t *token);
yozora_status yozora_navigation_traverse(yozora_navigation *navigation, int direction, uint64_t *token);
yozora_status yozora_navigation_reload(yozora_navigation *navigation, uint64_t *token);
yozora_status yozora_navigation_redirect(yozora_navigation *navigation, uint64_t token, const char *url);
/* only an core document commit changes the back/forward list */
yozora_status yozora_navigation_commit(yozora_navigation *navigation, uint64_t token,
                                     const char *url, const char *title, int64_t time_ms);
yozora_status yozora_navigation_finish(yozora_navigation *navigation, uint64_t token);
yozora_status yozora_navigation_fail(yozora_navigation *navigation, uint64_t token, yozora_status reason);
yozora_status yozora_navigation_cancel(yozora_navigation *navigation, uint64_t token);
yozora_status yozora_navigation_read(const yozora_navigation *navigation, yozora_navigation_snapshot *out);

#ifdef __cplusplus
}
#endif
#endif
