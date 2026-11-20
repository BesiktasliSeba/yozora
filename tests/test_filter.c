#define _POSIX_C_SOURCE 200809L
#include "yozora_filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_filter: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

/* the host of an address, for the request */
static void host_of(const char *url, char *host, size_t capacity)
{
    const char *start = strstr(url, "://");
    size_t n = 0;
    start = start ? start + 3 : url;
    {
        /* the host of WebCore has no user information */
        const char *at = strchr(start, '@');
        const char *slash = strchr(start, '/');
        if (at && (!slash || at < slash))
            start = at + 1;
    }
    while (start[n] && start[n] != '/' && start[n] != '?' && start[n] != ':' && n + 1 < capacity) {
        host[n] = start[n];
        ++n;
    }
    host[n] = '\0';
}

static int blocks_as(const yozora_filter *filter, const char *url, const char *page, yozora_filter_type type, int third)
{
    char host[260];
    yozora_filter_request request;
    host_of(url, host, sizeof host);
    memset(&request, 0, sizeof request);
    request.url = url;
    request.url_length = strlen(url);
    request.host = host;
    request.host_length = strlen(host);
    request.page_host = page;
    request.page_host_length = page ? strlen(page) : 0;
    request.type = type;
    request.third_party = third;
    return yozora_filter_blocks(filter, &request);
}

static int blocks(const yozora_filter *filter, const char *url)
{
    return blocks_as(filter, url, "page.example", YOZORA_FILTER_SCRIPT, 1);
}

static yozora_filter *with(const char *text)
{
    yozora_filter *filter = yozora_filter_create();
    yozora_filter_add_text(filter, text, strlen(text), NULL);
    return filter;
}

static void test_anchors(void)
{
    yozora_filter *filter = with("||ads.example.com^\n||tracker.net/pixel\n|http://exact.example/a|\n/banner/ad.\n");
    CHECK(blocks(filter, "https://ads.example.com/x.js"));
    CHECK(blocks(filter, "https://sub.ads.example.com/x.js"));
    CHECK(blocks(filter, "https://ADS.Example.COM/X.JS"));
    CHECK(!blocks(filter, "https://notads.example.com/x.js"));
    CHECK(!blocks(filter, "https://example.com/ads.example.com/x.js"));
    CHECK(blocks(filter, "https://tracker.net/pixel?id=1"));
    CHECK(blocks(filter, "https://a.tracker.net/pixel"));
    CHECK(!blocks(filter, "https://tracker.net/other"));
    CHECK(blocks(filter, "http://exact.example/a"));
    CHECK(!blocks(filter, "http://exact.example/a?x"));
    CHECK(!blocks(filter, "https://exact.example/a"));
    CHECK(blocks(filter, "https://cdn.example/banner/ad.png"));
    CHECK(!blocks(filter, "https://cdn.example/banner/bd.png"));
    yozora_filter_destroy(filter);
}

static void test_wildcards(void)
{
    yozora_filter *filter = with("/ads/*/banner^\n||cdn.example/*.gif|\n/track^id\n");
    CHECK(blocks(filter, "https://a.example/ads/300/banner"));
    CHECK(blocks(filter, "https://a.example/ads/300/banner?x=1"));
    CHECK(blocks(filter, "https://a.example/ads/a/b/banner/z"));
    CHECK(!blocks(filter, "https://a.example/ads/300/banners"));
    CHECK(!blocks(filter, "https://a.example/ads/banner"));
    CHECK(blocks(filter, "https://cdn.example/a/b.gif"));
    CHECK(!blocks(filter, "https://cdn.example/a/b.gif?x"));
    /* ^ is a separator, not a letter or a digit or one of _ - . % */
    CHECK(blocks(filter, "https://a.example/track?id=1"));
    CHECK(blocks(filter, "https://a.example/track/id"));
    CHECK(!blocks(filter, "https://a.example/trackxid"));
    CHECK(!blocks(filter, "https://a.example/track-id"));
    yozora_filter_destroy(filter);
}

static void test_separator_at_end(void)
{
    yozora_filter *filter = with("||example.org/ad^\n");
    CHECK(blocks(filter, "https://example.org/ad"));
    CHECK(blocks(filter, "https://example.org/ad/"));
    CHECK(blocks(filter, "https://example.org/ad?x"));
    CHECK(!blocks(filter, "https://example.org/ads"));
    yozora_filter_destroy(filter);
}

