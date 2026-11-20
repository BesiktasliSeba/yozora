#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "yozora_httpcache.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_httpcache: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static void policy_of(const char *headers, yozora_cache_policy *policy)
{
    yozora_cache_policy_parse(200, headers, strlen(headers), policy);
}

static void test_dates(void)
{
    int64_t value = 0;
    const char *good = "Sun, 06 Nov 1994 08:49:37 GMT";
    CHECK(yozora_http_date(good, strlen(good), &value) && value == 784111777000ll);
    good = "Thu, 01 Jan 1970 00:00:00 GMT";
    CHECK(yozora_http_date(good, strlen(good), &value) && value == 0);
    good = "Thu, 29 Feb 2024 12:00:00 GMT";
    CHECK(yozora_http_date(good, strlen(good), &value) && value == 1709208000000ll);
    good = "Tue, 29 Feb 2028 12:00:00 GMT";
    CHECK(yozora_http_date(good, strlen(good), &value));
    /* not a leap year */
    good = "Wed, 29 Feb 2023 12:00:00 GMT";
    CHECK(!yozora_http_date(good, strlen(good), &value));
    good = "Sun, 06 Nov 1994 08:49:37 UTC";
    CHECK(!yozora_http_date(good, strlen(good), &value));
    good = "Sun, 06 Nov 1994 25:49:37 GMT";
    CHECK(!yozora_http_date(good, strlen(good), &value));
    good = "Sun, 31 Apr 2020 08:49:37 GMT";
    CHECK(!yozora_http_date(good, strlen(good), &value));
    good = "Xxx, 06 Nov 1994 08:49:37 GMT";
    CHECK(!yozora_http_date(good, strlen(good), &value));
    good = "Sun, 06 Nov 1994 08:49:37 GMT ";
    CHECK(!yozora_http_date(good, strlen(good), &value));
    CHECK(!yozora_http_date("", 0, &value));
    CHECK(!yozora_http_date(NULL, 29, &value));
}

static void test_header_lookup(void)
{
    const char *headers = "Content-Type: text/html\r\nETag:  \"abc\" \r\nx-test:1\r\n";
    char value[16];
    CHECK(yozora_cache_header(headers, strlen(headers), "etag", value, sizeof value) && strcmp(value, "\"abc\"") == 0);
    CHECK(yozora_cache_header(headers, strlen(headers), "X-Test", value, sizeof value) && strcmp(value, "1") == 0);
    CHECK(!yozora_cache_header(headers, strlen(headers), "Missing", value, sizeof value));
    /* a value that does not fit is not truncated */
    CHECK(!yozora_cache_header(headers, strlen(headers), "Content-Type", value, 5));
    CHECK(!yozora_cache_header(headers, strlen(headers), "Type", value, sizeof value));
    CHECK(!yozora_cache_header(NULL, 0, "etag", value, sizeof value));
}

