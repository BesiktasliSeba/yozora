#include "yozora_navigation.h"

#include <stdlib.h>
#include <string.h>

struct yozora_navigation {
    uint64_t token;
    yozora_navigation_phase phase;
    yozora_status failure;
    uint32_t redirects;
    uint32_t count;
    uint32_t current;
    uint32_t target;
    int replace;
    char pending_url[YOZORA_URL_CAP];
    yozora_record *entries[YOZORA_NAVIGATION_ENTRIES];
};

yozora_status yozora_navigation_create(yozora_navigation **out) {
    if (!out) return YOZORA_INVALID;
    *out = calloc(1, sizeof **out);
    return *out ? YOZORA_OK : YOZORA_NOMEM;
}

void yozora_navigation_destroy(yozora_navigation *navigation) {
    uint32_t i;
    if (!navigation) return;
    for (i = 0; i < navigation->count; ++i) free(navigation->entries[i]);
    free(navigation);
}

static yozora_status begin(yozora_navigation *navigation, const char *url,
                          int replace, uint32_t target, uint64_t *token) {
    yozora_status status;
    if (token) *token = 0;
    if (!navigation || !token) return YOZORA_INVALID;
    status = yozora_url_text_check(url);
    if (status != YOZORA_OK) return status;
    if (navigation->token == UINT64_MAX) return YOZORA_LIMIT;
    ++navigation->token;
    navigation->phase = YOZORA_NAV_PROVISIONAL;
    navigation->failure = YOZORA_OK;
    navigation->redirects = 0;
    navigation->replace = replace;
    navigation->target = target;
    memmove(navigation->pending_url, url, strlen(url) + 1);
    *token = navigation->token;
    return YOZORA_OK;
}

yozora_status yozora_navigation_begin(yozora_navigation *navigation, const char *url, uint64_t *token) {
    return begin(navigation, url, 0, 0, token);
}

yozora_status yozora_navigation_traverse(yozora_navigation *navigation, int direction, uint64_t *token) {
    uint32_t target;
    if (token) *token = 0;
    if (!navigation || !token || (direction != -1 && direction != 1)) return YOZORA_INVALID;
    if (!navigation->count || (direction == -1 && !navigation->current) ||
        (direction == 1 && navigation->current + 1 >= navigation->count)) return YOZORA_NOT_FOUND;
    target = direction == -1 ? navigation->current - 1 : navigation->current + 1;
    return begin(navigation, navigation->entries[target]->url, 1, target, token);
}

yozora_status yozora_navigation_reload(yozora_navigation *navigation, uint64_t *token) {
    if (token) *token = 0;
    if (!navigation || !token) return YOZORA_INVALID;
    if (!navigation->count) return YOZORA_NOT_FOUND;
    return begin(navigation, navigation->entries[navigation->current]->url, 1, navigation->current, token);
}

static yozora_status check_token(const yozora_navigation *navigation, uint64_t token) {
    if (!navigation || !token) return YOZORA_INVALID;
    if (token != navigation->token) return YOZORA_STALE;
    return YOZORA_OK;
}

yozora_status yozora_navigation_redirect(yozora_navigation *navigation, uint64_t token, const char *url) {
    yozora_status status = check_token(navigation, token);
    if (status != YOZORA_OK) return status;
    if (navigation->phase != YOZORA_NAV_PROVISIONAL) return YOZORA_STATE;
    status = yozora_url_text_check(url);
    if (status != YOZORA_OK) return status;
    if (navigation->redirects == YOZORA_NAVIGATION_REDIRECTS) return YOZORA_LIMIT;
    ++navigation->redirects;
    memmove(navigation->pending_url, url, strlen(url) + 1);
    return YOZORA_OK;
}