static void test_options(void)
{
    yozora_filter *filter = with("||a.example^$third-party\n||b.example^$script\n||c.example^$~image\n||d.example^$domain=news.example|~sport.news.example\n||e.example^$image,font\n");
    CHECK(blocks_as(filter, "https://a.example/x", "p.example", YOZORA_FILTER_IMAGE, 1));
    CHECK(!blocks_as(filter, "https://a.example/x", "a.example", YOZORA_FILTER_IMAGE, 0));
    CHECK(blocks_as(filter, "https://b.example/x", "p.example", YOZORA_FILTER_SCRIPT, 1));
    CHECK(!blocks_as(filter, "https://b.example/x", "p.example", YOZORA_FILTER_IMAGE, 1));
    CHECK(!blocks_as(filter, "https://c.example/x", "p.example", YOZORA_FILTER_IMAGE, 1));
    CHECK(blocks_as(filter, "https://c.example/x", "p.example", YOZORA_FILTER_SCRIPT, 1));
    CHECK(blocks_as(filter, "https://d.example/x", "news.example", YOZORA_FILTER_SCRIPT, 1));
    CHECK(blocks_as(filter, "https://d.example/x", "m.news.example", YOZORA_FILTER_SCRIPT, 1));
    CHECK(!blocks_as(filter, "https://d.example/x", "sport.news.example", YOZORA_FILTER_SCRIPT, 1));
    CHECK(!blocks_as(filter, "https://d.example/x", "other.example", YOZORA_FILTER_SCRIPT, 1));
    CHECK(!blocks_as(filter, "https://d.example/x", NULL, YOZORA_FILTER_SCRIPT, 1));
    CHECK(blocks_as(filter, "https://e.example/x", "p.example", YOZORA_FILTER_FONT, 1));
    CHECK(!blocks_as(filter, "https://e.example/x", "p.example", YOZORA_FILTER_SCRIPT, 1));
    /* a rule does not touch the page itself unless it names the document */
    CHECK(!blocks_as(filter, "https://a.example/", "a.example", YOZORA_FILTER_DOCUMENT, 0));
    yozora_filter_destroy(filter);
}

static void test_exceptions(void)
{
    yozora_filter *filter = with("||ads.example.com^\n@@||ads.example.com/allowed^\n||doubleclick.net^\n@@||doubleclick.net^$domain=good.example\n/promo/\n@@/promo/ok$script\n");
    CHECK(blocks(filter, "https://ads.example.com/a.js"));
    CHECK(!blocks(filter, "https://ads.example.com/allowed/a.js"));
    /* an exception covers the built in hosts as well */
    CHECK(blocks_as(filter, "https://ad.doubleclick.net/x", "p.example", YOZORA_FILTER_SCRIPT, 1));
    CHECK(!blocks_as(filter, "https://ad.doubleclick.net/x", "good.example", YOZORA_FILTER_SCRIPT, 1));
    CHECK(blocks_as(filter, "https://a.example/promo/ok", "p.example", YOZORA_FILTER_IMAGE, 1));
    CHECK(!blocks_as(filter, "https://a.example/promo/ok", "p.example", YOZORA_FILTER_SCRIPT, 1));
    yozora_filter_destroy(filter);
}

static void test_site_exception(void)
{
    yozora_filter *filter = with("||ads.example.com^\n@@||trusted.example^$document\n##.banner\n");
    char css[256];
    CHECK(blocks_as(filter, "https://ads.example.com/a", "other.example", YOZORA_FILTER_SCRIPT, 1));
    CHECK(!blocks_as(filter, "https://ads.example.com/a", "trusted.example", YOZORA_FILTER_SCRIPT, 1));
    CHECK(!blocks_as(filter, "https://ads.example.com/a", "www.trusted.example", YOZORA_FILTER_SCRIPT, 1));
    CHECK(blocks_as(filter, "https://ads.example.com/a", "nottrusted.example", YOZORA_FILTER_SCRIPT, 1));
    CHECK(yozora_filter_hiding_css(filter, "trusted.example", 15, css, sizeof css) == 0);
    CHECK(yozora_filter_hiding_css(filter, "other.example", 13, css, sizeof css) > 0);
    yozora_filter_destroy(filter);
}

