#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yozora_icons.h"

static yozora_icon_link links[YOZORA_ICON_LINKS_MAX];

static size_t scan(const char *html, const char *base)
{
    memset(links, 0, sizeof links);
    return yozora_icon_links(html, strlen(html), base, links, YOZORA_ICON_LINKS_MAX);
}

static void declared(void)
{
    /* a head like the one of a large site: a vector icon, small and large pngs, a home screen picture */
    const char *page =
        "<!doctype html><html><head><meta charset=utf-8><title>x</title>"
        "<link rel=\"mask-icon\" href=\"/mask.svg\" color=\"#000\">"
        "<link rel=\"icon\" href=\"/favicon.svg\" type=\"image/svg+xml\">"
        "<link rel=\"icon\" sizes=\"32x32\" href=\"/f32.png\">"
        "<link rel=\"icon\" sizes=\"192x192\" href=\"/f192.png\">"
        "<link rel=\"apple-touch-icon\" sizes=\"180x180\" href=\"/touch.png\">"
        "</head><body><link rel=icon href=/late.png></body></html>";
    size_t count = scan(page, "https://example.org/a/b");
    assert(count == 3);
    assert(!strcmp(links[0].url, "https://example.org/f192.png") || !strcmp(links[0].url, "https://example.org/touch.png"));
    /* 192 beats 180 by size */
    assert(!strcmp(links[0].url, "https://example.org/f192.png") && links[0].size == 192 && !links[0].touch);
    assert(!strcmp(links[1].url, "https://example.org/touch.png") && links[1].size == 180 && links[1].touch);
    assert(!strcmp(links[2].url, "https://example.org/f32.png") && links[2].size == 32);
}

static void tie_and_unknown(void)
{
    /* equal size: the home screen picture first. no sizes: a touch icon counts as 120, a plain icon as 32 */
    size_t count = scan("<link rel=icon sizes=64x64 href=a.png><link rel=apple-touch-icon sizes=64x64 href=b.png>"
                        "<link rel=icon href=c.png><link rel=\"apple-touch-icon-precomposed\" href=d.png>", "http://h/dir/");
    assert(count == 4);
    assert(!strcmp(links[0].url, "http://h/dir/d.png") && links[0].touch && links[0].size == 0);
    assert(!strcmp(links[1].url, "http://h/dir/b.png"));
    assert(!strcmp(links[2].url, "http://h/dir/a.png"));
    assert(!strcmp(links[3].url, "http://h/dir/c.png"));
}

static void syntax(void)
{
    size_t count;
    /* upper case tags, single quotes, no quotes, shortcut icon, several sizes, an escaped query */
    count = scan("<LINK REL='Shortcut Icon' HREF='/x.ico?a=1&amp;b=2' SIZES='16x16 48x48'>", "https://h.org/");
    assert(count == 1 && !strcmp(links[0].url, "https://h.org/x.ico?a=1&b=2") && links[0].size == 48);
    count = scan("<link href=/y.png rel=icon />", "https://h.org/");
    assert(count == 1 && !strcmp(links[0].url, "https://h.org/y.png"));
    count = scan("<link\nrel=\"icon\"\nhref=\"//cdn.h.org/z.png\">", "https://h.org/p");
    assert(count == 1 && !strcmp(links[0].url, "https://cdn.h.org/z.png"));
    /* a vector address by name, a data address and a stylesheet are not icons */
    assert(scan("<link rel=icon href=/v.svg?x=1><link rel=icon href=\"data:image/png;base64,AAAA\"><link rel=stylesheet href=/s.css>", "https://h.org/") == 0);
    assert(scan("<link rel=icon type=\"image/svg+xml\" href=/v.png>", "https://h.org/") == 0);
    /* the same address twice is one entry */
    assert(scan("<link rel=icon href=/a.png><link rel=\"apple-touch-icon\" href=\"/a.png\">", "https://h.org/") == 1);
}

static void not_markup(void)
{
    /* comments, scripts and styles hold text that looks like tags, the body is not read */
    assert(scan("<!-- <link rel=icon href=/c.png> --><script>var s='<link rel=icon href=/s.png>';</script>"
                "<style>/* <link rel=icon href=/t.png> */</style><link rel=icon href=/real.png>", "https://h.org/") == 1);
    assert(!strcmp(links[0].url, "https://h.org/real.png"));
    assert(scan("<head></head><link rel=icon href=/late.png>", "https://h.org/") == 0);
    assert(scan("<HEAD><BODY><link rel=icon href=/late.png>", "https://h.org/") == 0);
}

static void malformed(void)
{
    assert(scan("", "https://h.org/") == 0);
    assert(scan("<link rel=icon href=/cut.png", "https://h.org/") == 0);
    assert(scan("<link rel=icon href=\"/unclosed.png>", "https://h.org/") == 0);
    assert(scan("<link rel=icon>", "https://h.org/") == 0);
    assert(scan("<link href=/norel.png>", "https://h.org/") == 0);
    assert(scan("<link rel=icon href=''>", "https://h.org/") == 0);
    /* a base that is not http or https resolves nothing */
    assert(scan("<link rel=icon href=/a.png>", "ftp://h.org/") == 0);
    assert(yozora_icon_links(NULL, 0, "https://h.org/", links, 1) == 0);
    assert(yozora_icon_links("<link rel=icon href=/a.png>", 26, NULL, links, 1) == 0);
    assert(yozora_icon_links("<link rel=icon href=/a.png>", 26, "https://h.org/", links, 0) == 0);
}

static void bounds(void)
{
    /* more tags than the table holds, ranked by size: the largest are kept */
    char *page = malloc(70 * 80 + 1);
    assert(page);
    page[0] = 0;
    for (unsigned i = 1; i <= 70; ++i) {
        char tag[96];
        snprintf(tag, sizeof tag, "<link rel=icon sizes=%ux%u href=/i%u.png>", i, i, i);
        strcat(page, tag);
    }
    size_t count = scan(page, "https://h.org/");
    assert(count == YOZORA_ICON_LINKS_MAX);
    assert(links[0].size >= links[YOZORA_ICON_LINKS_MAX - 1].size);
    /* the first 64 tags are candidates, the rest are not read */
    assert(links[0].size == 64);
    /* a capacity of one gives the best one */
    assert(yozora_icon_links(page, strlen(page), "https://h.org/", links, 1) == 1 && links[0].size == 64);
    /* an address that does not fit is left out, a long attribute is not copied */
    char *long_href = malloc(3000);
    assert(long_href);
    memset(long_href, 'a', 2990);
    long_href[2990] = 0;
    char *tag = malloc(3100);
    assert(tag);
    snprintf(tag, 3100, "<link rel=icon href=/%s.png>", long_href);
    assert(scan(tag, "https://h.org/") == 0);
    free(tag);
    free(long_href);
    free(page);
    /* the read stops at the scan bound: a tag past it does not exist */
    size_t size = YOZORA_ICON_SCAN_MAX + 64;
    char *big = malloc(size + 1);
    assert(big);
    memset(big, ' ', size);
    memcpy(big + YOZORA_ICON_SCAN_MAX + 8, "<link rel=icon href=/far.png>", 29);
    big[size] = 0;
    memset(links, 0, sizeof links);
    assert(yozora_icon_links(big, size, "https://h.org/", links, YOZORA_ICON_LINKS_MAX) == 0);
    free(big);
}

int main(void)
{
    declared();
    tie_and_unknown();
    syntax();
    not_markup();
    malformed();
    bounds();
    puts("icons: declared links, ranking, syntax, comments and scripts, malformed input and bounds passed");
    return 0;
}
