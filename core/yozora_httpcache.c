#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "yozora_httpcache.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define FILE_MAGIC 0x43484b41u /* "YZHC" little endian */
#define FILE_VERSION 1u
#define FIXED_BYTES 32u
#define MAX_FILE_BYTES (FIXED_BYTES + YOZORA_URL_CAP + YOZORA_HTTPCACHE_HEADERS_CAP + YOZORA_HTTPCACHE_BODY_CAP + 4u)
#define MAX_SCANNED 65536u
#define MAX_LIFETIME_S 31536000

struct yozora_httpcache {
    char *directory;
    uint64_t max_bytes;
    uint64_t total_bytes;
    unsigned counter;
};

static void put_u32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static uint32_t get_u32(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static uint32_t checksum(const uint8_t *data, size_t length) {
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = 0; i < length; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint64_t name_hash(const char *url) {
    uint64_t hash = 14695981039346656037ull;
    for (; *url; ++url) {
        hash ^= (uint8_t)*url;
        hash *= 1099511628211ull;
    }
    return hash;
}

static int entry_path(const yozora_httpcache *cache, const char *url, char *path, size_t capacity) {
    int length = snprintf(path, capacity, "%s/%016llx.entry", cache->directory, (unsigned long long)name_hash(url));
    return length > 0 && (size_t)length < capacity;
}

static int is_entry_name(const char *name) {
    size_t length = strlen(name);
    return length == 22 && strcmp(name + 16, ".entry") == 0;
}

static int is_temp_name(const char *name) {
    return strncmp(name, ".tmp-", 5) == 0;
}

static int lower(int c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int equal_ignore_case(const char *a, size_t length, const char *b) {
    size_t i;
    if (strlen(b) != length) return 0;
    for (i = 0; i < length; ++i)
        if (lower((unsigned char)a[i]) != lower((unsigned char)b[i])) return 0;
    return 1;
}

/* the trimmed value span of the first header with this name */
static int find_header(const char *headers, size_t length, const char *name, const char **value, size_t *value_length) {
    size_t position = 0;
    if (!headers) return 0;
    while (position < length) {
        size_t end = position, colon = 0, start, stop;
        int has_colon = 0;
        while (end < length && headers[end] != '\n') {
            if (headers[end] == ':' && !has_colon) {
                has_colon = 1;
                colon = end;
            }
            ++end;
        }
        if (has_colon && equal_ignore_case(headers + position, colon - position, name)) {
            start = colon + 1;
            stop = end;
            while (start < stop && (headers[start] == ' ' || headers[start] == '\t')) ++start;
            while (stop > start && (headers[stop - 1] == '\r' || headers[stop - 1] == ' ' || headers[stop - 1] == '\t')) --stop;
            *value = headers + start;
            *value_length = stop - start;
            return 1;
        }
        position = end + 1;
    }
    return 0;
}

int yozora_cache_header(const char *headers, size_t length, const char *name, char *value, size_t capacity) {
    const char *found;
    size_t found_length;
    if (!name || !value || !capacity) return 0;
    value[0] = 0;
    if (!find_header(headers, length, name, &found, &found_length) || found_length >= capacity) return 0;
    memcpy(value, found, found_length);
    value[found_length] = 0;
    return 1;
}

/* days since 1970-01-01 of a proleptic gregorian date */
static int64_t days_from_civil(int64_t year, int month, int day) {
    int64_t era, year_of_era, day_of_year, day_of_era;
    year -= month <= 2;
    era = (year >= 0 ? year : year - 399) / 400;
    year_of_era = year - era * 400;
    day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

static int digits(const char *text, int count, int *value) {
    int i, result = 0;
    for (i = 0; i < count; ++i) {
        if (text[i] < '0' || text[i] > '9') return 0;
        result = result * 10 + (text[i] - '0');
    }
    *value = result;
    return 1;
}

int yozora_http_date(const char *text, size_t length, int64_t *epoch_ms) {
    static const char *const days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const int month_lengths[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int day, year, hour, minute, second, month = -1, weekday = -1, i;
    int64_t days_since_epoch;
    if (!text || !epoch_ms || length != 29) return 0;
    if (text[3] != ',' || text[4] != ' ' || text[7] != ' ' || text[11] != ' ' || text[16] != ' ' ||
        text[19] != ':' || text[22] != ':' || text[25] != ' ' || memcmp(text + 26, "GMT", 3) != 0) return 0;
    for (i = 0; i < 7; ++i)
        if (memcmp(text, days[i], 3) == 0) weekday = i;
    for (i = 0; i < 12; ++i)
        if (memcmp(text + 8, months[i], 3) == 0) month = i;
    if (weekday < 0 || month < 0) return 0;
    if (!digits(text + 5, 2, &day) || !digits(text + 12, 4, &year) || !digits(text + 17, 2, &hour) ||
        !digits(text + 20, 2, &minute) || !digits(text + 23, 2, &second)) return 0;
    if (day < 1 || day > month_lengths[month] || year < 1970 || hour > 23 || minute > 59 || second > 60) return 0;
    if (month == 1 && day == 29 && !((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 0;
    days_since_epoch = days_from_civil(year, month + 1, day);
    *epoch_ms = (((days_since_epoch * 24 + hour) * 60 + minute) * 60 + second) * 1000;
    return 1;
}

/* cache-control: finds a directive by name, value is set when the directive has a number */
static int directive(const char *text, const char *name, int64_t *value, int *has_value) {
    size_t name_length = strlen(name);
    const char *cursor = text;
    *has_value = 0;
    *value = 0;
    while (*cursor) {
        const char *end = strchr(cursor, ',');
        const char *token = cursor, *token_end = end ? end : cursor + strlen(cursor);
        cursor = end ? end + 1 : token_end;
        while (token < token_end && (*token == ' ' || *token == '\t')) ++token;
        while (token_end > token && (token_end[-1] == ' ' || token_end[-1] == '\t')) --token_end;
        if ((size_t)(token_end - token) < name_length) continue;
        if (!equal_ignore_case(token, name_length, name)) continue;
        if ((size_t)(token_end - token) == name_length) return 1;
        if (token[name_length] == '=') {
            const char *number = token + name_length + 1;
            int64_t result = 0;
            int seen = 0;
            if (number < token_end && *number == '"') ++number;
            while (number < token_end && *number >= '0' && *number <= '9') {
                if (result < MAX_LIFETIME_S * 4) result = result * 10 + (*number - '0');
                ++number;
                seen = 1;
            }
            *value = result;
            *has_value = seen;
            return 1;
        }
    }
    return 0;
}

void yozora_cache_policy_parse(int status, const char *headers, size_t length, yozora_cache_policy *policy) {
    char control[512], text[128], expires_text[64], date_text[64];
    const char *found;
    size_t found_length;
    int64_t max_age = 0, age = 0, expires_ms, date_ms;
    int has_max_age = 0, has_age = 0, no_cache;
    memset(policy, 0, sizeof *policy);
    if (status != 200 || !headers) return;
    control[0] = 0;
    /* a header too long to inspect is a refusal, a half read policy could cache a no-store response */
    if (find_header(headers, length, "Cache-Control", &found, &found_length) && !yozora_cache_header(headers, length, "Cache-Control", control, sizeof control)) return;
    if (directive(control, "no-store", &max_age, &has_max_age)) return;
    /* matching a stored copy against request headers is not implemented, only a transfer coding vary is safe */
    if (find_header(headers, length, "Vary", &found, &found_length) && !(found_length == 15 && equal_ignore_case(found, found_length, "Accept-Encoding"))) return;
    policy->has_validator = find_header(headers, length, "ETag", &found, &found_length) && found_length > 0;
    if (!policy->has_validator)
        policy->has_validator = find_header(headers, length, "Last-Modified", &found, &found_length) && found_length > 0;
    no_cache = directive(control, "no-cache", &max_age, &has_max_age);
    directive(control, "max-age", &max_age, &has_max_age);
    if (no_cache)
        policy->lifetime_s = 0;
    else if (has_max_age)
        policy->lifetime_s = max_age;
    else if (yozora_cache_header(headers, length, "Expires", expires_text, sizeof expires_text) &&
             yozora_cache_header(headers, length, "Date", date_text, sizeof date_text) &&
             yozora_http_date(expires_text, strlen(expires_text), &expires_ms) &&
             yozora_http_date(date_text, strlen(date_text), &date_ms) && expires_ms > date_ms)
        policy->lifetime_s = (expires_ms - date_ms) / 1000;
    if (yozora_cache_header(headers, length, "Age", text, sizeof text)) {
        const char *cursor = text;
        while (*cursor >= '0' && *cursor <= '9' && age < MAX_LIFETIME_S) {
            age = age * 10 + (*cursor - '0');
            ++cursor;
            has_age = 1;
        }
        if (!has_age) age = 0;
    }
    policy->lifetime_s -= age;
    if (policy->lifetime_s < 0) policy->lifetime_s = 0;
    if (policy->lifetime_s > MAX_LIFETIME_S) policy->lifetime_s = MAX_LIFETIME_S;
    policy->storable = policy->lifetime_s > 0 || policy->has_validator;
}

yozora_cache_decision yozora_cache_decide(const yozora_cache_entry *entry, yozora_cache_mode mode, int64_t now_ms) {
    yozora_cache_policy policy;
    int64_t age_s;
    if (!entry || mode == YOZORA_CACHE_RELOAD) return YOZORA_CACHE_USE_NETWORK;
    if (mode == YOZORA_CACHE_HISTORY) return YOZORA_CACHE_USE_STORED;
    yozora_cache_policy_parse(entry->status, entry->headers, entry->headers_length, &policy);
    if (!policy.storable) return YOZORA_CACHE_USE_NETWORK;
    /* a clock that moved backwards makes the age unknown, ask the server */
    if (now_ms < entry->response_ms) return policy.has_validator ? YOZORA_CACHE_USE_VALIDATE : YOZORA_CACHE_USE_NETWORK;
    age_s = (now_ms - entry->response_ms) / 1000;
    if (policy.lifetime_s > age_s) return YOZORA_CACHE_USE_STORED;
    return policy.has_validator ? YOZORA_CACHE_USE_VALIDATE : YOZORA_CACHE_USE_NETWORK;
}

static yozora_status check_directory(const char *directory) {
    struct stat attributes;
    size_t length;
    yozora_status status = yozora_text_check(directory, 3000, 0);
    if (status != YOZORA_OK) return status;
    if (directory[0] != '/') return YOZORA_INVALID;
    length = strlen(directory);
    if (directory[length - 1] == '/' || strstr(directory, "//") || strstr(directory, "/../") || strstr(directory, "/./") ||
        (length >= 3 && strcmp(directory + length - 3, "/..") == 0) ||
        (length >= 2 && strcmp(directory + length - 2, "/.") == 0)) return YOZORA_INVALID;
    if (lstat(directory, &attributes) != 0) return YOZORA_IO;
    if (!S_ISDIR(attributes.st_mode) || attributes.st_uid != geteuid() || (attributes.st_mode & 07777) != 0700) return YOZORA_IO;
    return YOZORA_OK;
}

typedef struct {
    char name[32];
    uint64_t size;
    int64_t mtime;
} scanned_file;

static int by_age(const void *a, const void *b) {
    const scanned_file *left = a, *right = b;
    if (left->mtime != right->mtime) return left->mtime < right->mtime ? -1 : 1;
    return strcmp(left->name, right->name);
}

/* whole seconds are too coarse to order entries written in one burst */
static int64_t modified_ns(const struct stat *attributes) {
#ifdef __APPLE__
    return (int64_t)attributes->st_mtimespec.tv_sec * 1000000000 + attributes->st_mtimespec.tv_nsec;
#else
    return (int64_t)attributes->st_mtim.tv_sec * 1000000000 + attributes->st_mtim.tv_nsec;
#endif
}

/* lists the entry files, removes leftover temporary files, returns the total size */
static yozora_status scan(yozora_httpcache *cache, scanned_file **files, size_t *count, uint64_t *total) {
    DIR *directory = opendir(cache->directory);
    struct dirent *item;
    size_t capacity = 64;
    scanned_file *list;
    *files = NULL;
    *count = 0;
    *total = 0;
    if (!directory) return YOZORA_IO;
    list = malloc(capacity * sizeof *list);
    if (!list) {
        closedir(directory);
        return YOZORA_NOMEM;
    }
    while ((item = readdir(directory)) != NULL) {
        char path[4096];
        struct stat attributes;
        int length;
        if (!is_entry_name(item->d_name) && !is_temp_name(item->d_name)) continue;
        length = snprintf(path, sizeof path, "%s/%s", cache->directory, item->d_name);
        if (length < 0 || (size_t)length >= sizeof path || lstat(path, &attributes) != 0) continue;
        if (!S_ISREG(attributes.st_mode)) continue;
        if (is_temp_name(item->d_name)) {
            unlink(path);
            continue;
        }
        if (*count >= MAX_SCANNED) {
            unlink(path);
            continue;
        }
        if (*count == capacity) {
            scanned_file *grown = realloc(list, capacity * 2 * sizeof *list);
            if (!grown) {
                free(list);
                closedir(directory);
                return YOZORA_NOMEM;
            }
            list = grown;
            capacity *= 2;
        }
        /* is_entry_name accepted exactly 22 characters */
        memcpy(list[*count].name, item->d_name, 23);
        list[*count].size = (uint64_t)attributes.st_size;
        list[*count].mtime = modified_ns(&attributes);
        *total += list[*count].size;
        ++*count;
    }
    closedir(directory);
    *files = list;
    return YOZORA_OK;
}

static void enforce_limit(yozora_httpcache *cache) {
    scanned_file *files;
    size_t count, i;
    uint64_t total;
    if (cache->total_bytes <= cache->max_bytes) return;
    if (scan(cache, &files, &count, &total) != YOZORA_OK) return;
    qsort(files, count, sizeof *files, by_age);
    /* evict down to nine tenths so the next store does not scan again */
    for (i = 0; i < count && total > cache->max_bytes / 10 * 9; ++i) {
        char path[4096];
        int length = snprintf(path, sizeof path, "%s/%s", cache->directory, files[i].name);
        if (length > 0 && (size_t)length < sizeof path && unlink(path) == 0) total -= files[i].size;
    }
    cache->total_bytes = total;
    free(files);
}

yozora_status yozora_httpcache_open(yozora_httpcache **out, const char *directory, uint64_t max_bytes) {
    yozora_httpcache *cache;
    scanned_file *files;
    size_t count;
    yozora_status status;
    if (!out) return YOZORA_INVALID;
    *out = NULL;
    if (!directory || max_bytes < 1024 * 1024 || max_bytes > 1024ull * 1024 * 1024) return YOZORA_INVALID;
    status = check_directory(directory);
    if (status != YOZORA_OK) return status;
    cache = calloc(1, sizeof *cache);
    if (!cache) return YOZORA_NOMEM;
    cache->directory = malloc(strlen(directory) + 1);
    if (!cache->directory) {
        free(cache);
        return YOZORA_NOMEM;
    }
    strcpy(cache->directory, directory);
    cache->max_bytes = max_bytes;
    status = scan(cache, &files, &count, &cache->total_bytes);
    if (status != YOZORA_OK) {
        yozora_httpcache_close(cache);
        return status;
    }
    free(files);
    enforce_limit(cache);
    *out = cache;
    return YOZORA_OK;
}

void yozora_httpcache_close(yozora_httpcache *cache) {
    if (!cache) return;
    free(cache->directory);
    free(cache);
}

void yozora_cache_entry_release(yozora_cache_entry *entry) {
    if (!entry) return;
    free(entry->headers);
    free(entry->body);
    memset(entry, 0, sizeof *entry);
}

static yozora_status read_all(int fd, uint8_t *buffer, size_t length) {
    size_t done = 0;
    while (done < length) {
        ssize_t count = read(fd, buffer + done, length - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return YOZORA_IO;
        done += (size_t)count;
    }
    return YOZORA_OK;
}

static yozora_status write_all(int fd, const uint8_t *buffer, size_t length) {
    size_t done = 0;
    while (done < length) {
        ssize_t count = write(fd, buffer + done, length - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return YOZORA_IO;
        done += (size_t)count;
    }
    return YOZORA_OK;
}

yozora_status yozora_httpcache_load(yozora_httpcache *cache, const char *url, yozora_cache_entry *entry) {
    char path[4096];
    struct stat attributes;
    uint8_t *data = NULL;
    size_t size, url_length, headers_length, body_length;
    uint32_t status_code;
    int fd;
    yozora_status status;
    if (!cache || !url || !entry) return YOZORA_INVALID;
    memset(entry, 0, sizeof *entry);
    if (yozora_url_text_check(url) != YOZORA_OK) return YOZORA_INVALID;
    if (!entry_path(cache, url, path, sizeof path)) return YOZORA_LIMIT;
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return errno == ENOENT ? YOZORA_NOT_FOUND : YOZORA_IO;
    if (fstat(fd, &attributes) != 0 || !S_ISREG(attributes.st_mode) || attributes.st_uid != geteuid()) {
        close(fd);
        return YOZORA_IO;
    }
    if (attributes.st_size < (off_t)(FIXED_BYTES + 4) || (uint64_t)attributes.st_size > MAX_FILE_BYTES) {
        close(fd);
        unlink(path);
        return YOZORA_CORRUPT;
    }
    size = (size_t)attributes.st_size;
    data = malloc(size);
    if (!data) {
        close(fd);
        return YOZORA_NOMEM;
    }
    status = read_all(fd, data, size);
    close(fd);
    if (status != YOZORA_OK) {
        free(data);
        return status;
    }
    url_length = get_u32(data + 12);
    headers_length = get_u32(data + 16);
    body_length = get_u32(data + 20);
    status_code = get_u32(data + 8);
    if (get_u32(data) != FILE_MAGIC || get_u32(data + 4) != FILE_VERSION || url_length == 0 || url_length >= YOZORA_URL_CAP ||
        headers_length > YOZORA_HTTPCACHE_HEADERS_CAP || body_length > YOZORA_HTTPCACHE_BODY_CAP || status_code != 200 ||
        (uint64_t)FIXED_BYTES + url_length + headers_length + body_length + 4 != size ||
        checksum(data, size - 4) != get_u32(data + size - 4)) {
        free(data);
        unlink(path);
        return YOZORA_CORRUPT;
    }
    /* two urls can share a file name, the stored address decides */
    if (strlen(url) != url_length || memcmp(data + FIXED_BYTES, url, url_length) != 0) {
        free(data);
        return YOZORA_NOT_FOUND;
    }
    snprintf(entry->url, sizeof entry->url, "%s", url);
    entry->status = (int)status_code;
    entry->response_ms = (int64_t)((uint64_t)get_u32(data + 24) | ((uint64_t)get_u32(data + 28) << 32));
    entry->headers = malloc(headers_length + 1);
    entry->body = malloc(body_length ? body_length : 1);
    if (!entry->headers || !entry->body) {
        yozora_cache_entry_release(entry);
        free(data);
        return YOZORA_NOMEM;
    }
    memcpy(entry->headers, data + FIXED_BYTES + url_length, headers_length);
    entry->headers[headers_length] = 0;
    entry->headers_length = headers_length;
    memcpy(entry->body, data + FIXED_BYTES + url_length + headers_length, body_length);
    entry->body_length = body_length;
    free(data);
    /* a used entry becomes the newest for the eviction order */
    utimes(path, NULL);
    return YOZORA_OK;
}

yozora_status yozora_httpcache_store(yozora_httpcache *cache, const yozora_cache_entry *entry) {
    char path[4096], temporary[4096];
    struct stat attributes;
    uint8_t *data;
    size_t url_length, size;
    uint64_t old_size = 0;
    int fd, length;
    yozora_status status;
    if (!cache || !entry || entry->status != 200 || yozora_url_text_check(entry->url) != YOZORA_OK) return YOZORA_INVALID;
    if (entry->headers_length > YOZORA_HTTPCACHE_HEADERS_CAP || entry->body_length > YOZORA_HTTPCACHE_BODY_CAP ||
        (entry->headers_length && !entry->headers) || (entry->body_length && !entry->body)) return YOZORA_LIMIT;
    url_length = strlen(entry->url);
    size = FIXED_BYTES + url_length + entry->headers_length + entry->body_length + 4;
    if (size > cache->max_bytes / 4) return YOZORA_LIMIT;
    if (!entry_path(cache, entry->url, path, sizeof path)) return YOZORA_LIMIT;
    length = snprintf(temporary, sizeof temporary, "%s/.tmp-%u-%ld", cache->directory, ++cache->counter, (long)getpid());
    if (length < 0 || (size_t)length >= sizeof temporary) return YOZORA_LIMIT;
    data = malloc(size);
    if (!data) return YOZORA_NOMEM;
    put_u32(data, FILE_MAGIC);
    put_u32(data + 4, FILE_VERSION);
    put_u32(data + 8, (uint32_t)entry->status);
    put_u32(data + 12, (uint32_t)url_length);
    put_u32(data + 16, (uint32_t)entry->headers_length);
    put_u32(data + 20, (uint32_t)entry->body_length);
    put_u32(data + 24, (uint32_t)((uint64_t)entry->response_ms & 0xffffffffu));
    put_u32(data + 28, (uint32_t)((uint64_t)entry->response_ms >> 32));
    memcpy(data + FIXED_BYTES, entry->url, url_length);
    if (entry->headers_length) memcpy(data + FIXED_BYTES + url_length, entry->headers, entry->headers_length);
    if (entry->body_length) memcpy(data + FIXED_BYTES + url_length + entry->headers_length, entry->body, entry->body_length);
    put_u32(data + size - 4, checksum(data, size - 4));
    fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        free(data);
        return YOZORA_IO;
    }
    status = write_all(fd, data, size);
    free(data);
    if (close(fd) != 0 && status == YOZORA_OK) status = YOZORA_IO;
    if (status == YOZORA_OK && lstat(path, &attributes) == 0 && S_ISREG(attributes.st_mode)) old_size = (uint64_t)attributes.st_size;
    if (status == YOZORA_OK && rename(temporary, path) != 0) status = YOZORA_IO;
    if (status != YOZORA_OK) {
        unlink(temporary);
        return status;
    }
    cache->total_bytes = cache->total_bytes - (old_size < cache->total_bytes ? old_size : cache->total_bytes) + size;
    enforce_limit(cache);
    return YOZORA_OK;
}

yozora_status yozora_httpcache_clear(yozora_httpcache *cache) {
    DIR *directory;
    struct dirent *item;
    yozora_status status = YOZORA_OK;
    if (!cache) return YOZORA_INVALID;
    directory = opendir(cache->directory);
    if (!directory) return YOZORA_IO;
    while ((item = readdir(directory)) != NULL) {
        char path[4096];
        int length;
        if (!is_entry_name(item->d_name) && !is_temp_name(item->d_name)) continue;
        length = snprintf(path, sizeof path, "%s/%s", cache->directory, item->d_name);
        if (length < 0 || (size_t)length >= sizeof path) continue;
        if (unlink(path) != 0 && errno != ENOENT) status = YOZORA_IO;
    }
    closedir(directory);
    cache->total_bytes = 0;
    return status;
}

void yozora_byte_budget_init(yozora_byte_budget *budget, size_t limit)
{
    budget->limit = limit;
    __atomic_store_n(&budget->used, 0, __ATOMIC_RELAXED);
}

int yozora_byte_budget_reserve(yozora_byte_budget *budget, size_t bytes)
{
    size_t used = __atomic_load_n(&budget->used, __ATOMIC_RELAXED);
    do {
        if (bytes > budget->limit || used > budget->limit - bytes)
            return 0;
    } while (!__atomic_compare_exchange_n(&budget->used, &used, used + bytes, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
    return 1;
}

void yozora_byte_budget_release(yozora_byte_budget *budget, size_t bytes)
{
    __atomic_fetch_sub(&budget->used, bytes, __ATOMIC_ACQ_REL);
}