static void test_policy(void)
{
    yozora_cache_policy policy;
    char long_control[1200];
    policy_of("Cache-Control: max-age=60\r\n", &policy);
    CHECK(policy.storable && policy.lifetime_s == 60 && !policy.has_validator);
    policy_of("Cache-Control: public, MAX-AGE=\"90\", must-revalidate\r\nETag: \"x\"\r\n", &policy);
    CHECK(policy.storable && policy.lifetime_s == 90 && policy.has_validator);
    policy_of("Cache-Control: no-store, max-age=60\r\n", &policy);
    CHECK(!policy.storable);
    policy_of("Cache-Control: no-cache\r\nETag: \"x\"\r\n", &policy);
    CHECK(policy.storable && policy.lifetime_s == 0 && policy.has_validator);
    /* no freshness and no validator: a stored copy could never be used */
    policy_of("Content-Type: text/html\r\n", &policy);
    CHECK(!policy.storable);
    policy_of("Last-Modified: Sun, 06 Nov 1994 08:49:37 GMT\r\n", &policy);
    CHECK(policy.storable && policy.lifetime_s == 0 && policy.has_validator);
    policy_of("Date: Sun, 06 Nov 1994 08:49:37 GMT\r\nExpires: Sun, 06 Nov 1994 09:49:37 GMT\r\n", &policy);
    CHECK(policy.storable && policy.lifetime_s == 3600);
    /* max-age wins over Expires */
    policy_of("Cache-Control: max-age=10\r\nDate: Sun, 06 Nov 1994 08:49:37 GMT\r\nExpires: Sun, 06 Nov 1994 09:49:37 GMT\r\n", &policy);
    CHECK(policy.lifetime_s == 10);
    /* an Expires in the past or unreadable is already stale */
    policy_of("Date: Sun, 06 Nov 1994 08:49:37 GMT\r\nExpires: Sun, 06 Nov 1994 07:49:37 GMT\r\n", &policy);
    CHECK(!policy.storable);
    policy_of("Expires: 0\r\nDate: Sun, 06 Nov 1994 08:49:37 GMT\r\n", &policy);
    CHECK(!policy.storable);
    policy_of("Cache-Control: max-age=100\r\nAge: 30\r\n", &policy);
    CHECK(policy.lifetime_s == 70);
    policy_of("Cache-Control: max-age=100\r\nAge: 500\r\n", &policy);
    CHECK(!policy.storable);
    policy_of("Cache-Control: max-age=999999999999\r\n", &policy);
    CHECK(policy.storable && policy.lifetime_s == 31536000);
    /* the stored copy is not matched against request headers, so any other vary is refused */
    policy_of("Cache-Control: max-age=60\r\nVary: Cookie\r\n", &policy);
    CHECK(!policy.storable);
    policy_of("Cache-Control: max-age=60\r\nVary: *\r\n", &policy);
    CHECK(!policy.storable);
    policy_of("Cache-Control: max-age=60\r\nVary: Accept-Encoding, User-Agent\r\n", &policy);
    CHECK(!policy.storable);
    policy_of("Cache-Control: max-age=60\r\nVary: accept-encoding\r\n", &policy);
    CHECK(policy.storable);
    /* a Cache-Control value too long to read cannot be trusted to hold no-store */
    memset(long_control, 'a', sizeof long_control);
    memcpy(long_control, "Cache-Control: ", 15);
    memcpy(long_control + 1100, ", no-store\r\n", 13);
    yozora_cache_policy_parse(200, long_control, strlen(long_control), &policy);
    CHECK(!policy.storable);
    yozora_cache_policy_parse(404, "Cache-Control: max-age=60\r\n", 27, &policy);
    CHECK(!policy.storable);
    yozora_cache_policy_parse(206, "Cache-Control: max-age=60\r\n", 27, &policy);
    CHECK(!policy.storable);
    yozora_cache_policy_parse(200, NULL, 0, &policy);
    CHECK(!policy.storable);
}

