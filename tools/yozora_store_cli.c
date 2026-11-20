#define _POSIX_C_SOURCE 200809L
#include "yozora_store.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static int json_text(const char *text) {
    const unsigned char *byte = (const unsigned char *)text;
    if (putchar('"') == EOF) return 0;
    while (*byte) {
        int result;
        if (*byte == '"' || *byte == '\\') result = printf("\\%c", *byte);
        else if (*byte < 0x20) result = printf("\\u%04x", (unsigned)*byte);
        else result = putchar(*byte);
        if (result < 0) return 0;
        ++byte;
    }
    return putchar('"') != EOF;
}

static int print_record(const yozora_record *record) {
    if (printf("{\"url\":") < 0 || !json_text(record->url) ||
        printf(",\"title\":") < 0 || !json_text(record->title)) return 0;
    return printf(",\"time_ms\":%" PRId64 ",\"visits\":%" PRId64 "}\n",
                  record->time_ms, record->visits) >= 0;
}

static void usage(void) {
    fputs("usage: yozora-store DIRECTORY history|bookmarks [OFFSET]\n"
          "       yozora-store DIRECTORY visit|bookmark URL TITLE\n"
          "       yozora-store DIRECTORY remove-history|remove-bookmark URL\n"
          "       yozora-store DIRECTORY clear-history\n"
          "DIRECTORY must already exist, be absolute, owned by this uid and mode 0700\n", stderr);
}

int main(int argc, char **argv) {
    yozora_store *store = NULL;
    yozora_status status, close_status;
    struct timeval now;
    int64_t time_ms;
    size_t offset = 0, count, i;
    yozora_record records[YOZORA_STORE_PAGE_LIMIT];
    const char *operation;
    int listing, writing, removing;
    if (argc < 3) { usage(); return 2; }
    operation = argv[2];
    listing = strcmp(operation, "history") == 0 || strcmp(operation, "bookmarks") == 0;
    writing = strcmp(operation, "visit") == 0 || strcmp(operation, "bookmark") == 0;
    removing = strcmp(operation, "remove-history") == 0 || strcmp(operation, "remove-bookmark") == 0;
    if ((listing && argc != 3 && argc != 4) || (writing && argc != 5) ||
        (removing && argc != 4) || (!listing && !writing && !removing &&
        (strcmp(operation, "clear-history") != 0 || argc != 3))) { usage(); return 2; }
    if (listing && argc == 4) {
        unsigned long value;
        char *end;
        if (argv[3][0] < '0' || argv[3][0] > '9') { usage(); return 2; }
        errno = 0;
        value = strtoul(argv[3], &end, 10);
        if (errno || *end || value > YOZORA_HISTORY_LIMIT) { usage(); return 2; }
        offset = (size_t)value;
    }
    status = yozora_store_open(argv[1], &store);
    if (status != YOZORA_OK) {
        fprintf(stderr, "yozora-store: %s\n", yozora_status_string(status));
        return 1;
    }
    if (listing) {
        status = strcmp(operation, "history") == 0 ?
            yozora_store_history(store, offset, records, YOZORA_STORE_PAGE_LIMIT, &count) :
            yozora_store_bookmarks(store, offset, records, YOZORA_STORE_PAGE_LIMIT, &count);
        for (i = 0; status == YOZORA_OK && i < count; ++i) {
            if (!print_record(&records[i])) status = YOZORA_IO;
        }
        if (status == YOZORA_OK && fflush(stdout) != 0) status = YOZORA_IO;
    } else if (writing) {
        if (gettimeofday(&now, NULL) != 0 || now.tv_sec < 0 ||
            (uint64_t)now.tv_sec > (uint64_t)(INT64_MAX - 999) / 1000) status = YOZORA_IO;
        else {
            time_ms = (int64_t)now.tv_sec * 1000 + now.tv_usec / 1000;
            status = strcmp(operation, "visit") == 0 ?
                yozora_store_visit(store, argv[3], argv[4], time_ms) :
                yozora_store_bookmark(store, argv[3], argv[4], time_ms);
        }
    } else if (removing) {
        status = strcmp(operation, "remove-history") == 0 ?
            yozora_store_remove_history(store, argv[3]) : yozora_store_remove_bookmark(store, argv[3]);
    } else status = yozora_store_clear_history(store);
    if (status != YOZORA_OK)
        fprintf(stderr, "yozora-store: %s (sqlite %d)\n", yozora_status_string(status), yozora_store_sqlite_error(store));
    close_status = yozora_store_close(store);
    if (close_status != YOZORA_OK)
        fprintf(stderr, "yozora-store: close: %s\n", yozora_status_string(close_status));
    return status == YOZORA_OK && close_status == YOZORA_OK ? 0 : 1;
}