static void test_builtin(void)
{
    yozora_filter *filter = yozora_filter_create();
    CHECK(yozora_filter_add_builtin(filter) == 0);
    yozora_filter_finish(filter);
    CHECK(blocks(filter, "https://www.google-analytics.com/collect"));
    CHECK(blocks(filter, "https://stats.g.doubleclick.net/j"));
    CHECK(blocks(filter, "https://www.youtube.com/api/stats/ads?x=1"));
    CHECK(!blocks(filter, "https://www.youtube.com/api/stats/playback?x=1"));
    CHECK(!blocks(filter, "https://www.youtube.com/watch?v=abc"));
    CHECK(blocks(filter, "https://connect.facebook.net/en_US/fbevents.js"));
    CHECK(!blocks(filter, "https://connect.facebook.net/en_US/sdk.js"));
    CHECK(blocks(filter, "https://example.com/js/adsbygoogle.js"));
    CHECK(!blocks(filter, "https://example.com/js/app.js"));
    CHECK(!blocks(filter, "https://example.com/"));
    CHECK(yozora_filter_hiding_count(filter) > 5);
    yozora_filter_destroy(filter);
}

static void test_unsupported_and_invalid(void)
{
    yozora_filter *filter = yozora_filter_create();
    yozora_filter_report report;
    const char *text =
        "[Adblock Plus 2.0]\n"
        "! a comment\n"
        "\n"
        "||ok.example^\n"
        "/regex\\d+/\n"
        "||csp.example^$csp=script-src 'none'\n"
        "||red.example^$redirect=noopjs\n"
        "example.com#?#div:-abp-has(.x)\n"
        "example.com##div:has-text(Ad)\n"
        "example.com##.a{color:red}\n"
        "$third-party\n"
        "*\n"
        "||x.example^$domain=\n"
        "||x.example^$domain=a.com||b.com\n"
        "||y.example^$script,bogus\n";
    yozora_filter_add_text(filter, text, strlen(text), &report);
    CHECK(report.added == 1);
    CHECK(report.skipped == 3);
    CHECK(report.unsupported >= 4);
    CHECK(report.invalid >= 3);
    CHECK(report.dropped == 0);
    CHECK(blocks(filter, "https://ok.example/a"));
    CHECK(!blocks(filter, "https://red.example/a"));
    CHECK(!blocks(filter, "https://y.example/a"));
    CHECK(yozora_filter_hiding_count(filter) == 0);
    /* a line over the limit and a NUL inside a line are refused */
    {
        char long_line[YOZORA_FILTER_LINE_MAX + 8];
        memset(long_line, 'a', sizeof long_line);
        CHECK(yozora_filter_add_line(filter, long_line, sizeof long_line) == YOZORA_FILTER_INVALID);
        CHECK(yozora_filter_add_line(filter, "||a\0b.example^", 14) == YOZORA_FILTER_INVALID);
        CHECK(yozora_filter_add_line(NULL, "||a.example^", 12) == YOZORA_FILTER_INVALID);
    }
    yozora_filter_destroy(filter);
}

static void test_hiding(void)
{
    yozora_filter *filter = with("##.ad\nnews.example,~sport.news.example##div.promo\nnews.example#@#.ad\nother.example##.ad\n");
    char css[512];
    size_t n;
    n = yozora_filter_hiding_css(filter, "site.example", 12, css, sizeof css);
    CHECK(n == strlen(css));
    CHECK(strstr(css, ".ad{display:none!important}\n") != NULL);
    CHECK(strstr(css, "div.promo") == NULL);
    n = yozora_filter_hiding_css(filter, "news.example", 12, css, sizeof css);
    CHECK(strstr(css, "div.promo{display:none!important}") != NULL);
    /* the exception keeps .ad visible on that site */
    CHECK(strstr(css, ".ad{") == NULL);
    n = yozora_filter_hiding_css(filter, "sport.news.example", 18, css, sizeof css);
    CHECK(strstr(css, "div.promo") == NULL);
    CHECK(strstr(css, ".ad{") == NULL);
    n = yozora_filter_hiding_css(filter, "www.other.example", 17, css, sizeof css);
    CHECK(strstr(css, ".ad{") != NULL);
    /* a sheet that does not fit is cut after a whole rule */
    n = yozora_filter_hiding_css(filter, "other.example", 13, css, 20);
    CHECK(n < 20 && n == strlen(css));
    CHECK(n == 0 || css[n - 1] == '\n');
    CHECK(yozora_filter_hiding_css(filter, "other.example", 13, css, 1) == 0);
    CHECK(yozora_filter_hiding_css(filter, "other.example", 13, NULL, 10) == 0);
    CHECK(yozora_filter_hiding_css(filter, NULL, 0, css, sizeof css) == 0);
    yozora_filter_destroy(filter);
}