static void test_decision(void)
{
    yozora_cache_entry entry;
    char fresh[] = "Cache-Control: max-age=60\r\nETag: \"x\"\r\n";
    char no_validator[] = "Cache-Control: max-age=60\r\n";
    memset(&entry, 0, sizeof entry);
    entry.status = 200;
    entry.response_ms = 1000000;
    entry.headers = fresh;
    entry.headers_length = strlen(fresh);
    CHECK(yozora_cache_decide(&entry, YOZORA_CACHE_NORMAL, 1000000 + 59000) == YOZORA_CACHE_USE_STORED);
    CHECK(yozora_cache_decide(&entry, YOZORA_CACHE_NORMAL, 1000000 + 61000) == YOZORA_CACHE_USE_VALIDATE);
    /* the boundary second is stale */
    CHECK(yozora_cache_decide(&entry, YOZORA_CACHE_NORMAL, 1000000 + 60000) == YOZORA_CACHE_USE_VALIDATE);
    CHECK(yozora_cache_decide(&entry, YOZORA_CACHE_RELOAD, 1000000) == YOZORA_CACHE_USE_NETWORK);
    CHECK(yozora_cache_decide(&entry, YOZORA_CACHE_HISTORY, 1000000 + 86400000ll * 400) == YOZORA_CACHE_USE_STORED);
    /* a clock that went back asks the server instead of trusting the age */
    CHECK(yozora_cache_decide(&entry, YOZORA_CACHE_NORMAL, 1000) == YOZORA_CACHE_USE_VALIDATE);
    entry.headers = no_validator;
    entry.headers_length = strlen(no_validator);
    CHECK(yozora_cache_decide(&entry, YOZORA_CACHE_NORMAL, 1000000 + 61000) == YOZORA_CACHE_USE_NETWORK);
    CHECK(yozora_cache_decide(&entry, YOZORA_CACHE_NORMAL, 1000) == YOZORA_CACHE_USE_NETWORK);
    CHECK(yozora_cache_decide(NULL, YOZORA_CACHE_HISTORY, 0) == YOZORA_CACHE_USE_NETWORK);
}

static void make_entry(yozora_cache_entry *entry, const char *url, size_t body_length, uint8_t fill)
{
    memset(entry, 0, sizeof *entry);
    snprintf(entry->url, sizeof entry->url, "%s", url);
    entry->status = 200;
    entry->response_ms = 1700000000000ll;
    entry->headers = strdup("Content-Type: text/plain\r\nCache-Control: max-age=60\r\n");
    entry->headers_length = strlen(entry->headers);
    entry->body = malloc(body_length ? body_length : 1);
    memset(entry->body, fill, body_length);
    entry->body_length = body_length;
}

static size_t count_entries(const char *directory)
{
    DIR *handle = opendir(directory);
    struct dirent *item;
    size_t count = 0;
    while (handle && (item = readdir(handle)) != NULL)
        if (item->d_name[0] != '.') ++count;
    if (handle)
        closedir(handle);
    return count;
}

