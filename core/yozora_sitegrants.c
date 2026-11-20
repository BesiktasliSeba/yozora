#include "yozora_sitegrants.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *duplicate(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy)
        memcpy(copy, text, length);
    return copy;
}

typedef struct {
    char *key;
    yozora_grant grant;
} grant_entry;

struct yozora_sitegrants {
    char *header;
    size_t limit;
    size_t count;
    size_t capacity;
    grant_entry *entries;
};

yozora_sitegrants *yozora_sitegrants_create(const char *header, size_t limit)
{
    yozora_sitegrants *grants;
    if (!header || !*header || !limit)
        return NULL;
    grants = calloc(1, sizeof *grants);
    if (!grants)
        return NULL;
    grants->header = duplicate(header);
    if (!grants->header) {
        free(grants);
        return NULL;
    }
    grants->limit = limit;
    return grants;
}

void yozora_sitegrants_clear(yozora_sitegrants *grants)
{
    if (!grants)
        return;
    for (size_t i = 0; i < grants->count; ++i)
        free(grants->entries[i].key);
    grants->count = 0;
}

void yozora_sitegrants_destroy(yozora_sitegrants *grants)
{
    if (!grants)
        return;
    yozora_sitegrants_clear(grants);
    free(grants->entries);
    free(grants->header);
    free(grants);
}

static int key_valid(const char *key)
{
    size_t length;
    if (!key)
        return 0;
    length = strlen(key);
    if (!length || length > YOZORA_GRANT_KEY_MAX)
        return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)key[i];
        /* tab and line breaks separate the fields of the file */
        if (c < 0x20 || c == 0x7f)
            return 0;
    }
    return 1;
}

static size_t find(const yozora_sitegrants *grants, const char *key)
{
    for (size_t i = 0; i < grants->count; ++i)
        if (!strcmp(grants->entries[i].key, key))
            return i;
    return (size_t)-1;
}

yozora_grant yozora_sitegrants_get(const yozora_sitegrants *grants, const char *key)
{
    size_t at;
    if (!grants || !key_valid(key))
        return YOZORA_GRANT_NONE;
    at = find(grants, key);
    return at == (size_t)-1 ? YOZORA_GRANT_NONE : grants->entries[at].grant;
}

static void drop_at(yozora_sitegrants *grants, size_t at)
{
    free(grants->entries[at].key);
    memmove(grants->entries + at, grants->entries + at + 1, (grants->count - at - 1) * sizeof grants->entries[0]);
    --grants->count;
}

int yozora_sitegrants_set(yozora_sitegrants *grants, const char *key, yozora_grant grant)
{
    size_t at;
    char *copy;
    if (!grants || !key_valid(key) || (grant != YOZORA_GRANT_ALLOW && grant != YOZORA_GRANT_DENY))
        return -1;
    /* a site that is decided again counts as the newest */
    at = find(grants, key);
    if (at != (size_t)-1)
        drop_at(grants, at);
    if (grants->count >= grants->limit)
        drop_at(grants, 0);
    if (grants->count == grants->capacity) {
        size_t capacity = grants->capacity ? grants->capacity * 2 : 16;
        grant_entry *grown;
        if (capacity > grants->limit)
            capacity = grants->limit;
        grown = realloc(grants->entries, capacity * sizeof grown[0]);
        if (!grown)
            return -2;
        grants->entries = grown;
        grants->capacity = capacity;
    }
    copy = duplicate(key);
    if (!copy)
        return -2;
    grants->entries[grants->count].key = copy;
    grants->entries[grants->count].grant = grant;
    ++grants->count;
    return 0;
}

int yozora_sitegrants_remove(yozora_sitegrants *grants, const char *key)
{
    size_t at;
    if (!grants || !key_valid(key))
        return 0;
    at = find(grants, key);
    if (at == (size_t)-1)
        return 0;
    drop_at(grants, at);
    return 1;
}

size_t yozora_sitegrants_count(const yozora_sitegrants *grants) { return grants ? grants->count : 0; }

const char *yozora_sitegrants_key_at(const yozora_sitegrants *grants, size_t index)
{
    return grants && index < grants->count ? grants->entries[index].key : NULL;
}

yozora_grant yozora_sitegrants_grant_at(const yozora_sitegrants *grants, size_t index)
{
    return grants && index < grants->count ? grants->entries[index].grant : YOZORA_GRANT_NONE;
}

int yozora_sitegrants_load(yozora_sitegrants *grants, const char *path)
{
    FILE *file;
    char line[YOZORA_GRANT_KEY_MAX + 32];
    int first = 1;
    int result = 0;
    if (!grants || !path)
        return -1;
    yozora_sitegrants_clear(grants);
    file = fopen(path, "r");
    if (!file)
        return errno == ENOENT ? 0 : -1;
    while (fgets(line, sizeof line, file)) {
        size_t length = strlen(line);
        char *tab;
        int status;
        /* a line that did not fit is cut and its rest would read as a line of its own */
        if (!length || line[length - 1] != '\n') {
            if (!feof(file)) {
                int c;
                while ((c = fgetc(file)) != '\n' && c != EOF) {
                }
                continue;
            }
        } else
            line[--length] = '\0';
        if (first) {
            first = 0;
            if (strcmp(line, grants->header))
                break;
            continue;
        }
        tab = strchr(line, '\t');
        if (!tab || tab == line)
            continue;
        *tab = '\0';
        if (!strcmp(tab + 1, "granted"))
            status = yozora_sitegrants_set(grants, line, YOZORA_GRANT_ALLOW);
        else if (!strcmp(tab + 1, "denied"))
            status = yozora_sitegrants_set(grants, line, YOZORA_GRANT_DENY);
        else
            continue;
        if (status == -2) {
            result = -1;
            break;
        }
    }
    if (ferror(file))
        result = -1;
    fclose(file);
    return result;
}

int yozora_sitegrants_save(const yozora_sitegrants *grants, const char *path)
{
    char *temporary;
    FILE *file;
    int failed;
    size_t length;
    if (!grants || !path || !*path)
        return -1;
    length = strlen(path) + 5;
    temporary = malloc(length);
    if (!temporary)
        return -1;
    snprintf(temporary, length, "%s.tmp", path);
    file = fopen(temporary, "w");
    if (!file) {
        free(temporary);
        return -1;
    }
    fprintf(file, "%s\n", grants->header);
    for (size_t i = 0; i < grants->count; ++i)
        fprintf(file, "%s\t%s\n", grants->entries[i].key, grants->entries[i].grant == YOZORA_GRANT_ALLOW ? "granted" : "denied");
    failed = ferror(file) != 0;
    if (fclose(file) != 0)
        failed = 1;
    if (failed || rename(temporary, path) != 0) {
        remove(temporary);
        free(temporary);
        return -1;
    }
    free(temporary);
    return 0;
}
