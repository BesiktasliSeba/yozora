#include "yozora_navigation.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void load(yozora_navigation *navigation, const char *url, int64_t time_ms) {
    uint64_t token;
    assert(yozora_navigation_begin(navigation, url, &token) == YOZORA_OK);
    assert(yozora_navigation_commit(navigation, token, url, "page", time_ms) == YOZORA_OK);
    assert(yozora_navigation_finish(navigation, token) == YOZORA_OK);
}

int main(void) {
    yozora_navigation *navigation = NULL;
    yozora_navigation_snapshot snapshot, previous;
    uint64_t old_token, token, failed_token;
    uint32_t i;
    char url[100], oversized[YOZORA_URL_CAP + 1];
    assert(yozora_navigation_create(NULL) == YOZORA_INVALID);
    assert(yozora_navigation_create(&navigation) == YOZORA_OK);
    assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK);
    assert(snapshot.phase == YOZORA_NAV_IDLE && !snapshot.entry_count && !snapshot.document.url[0]);
    token = 99;
    assert(yozora_navigation_traverse(navigation, -1, &token) == YOZORA_NOT_FOUND && token == 0);
    assert(yozora_navigation_reload(navigation, &token) == YOZORA_NOT_FOUND);

    assert(yozora_navigation_begin(navigation, "https://example.test/old", &old_token) == YOZORA_OK);
    assert(yozora_navigation_finish(navigation, old_token) == YOZORA_STATE);
    assert(yozora_navigation_begin(navigation, "https://example.test/new", &token) == YOZORA_OK);
    assert(token > old_token);
    assert(yozora_navigation_commit(navigation, old_token, "https://example.test/old", "old", 1) == YOZORA_STALE);
    assert(yozora_navigation_redirect(navigation, old_token, "https://example.test/old") == YOZORA_STALE);
    assert(yozora_navigation_fail(navigation, old_token, YOZORA_IO) == YOZORA_STALE);
    assert(yozora_navigation_cancel(navigation, old_token) == YOZORA_STALE);
    for (i = 0; i < YOZORA_NAVIGATION_REDIRECTS; ++i)
        assert(yozora_navigation_redirect(navigation, token, "https://example.test/final") == YOZORA_OK);
    assert(yozora_navigation_redirect(navigation, token, "https://example.test/extra") == YOZORA_LIMIT);
    assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK);
    assert(!snapshot.entry_count && snapshot.redirects == YOZORA_NAVIGATION_REDIRECTS);
    assert(yozora_navigation_commit(navigation, token, "https://example.test/final", "Акари", 2) == YOZORA_OK);
    assert(yozora_navigation_commit(navigation, token, "https://example.test/duplicate", "", 2) == YOZORA_STATE);
    assert(yozora_navigation_finish(navigation, token) == YOZORA_OK);
    assert(yozora_navigation_finish(navigation, token) == YOZORA_STATE);
    assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK);
    assert(snapshot.entry_count == 1 && strcmp(snapshot.document.url, "https://example.test/final") == 0);

    load(navigation, "https://example.test/second", 3);
    load(navigation, "https://example.test/third", 4);
    assert(yozora_navigation_traverse(navigation, -1, &failed_token) == YOZORA_OK);
    assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK && snapshot.current_index == 2);
    assert(yozora_navigation_fail(navigation, failed_token, YOZORA_IO) == YOZORA_OK);
    assert(yozora_navigation_commit(navigation, failed_token, "https://example.test/second", "", 5) == YOZORA_STATE);
    assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK && snapshot.current_index == 2);
    assert(snapshot.phase == YOZORA_NAV_FAILED && snapshot.failure == YOZORA_IO);

    assert(yozora_navigation_traverse(navigation, -1, &token) == YOZORA_OK);
    assert(yozora_navigation_commit(navigation, token, "https://example.test/second", "second", 5) == YOZORA_OK);
    assert(yozora_navigation_finish(navigation, token) == YOZORA_OK);
    assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK);
    assert(snapshot.entry_count == 3 && snapshot.current_index == 1 && snapshot.can_go_forward && snapshot.can_go_back);
    assert(yozora_navigation_begin(navigation, "https://example.test/cancelled", &token) == YOZORA_OK);
    assert(yozora_navigation_cancel(navigation, token) == YOZORA_OK);
    assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK && snapshot.can_go_forward);
    assert(yozora_navigation_redirect(navigation, token, "https://example.test/late") == YOZORA_STATE);

    assert(yozora_navigation_reload(navigation, &token) == YOZORA_OK);
    assert(yozora_navigation_commit(navigation, token, "https://example.test/reloaded", "reload", 6) == YOZORA_OK);
    assert(yozora_navigation_finish(navigation, token) == YOZORA_OK);
    assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK && snapshot.entry_count == 3 && snapshot.current_index == 1);
    load(navigation, "https://example.test/branch", 7);
    assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK && snapshot.entry_count == 3 && !snapshot.can_go_forward);
    assert(yozora_navigation_traverse(navigation, 1, &token) == YOZORA_NOT_FOUND);
    assert(yozora_navigation_traverse(navigation, 2, &token) == YOZORA_INVALID);

    previous = snapshot;
    memset(oversized, 'x', sizeof oversized);
    oversized[sizeof oversized - 1] = '\0';
    assert(yozora_navigation_begin(navigation, oversized, &token) == YOZORA_LIMIT && token == 0);
    assert(yozora_navigation_begin(navigation, "https://example.test/ bad", &token) == YOZORA_INVALID);
    assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK && memcmp(&previous, &snapshot, sizeof snapshot) == 0);

    for (i = 0; i < YOZORA_NAVIGATION_ENTRIES + 5; ++i) {
        snprintf(url, sizeof url, "https://example.test/%u", i);
        load(navigation, url, i + 10);
    }
    assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK);
    assert(snapshot.entry_count == YOZORA_NAVIGATION_ENTRIES && snapshot.current_index == YOZORA_NAVIGATION_ENTRIES - 1);
    for (i = 0; i < YOZORA_NAVIGATION_ENTRIES - 1; ++i) {
        assert(yozora_navigation_traverse(navigation, -1, &token) == YOZORA_OK);
        assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK);
        assert(yozora_navigation_commit(navigation, token, snapshot.pending_url, "back", 100) == YOZORA_OK);
        assert(yozora_navigation_finish(navigation, token) == YOZORA_OK);
    }
    assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK);
    assert(strcmp(snapshot.document.url, "https://example.test/5") == 0);
    assert(yozora_navigation_traverse(navigation, -1, &token) == YOZORA_NOT_FOUND);
    assert(yozora_navigation_begin(navigation, "https://example.test/fail-after-commit", &token) == YOZORA_OK);
    assert(yozora_navigation_commit(navigation, token, "https://example.test/fail-after-commit", "", -1) == YOZORA_INVALID);
    assert(yozora_navigation_commit(navigation, token, "https://example.test/fail-after-commit", "", 200) == YOZORA_OK);
    assert(yozora_navigation_fail(navigation, token, YOZORA_IO) == YOZORA_OK);
    assert(yozora_navigation_read(navigation, &snapshot) == YOZORA_OK);
    assert(snapshot.phase == YOZORA_NAV_FAILED && snapshot.entry_count == 2);
    yozora_navigation_destroy(navigation);
    yozora_navigation_destroy(NULL);
    puts("navigation: stale callbacks, commit boundaries, redirects, cancellation and bounded traversal passed");
    return 0;
}