static void test_store(const char *root)
{
    char directory[512], path[1024];
    yozora_httpcache *cache = NULL;
    yozora_cache_entry stored, loaded;
    DIR *handle;
    struct dirent *item;
    snprintf(directory, sizeof directory, "%s/store", root);
    CHECK(mkdir(directory, 0700) == 0);
    CHECK(yozora_httpcache_open(&cache, directory, 1024 * 1024) == YOZORA_OK && cache);
    make_entry(&stored, "https://example.test/a.css?x=1", 5000, 0x61);
    CHECK(yozora_httpcache_load(cache, stored.url, &loaded) == YOZORA_NOT_FOUND);
    CHECK(yozora_httpcache_store(cache, &stored) == YOZORA_OK);
    CHECK(yozora_httpcache_load(cache, stored.url, &loaded) == YOZORA_OK);
    CHECK(loaded.status == 200 && loaded.response_ms == stored.response_ms && loaded.body_length == 5000);
    CHECK(loaded.headers_length == stored.headers_length && memcmp(loaded.headers, stored.headers, stored.headers_length) == 0);
    CHECK(memcmp(loaded.body, stored.body, 5000) == 0 && strcmp(loaded.url, stored.url) == 0);
    yozora_cache_entry_release(&loaded);
    CHECK(loaded.body == NULL && loaded.headers == NULL);
    /* an empty body is a valid response */
    free(stored.body);
    stored.body = NULL;
    stored.body_length = 0;
    CHECK(yozora_httpcache_store(cache, &stored) == YOZORA_OK);
    CHECK(yozora_httpcache_load(cache, stored.url, &loaded) == YOZORA_OK && loaded.body_length == 0);
    yozora_cache_entry_release(&loaded);
    /* the file persists across a reopen */
    yozora_httpcache_close(cache);
    cache = NULL;
    CHECK(yozora_httpcache_open(&cache, directory, 1024 * 1024) == YOZORA_OK);
    CHECK(yozora_httpcache_load(cache, stored.url, &loaded) == YOZORA_OK);
    yozora_cache_entry_release(&loaded);
    /* limits */
    free(stored.body);
    stored.body = malloc(YOZORA_HTTPCACHE_BODY_CAP + 1);
    stored.body_length = YOZORA_HTTPCACHE_BODY_CAP + 1;
    CHECK(yozora_httpcache_store(cache, &stored) == YOZORA_LIMIT);
    stored.body_length = 300 * 1024;
    /* one entry may not take more than a quarter of the cache */
    CHECK(yozora_httpcache_store(cache, &stored) == YOZORA_LIMIT);
    stored.body_length = 10;
    stored.status = 404;
    CHECK(yozora_httpcache_store(cache, &stored) == YOZORA_INVALID);
    stored.status = 200;
    memset(stored.url, 'u', 4095);
    stored.url[4095] = 0;
    CHECK(yozora_httpcache_load(cache, "", &loaded) == YOZORA_INVALID);
    yozora_cache_entry_release(&stored);

    /* damaged files are reported once and removed */
    make_entry(&stored, "https://example.test/damaged", 2000, 0x62);
    CHECK(yozora_httpcache_store(cache, &stored) == YOZORA_OK);
    handle = opendir(directory);
    path[0] = 0;
    while (handle && (item = readdir(handle)) != NULL) {
        if (strstr(item->d_name, ".entry")) {
            struct stat attributes;
            snprintf(path, sizeof path, "%s/%s", directory, item->d_name);
            if (stat(path, &attributes) == 0 && attributes.st_size > 2000) break;
            path[0] = 0;
        }
    }
    if (handle)
        closedir(handle);
    CHECK(path[0] != 0);
    {
        int fd = open(path, O_WRONLY);
        uint8_t byte = 0x7f;
        CHECK(fd >= 0 && pwrite(fd, &byte, 1, 600) == 1);
        close(fd);
    }
    CHECK(yozora_httpcache_load(cache, stored.url, &loaded) == YOZORA_CORRUPT);
    CHECK(yozora_httpcache_load(cache, stored.url, &loaded) == YOZORA_NOT_FOUND);
    CHECK(access(path, F_OK) != 0);
    /* a truncated file too */
    CHECK(yozora_httpcache_store(cache, &stored) == YOZORA_OK);
    CHECK(truncate(path, 100) == 0);
    CHECK(yozora_httpcache_load(cache, stored.url, &loaded) == YOZORA_CORRUPT);
    yozora_cache_entry_release(&stored);

    CHECK(yozora_httpcache_clear(cache) == YOZORA_OK);
    CHECK(count_entries(directory) == 0);
    yozora_httpcache_close(cache);
}

static void test_eviction(const char *root)
{
    char directory[512], url[64];
    yozora_httpcache *cache = NULL;
    yozora_cache_entry entry, loaded;
    int i;
    snprintf(directory, sizeof directory, "%s/evict", root);
    CHECK(mkdir(directory, 0700) == 0);
    CHECK(yozora_httpcache_open(&cache, directory, 1024 * 1024) == YOZORA_OK);
    for (i = 0; i < 12; ++i) {
        snprintf(url, sizeof url, "https://example.test/%d", i);
        make_entry(&entry, url, 150 * 1024, (uint8_t)i);
        CHECK(yozora_httpcache_store(cache, &entry) == YOZORA_OK);
        yozora_cache_entry_release(&entry);
        /* entry 0 keeps being used, so it must outlive older untouched ones */
        if (i > 0 && i < 11) {
            CHECK(yozora_httpcache_load(cache, "https://example.test/0", &loaded) == YOZORA_OK);
            yozora_cache_entry_release(&loaded);
        }
    }
    CHECK(yozora_httpcache_load(cache, "https://example.test/0", &loaded) == YOZORA_OK);
    yozora_cache_entry_release(&loaded);
    CHECK(yozora_httpcache_load(cache, "https://example.test/11", &loaded) == YOZORA_OK);
    yozora_cache_entry_release(&loaded);
    CHECK(yozora_httpcache_load(cache, "https://example.test/1", &loaded) == YOZORA_NOT_FOUND);
    {
        size_t count = count_entries(directory);
        CHECK(count >= 3 && count <= 6);
    }
    yozora_httpcache_close(cache);
    /* the size limit also holds for what an earlier run left behind */
    CHECK(yozora_httpcache_open(&cache, directory, 1024 * 1024) == YOZORA_OK);
    yozora_httpcache_close(cache);
}