yozora_status yozora_navigation_commit(yozora_navigation *navigation, uint64_t token,
                                     const char *url, const char *title, int64_t time_ms) {
    yozora_record *entry;
    uint32_t i;
    yozora_status status = check_token(navigation, token);
    if (status != YOZORA_OK) return status;
    if (navigation->phase != YOZORA_NAV_PROVISIONAL) return YOZORA_STATE;
    if (time_ms < 0) return YOZORA_INVALID;
    status = yozora_url_text_check(url);
    if (status != YOZORA_OK) return status;
    status = yozora_text_check(title, YOZORA_TITLE_CAP, 1);
    if (status != YOZORA_OK) return status;
    entry = calloc(1, sizeof *entry);
    if (!entry) return YOZORA_NOMEM;
    memcpy(entry->url, url, strlen(url) + 1);
    memcpy(entry->title, title, strlen(title) + 1);
    entry->time_ms = time_ms;
    entry->visits = 1;
    if (navigation->replace) {
        free(navigation->entries[navigation->target]);
        navigation->entries[navigation->target] = entry;
        navigation->current = navigation->target;
    } else {
        if (navigation->count) {
            for (i = navigation->current + 1; i < navigation->count; ++i)
                free(navigation->entries[i]);
            navigation->count = navigation->current + 1;
        }
        if (navigation->count == YOZORA_NAVIGATION_ENTRIES) {
            free(navigation->entries[0]);
            memmove(navigation->entries, navigation->entries + 1,
                    (YOZORA_NAVIGATION_ENTRIES - 1) * sizeof navigation->entries[0]);
            --navigation->count;
        }
        navigation->current = navigation->count;
        navigation->entries[navigation->count++] = entry;
    }
    navigation->phase = YOZORA_NAV_COMMITTED;
    navigation->pending_url[0] = '\0';
    return YOZORA_OK;
}

yozora_status yozora_navigation_finish(yozora_navigation *navigation, uint64_t token) {
    yozora_status status = check_token(navigation, token);
    if (status != YOZORA_OK) return status;
    if (navigation->phase != YOZORA_NAV_COMMITTED) return YOZORA_STATE;
    navigation->phase = YOZORA_NAV_FINISHED;
    return YOZORA_OK;
}

static yozora_status terminate(yozora_navigation *navigation, uint64_t token,
                              yozora_navigation_phase phase, yozora_status reason) {
    yozora_status status = check_token(navigation, token);
    if (status != YOZORA_OK) return status;
    if (navigation->phase != YOZORA_NAV_PROVISIONAL && navigation->phase != YOZORA_NAV_COMMITTED)
        return YOZORA_STATE;
    navigation->phase = phase;
    navigation->failure = reason;
    navigation->pending_url[0] = '\0';
    return YOZORA_OK;
}

yozora_status yozora_navigation_fail(yozora_navigation *navigation, uint64_t token, yozora_status reason) {
    if (reason <= YOZORA_OK || reason > YOZORA_SCHEMA) return YOZORA_INVALID;
    return terminate(navigation, token, YOZORA_NAV_FAILED, reason);
}

yozora_status yozora_navigation_cancel(yozora_navigation *navigation, uint64_t token) {
    return terminate(navigation, token, YOZORA_NAV_CANCELLED, YOZORA_OK);
}

yozora_status yozora_navigation_read(const yozora_navigation *navigation, yozora_navigation_snapshot *out) {
    if (!out) return YOZORA_INVALID;
    memset(out, 0, sizeof *out);
    if (!navigation) return YOZORA_INVALID;
    out->token = navigation->token;
    out->phase = navigation->phase;
    out->failure = navigation->failure;
    out->redirects = navigation->redirects;
    out->entry_count = navigation->count;
    out->current_index = navigation->current;
    out->can_go_back = navigation->count && navigation->current > 0;
    out->can_go_forward = navigation->count && navigation->current + 1 < navigation->count;
    memcpy(out->pending_url, navigation->pending_url, sizeof out->pending_url);
    if (navigation->count) out->document = *navigation->entries[navigation->current];
    return YOZORA_OK;
}