static void test_limits(void)
{
    yozora_filter *filter = yozora_filter_create();
    yozora_filter_report report;
    size_t capacity = 1024 * 1024;
    char *text = malloc(capacity);
    size_t used = 0;
    CHECK(text != NULL);
    if (!text)
        return;
    for (int i = 0; i < YOZORA_FILTER_MAX_RULES + 50; ++i)
        used += (size_t)snprintf(text + used, capacity - used, "/path%d/ad.js\n", i);
    yozora_filter_add_text(filter, text, used, &report);
    CHECK(report.added == YOZORA_FILTER_MAX_RULES);
    CHECK(report.dropped == 50);
    CHECK(yozora_filter_rule_count(filter) == YOZORA_FILTER_MAX_RULES);
    CHECK(blocks(filter, "https://a.example/path7/ad.js"));
    CHECK(!blocks(filter, "https://a.example/path99999/ad.js"));
    used = 0;
    for (int i = 0; i < YOZORA_FILTER_MAX_HIDING + 10; ++i)
        used += (size_t)snprintf(text + used, capacity - used, "##.ad%d\n", i);
    yozora_filter_add_text(filter, text, used, &report);
    CHECK(report.added == YOZORA_FILTER_MAX_HIDING);
    CHECK(report.dropped == 10);
    free(text);
    yozora_filter_destroy(filter);
}

static void test_request_bounds(void)
{
    yozora_filter *filter = with("||ads.example^\n/tail-marker\n");
    char url[YOZORA_FILTER_URL_MAX * 2];
    yozora_filter_request request;
    memset(&request, 0, sizeof request);
    CHECK(!yozora_filter_blocks(NULL, &request));
    CHECK(!yozora_filter_blocks(filter, NULL));
    CHECK(!yozora_filter_blocks(filter, &request));
    /* only the first part of a very long address is looked at */
    memcpy(url, "https://a.example/", 18);
    memset(url + 18, 'q', sizeof url - 18);
    memcpy(url + sizeof url - 13, "/tail-marker", 12);
    url[sizeof url - 1] = '\0';
    CHECK(!blocks(filter, url));
    memcpy(url, "https://ads.example/", 20);
    CHECK(blocks_as(filter, url, "p.example", YOZORA_FILTER_SCRIPT, 1));
    /* a NUL inside the address ends the look */
    CHECK(yozora_filter_host_in_domain("a.b.example", 11, "b.example", 9));
    CHECK(!yozora_filter_host_in_domain("ab.example", 10, "b.example", 9));
    CHECK(yozora_filter_host_in_domain("B.EXAMPLE", 9, "b.example", 9));
    CHECK(!yozora_filter_host_in_domain("example", 7, "b.example", 9));
    yozora_filter_destroy(filter);
}

static void test_ports_and_userinfo(void)
{
    yozora_filter *filter = with("||ads.example^\n");
    CHECK(blocks(filter, "https://ads.example:8443/a"));
    CHECK(blocks(filter, "https://user:pw@ads.example/a"));
    CHECK(!blocks(filter, "https://a.example/?u=https://ads.example/"));
    yozora_filter_destroy(filter);
}

int main(void)
{
    test_anchors();
    test_wildcards();
    test_separator_at_end();
    test_options();
    test_exceptions();
    test_site_exception();
    test_builtin();
    test_unsupported_and_invalid();
    test_hiding();
    test_limits();
    test_request_bounds();
    test_ports_and_userinfo();
    if (failures) {
        fprintf(stderr, "test_filter: %d failures\n", failures);
        return 1;
    }
    puts("test_filter: passed");
    return 0;
}