static void test_open_rules(const char *root)
{
    char directory[512], temporary[600];
    yozora_httpcache *cache = NULL;
    int fd;
    CHECK(yozora_httpcache_open(&cache, "relative", 1024 * 1024) == YOZORA_INVALID && !cache);
    snprintf(directory, sizeof directory, "%s/missing", root);
    CHECK(yozora_httpcache_open(&cache, directory, 1024 * 1024) == YOZORA_IO);
    snprintf(directory, sizeof directory, "%s/open", root);
    CHECK(mkdir(directory, 0755) == 0);
    CHECK(yozora_httpcache_open(&cache, directory, 1024 * 1024) == YOZORA_IO);
    CHECK(chmod(directory, 0700) == 0);
    CHECK(yozora_httpcache_open(&cache, directory, 1000) == YOZORA_INVALID);
    CHECK(yozora_httpcache_open(&cache, directory, 2048ull * 1024 * 1024) == YOZORA_INVALID);
    CHECK(yozora_httpcache_open(NULL, directory, 1024 * 1024) == YOZORA_INVALID);
    /* an interrupted write leaves a temporary file, the next open removes it */
    snprintf(temporary, sizeof temporary, "%s/.tmp-1-1", directory);
    fd = open(temporary, O_WRONLY | O_CREAT, 0600);
    CHECK(fd >= 0);
    close(fd);
    CHECK(yozora_httpcache_open(&cache, directory, 1024 * 1024) == YOZORA_OK);
    CHECK(access(temporary, F_OK) != 0);
    yozora_httpcache_close(cache);
}

static void test_budget(void)
{
    yozora_byte_budget budget;
    yozora_byte_budget_init(&budget, 100);
    CHECK(yozora_byte_budget_reserve(&budget, 60));
    CHECK(!yozora_byte_budget_reserve(&budget, 41));
    CHECK(yozora_byte_budget_reserve(&budget, 40));
    CHECK(!yozora_byte_budget_reserve(&budget, 1));
    yozora_byte_budget_release(&budget, 60);
    CHECK(yozora_byte_budget_reserve(&budget, 60));
    CHECK(!yozora_byte_budget_reserve(&budget, (size_t)-1));
    yozora_byte_budget_release(&budget, 100);
    CHECK(yozora_byte_budget_reserve(&budget, 100));
    CHECK(!yozora_byte_budget_reserve(&budget, 101));
}

int main(void)
{
    char root[] = "/tmp/yozora-httpcache-XXXXXX";
    char command[600];
    if (!mkdtemp(root)) {
        perror("mkdtemp");
        return 2;
    }
    test_dates();
    test_budget();
    test_header_lookup();
    test_policy();
    test_decision();
    test_store(root);
    test_eviction(root);
    test_open_rules(root);
    snprintf(command, sizeof command, "rm -rf '%s'", root);
    if (system(command) != 0)
        fprintf(stderr, "test_httpcache: could not remove %s\n", root);
    if (failures) {
        fprintf(stderr, "test_httpcache: %d failures\n", failures);
        return 1;
    }
    puts("test_httpcache: passed");
    return 0;
}
