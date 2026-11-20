#include "yozora_blocklist.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_blocklist: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static int matches(const char *host)
{
    return yozora_blocklist_matches(host, strlen(host));
}

static void test_table(void)
{
    size_t count = yozora_blocklist_count();
    CHECK(count > 50 && count < 512);
    CHECK(yozora_blocklist_entry(count) == NULL);
    for (size_t i = 0; i < count; ++i) {
        const char *entry = yozora_blocklist_entry(i);
        CHECK(entry != NULL);
        if (!entry)
            continue;
        size_t length = strlen(entry);
        CHECK(length > 3 && length <= 253);
        CHECK(strchr(entry, '.') != NULL);
        CHECK(entry[0] != '.' && entry[length - 1] != '.');
        for (size_t j = 0; j < length; ++j) {
            char c = entry[j];
            CHECK((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-');
        }
        /* the lookup is a binary search over strcmp order */
        if (i)
            CHECK(strcmp(yozora_blocklist_entry(i - 1), entry) < 0);
        CHECK(yozora_blocklist_matches(entry, length) == 1);
    }
}

static void test_matching(void)
{
    CHECK(matches("doubleclick.net"));
    CHECK(matches("stats.g.doubleclick.net"));
    CHECK(matches("www.google-analytics.com"));
    CHECK(matches("WWW.Google-Analytics.COM"));
    /* a fully qualified name with a trailing dot is the same host */
    CHECK(matches("doubleclick.net."));
    CHECK(matches("a.b.c.d.scorecardresearch.com"));
    /* a rule for one subdomain does not cover its parent or its siblings */
    CHECK(matches("collector.github.com"));
    CHECK(matches("x.collector.github.com"));
    CHECK(!matches("github.com"));
    CHECK(!matches("api.github.com"));
    CHECK(matches("ad.mail.ru"));
    CHECK(!matches("mail.ru"));
    CHECK(!matches("e.mail.ru"));
    CHECK(!matches("yandex.ru"));
    CHECK(matches("mc.yandex.ru"));
}

static void test_rejections(void)
{
    /* only whole labels match: a longer name that ends in the same letters is another site */
    CHECK(!matches("notdoubleclick.net"));
    CHECK(!matches("doubleclick.net.example.com"));
    CHECK(!matches("doubleclick.network"));
    CHECK(!matches("example.com"));
    CHECK(!matches("net"));
    CHECK(!matches("com"));
    CHECK(!matches("localhost"));
    CHECK(!matches("127.0.0.1"));
    CHECK(!matches("[::1]"));
    CHECK(!matches(""));
    CHECK(!matches("."));
    CHECK(!matches(".."));
    CHECK(yozora_blocklist_matches(NULL, 5) == 0);
    CHECK(yozora_blocklist_matches("doubleclick.net", 0) == 0);
}

static void test_bounds(void)
{
    char host[400];
    /* the stated length wins over the terminator, and a NUL inside the host never matches */
    CHECK(yozora_blocklist_matches("doubleclick.net.evil", 15) == 1);
    CHECK(yozora_blocklist_matches("example.com\0doubleclick.net", 27) == 0);
    memset(host, 'a', sizeof(host));
    memcpy(host + sizeof(host) - 16, ".doubleclick.net", 16);
    host[sizeof(host) - 1] = 't';
    CHECK(yozora_blocklist_matches(host, sizeof(host)) == 0);
    /* 253 bytes is the longest valid name */
    memset(host, 'a', 253);
    memcpy(host + 253 - 15, "doubleclick.net", 15);
    host[253 - 16] = '.';
    CHECK(yozora_blocklist_matches(host, 253) == 1);
    CHECK(yozora_blocklist_matches(host, 254) == 0);
}

int main(void)
{
    test_table();
    test_matching();
    test_rejections();
    test_bounds();
    if (failures) {
        fprintf(stderr, "test_blocklist: %d failures\n", failures);
        return 1;
    }
    puts("test_blocklist: passed");
    return 0;
}
