#include "yozora_address.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YOZORA_ADDRESS_TEST_CAPACITY 4096

typedef struct {
    const char *input;
    yozora_address_kind kind;
    const char *output;
} resolve_case;

static const resolve_case resolve_cases[] = {
    {"example.com", YOZORA_ADDRESS_URL, "https://example.com"},
    {"  example.com/path?x=1  ", YOZORA_ADDRESS_URL, "https://example.com/path?x=1"},
    {"example.com:8443/a#b", YOZORA_ADDRESS_URL, "https://example.com:8443/a#b"},
    {"http://a.b", YOZORA_ADDRESS_URL, "http://a.b"},
    {"HTTPS://A.B/x", YOZORA_ADDRESS_URL, "HTTPS://A.B/x"},
    {"https://", YOZORA_ADDRESS_INVALID, ""},
    {"http://", YOZORA_ADDRESS_INVALID, ""},
    {"localhost", YOZORA_ADDRESS_URL, "http://localhost"},
    {"localhost:8080/x", YOZORA_ADDRESS_URL, "http://localhost:8080/x"},
    {"192.168.1.10", YOZORA_ADDRESS_URL, "http://192.168.1.10"},
    {"192.168.1.10:8080/a", YOZORA_ADDRESS_URL, "http://192.168.1.10:8080/a"},
    {"printer.local/status", YOZORA_ADDRESS_URL, "http://printer.local/status"},
    {"[::1]:80", YOZORA_ADDRESS_URL, "http://[::1]:80"},
    {"file.txt", YOZORA_ADDRESS_URL, "https://file.txt"},
    {"1.2.3", YOZORA_ADDRESS_SEARCH, "https://duckduckgo.com/?q=1.2.3"},
    {"3.14", YOZORA_ADDRESS_SEARCH, "https://duckduckgo.com/?q=3.14"},
    {"999.1.1.1.1", YOZORA_ADDRESS_SEARCH, "https://duckduckgo.com/?q=999.1.1.1.1"},
    {"x.y.", YOZORA_ADDRESS_SEARCH, "https://duckduckgo.com/?q=x.y."},
    {"host:99999", YOZORA_ADDRESS_SEARCH, "https://duckduckgo.com/?q=host%3A99999"},
    {"hello", YOZORA_ADDRESS_SEARCH, "https://duckduckgo.com/?q=hello"},
    {"hello world", YOZORA_ADDRESS_SEARCH, "https://duckduckgo.com/?q=hello+world"},
    {"a&b=c d", YOZORA_ADDRESS_SEARCH, "https://duckduckgo.com/?q=a%26b%3Dc+d"},
    {"привет", YOZORA_ADDRESS_SEARCH, "https://duckduckgo.com/?q=%D0%BF%D1%80%D0%B8%D0%B2%D0%B5%D1%82"},
    {"javascript:alert(1)", YOZORA_ADDRESS_SEARCH, "https://duckduckgo.com/?q=javascript%3Aalert%281%29"},
    {"ftp://x.y", YOZORA_ADDRESS_INVALID, ""},
    {"file:///etc/passwd", YOZORA_ADDRESS_INVALID, ""},
    {"", YOZORA_ADDRESS_INVALID, ""},
    {"   \t ", YOZORA_ADDRESS_INVALID, ""},
    {"a\x01" "b", YOZORA_ADDRESS_INVALID, ""},
};

typedef struct {
    const char *url;
    int found;
    const char *host;
} host_case;

static const host_case host_cases[] = {
    {"https://www.example.com/a", 1, "example.com"},
    {"http://example.com", 1, "example.com"},
    {"HTTPS://Example.COM:8443/x", 1, "Example.COM"},
    {"http://user:pw@host.org:8080/x", 1, "host.org"},
    {"https://[::1]/", 1, "[::1]"},
    {"https://www.", 1, "www."},
    {"https:///", 0, ""},
    {"ftp://x.y", 0, ""},
    {"example.com", 0, ""},
    {"", 0, ""},
};

typedef struct {
    const char *url;
    const char *page;
    yozora_external_kind kind;
} external_case;

static const external_case external_cases[] = {
    {"tg://resolve?domain=durov", "https://example.com/", YOZORA_EXTERNAL_SCHEME},
    {"mailto:a@b.c", "https://example.com/", YOZORA_EXTERNAL_SCHEME},
    {"tel:+123456", NULL, YOZORA_EXTERNAL_SCHEME},
    {"itms-apps://apps.apple.com/app/id1", "", YOZORA_EXTERNAL_SCHEME},
    {"TG://x", "https://example.com/", YOZORA_EXTERNAL_SCHEME},
    {"a+b-c.d:x", "https://example.com/", YOZORA_EXTERNAL_SCHEME},
    {"javascript:alert(1)", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"JavaScript:alert(1)", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"data:text/html,x", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"file:///etc/passwd", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"blob:https://example.com/1", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"about:blank", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"view-source:https://example.com", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"wss://example.com/s", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"ftp://example.com/", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"itms-services://?action=download-manifest&url=x", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"1tg://x", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"t g://x", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"tg//x", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"://x", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"tg://x\ny", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"tg://x\x7f", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"https://youtube.com/watch?v=1", "https://example.com/", YOZORA_EXTERNAL_UNIVERSAL},
    {"HTTP://YouTube.com/", NULL, YOZORA_EXTERNAL_UNIVERSAL},
    {"https://youtube.com/a", "https://www.youtube.com/b", YOZORA_EXTERNAL_NONE},
    {"https://www.youtube.com/a", "https://YOUTUBE.com/b", YOZORA_EXTERNAL_NONE},
    {"https://m.youtube.com/a", "https://youtube.com/b", YOZORA_EXTERNAL_UNIVERSAL},
    {"https://user:pw@youtube.com/", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"https://@youtube.com/", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"https:///", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"https://youtube.com/\x01", "https://example.com/", YOZORA_EXTERNAL_NONE},
    {"https://youtube.com/a", "about:blank", YOZORA_EXTERNAL_UNIVERSAL},
};

int main(void) {
    char out[YOZORA_ADDRESS_TEST_CAPACITY];
    char small[12];
    char *large;
    size_t i;
    for (i = 0; i < sizeof resolve_cases / sizeof resolve_cases[0]; ++i) {
        yozora_address_kind kind = yozora_address_resolve(resolve_cases[i].input, YOZORA_SEARCH_DUCKDUCKGO, out, sizeof out);
        if (kind != resolve_cases[i].kind || strcmp(out, resolve_cases[i].output)) {
            fprintf(stderr, "resolve case %zu failed: kind %d output '%s'\n", i, (int)kind, out);
            return 1;
        }
    }
    for (i = 0; i < sizeof host_cases / sizeof host_cases[0]; ++i) {
        int found = yozora_address_host(host_cases[i].url, out, sizeof out);
        if (found != host_cases[i].found || strcmp(out, host_cases[i].host)) {
            fprintf(stderr, "host case %zu failed: found %d output '%s'\n", i, found, out);
            return 1;
        }
    }
    for (i = 0; i < sizeof external_cases / sizeof external_cases[0]; ++i) {
        yozora_external_kind kind = yozora_address_external(external_cases[i].url, external_cases[i].page);
        if (kind != external_cases[i].kind) {
            fprintf(stderr, "external case %zu failed: kind %d\n", i, (int)kind);
            return 1;
        }
    }
    assert(yozora_address_external(NULL, NULL) == YOZORA_EXTERNAL_NONE);
    /* the longest scheme the host buffer holds is still judged, a longer one is malformed */
    large = (char *)malloc(400);
    assert(large);
    memset(large, 'a', 300);
    strcpy(large + 300, "://x");
    assert(yozora_address_external(large, NULL) == YOZORA_EXTERNAL_NONE);
    free(large);
    /* the default core is Google, an unknown value searches with it too */
    assert(yozora_address_resolve("hello world", YOZORA_SEARCH_GOOGLE, out, sizeof out) == YOZORA_ADDRESS_SEARCH);
    assert(!strcmp(out, "https://www.google.com/search?q=hello+world"));
    assert(yozora_address_resolve("a&b", (yozora_search_engine)99, out, sizeof out) == YOZORA_ADDRESS_SEARCH);
    assert(!strcmp(out, "https://www.google.com/search?q=a%26b"));
    assert(yozora_address_resolve("привет мир", YOZORA_SEARCH_YANDEX, out, sizeof out) == YOZORA_ADDRESS_SEARCH);
    assert(!strcmp(out, "https://yandex.ru/search/?text=%D0%BF%D1%80%D0%B8%D0%B2%D0%B5%D1%82+%D0%BC%D0%B8%D1%80"));
    assert(yozora_address_resolve("a&b", YOZORA_SEARCH_DUCKDUCKGO, out, sizeof out) == YOZORA_ADDRESS_SEARCH);
    assert(!strcmp(out, "https://duckduckgo.com/?q=a%26b"));
    assert(yozora_address_resolve("example.com", YOZORA_SEARCH_GOOGLE, out, sizeof out) == YOZORA_ADDRESS_URL);
    assert(!strcmp(out, "https://example.com"));
    /* a result that does not fit is rejected, never cut */
    assert(yozora_address_resolve("example.com", YOZORA_SEARCH_DUCKDUCKGO, small, sizeof small) == YOZORA_ADDRESS_INVALID);
    assert(small[0] == '\0');
    assert(yozora_address_resolve("example.com", YOZORA_SEARCH_DUCKDUCKGO, small, 20 > sizeof small ? sizeof small : 20) == YOZORA_ADDRESS_INVALID);
    assert(yozora_address_resolve("hello", YOZORA_SEARCH_DUCKDUCKGO, small, sizeof small) == YOZORA_ADDRESS_INVALID);
    assert(yozora_address_resolve("example.com", YOZORA_SEARCH_DUCKDUCKGO, out, 20) == YOZORA_ADDRESS_URL);
    assert(yozora_address_resolve("example.com", YOZORA_SEARCH_DUCKDUCKGO, out, 19) == YOZORA_ADDRESS_INVALID);
    assert(yozora_address_host("https://example.com", small, 12) == 1);
    assert(yozora_address_host("https://example.com", small, 11) == 0);
    assert(small[0] == '\0');
    assert(yozora_address_resolve(NULL, YOZORA_SEARCH_DUCKDUCKGO, out, sizeof out) == YOZORA_ADDRESS_INVALID);
    assert(yozora_address_resolve("a.b", YOZORA_SEARCH_DUCKDUCKGO, NULL, 10) == YOZORA_ADDRESS_INVALID);
    assert(yozora_address_resolve("a.b", YOZORA_SEARCH_DUCKDUCKGO, out, 0) == YOZORA_ADDRESS_INVALID);
    assert(yozora_address_host(NULL, out, sizeof out) == 0);
    /* the longest address that fits is accepted, one more byte is not */
    large = malloc(YOZORA_ADDRESS_TEST_CAPACITY + 8);
    assert(large);
    memset(large, 'a', YOZORA_ADDRESS_TEST_CAPACITY + 7);
    memcpy(large, "http://", 7);
    large[YOZORA_ADDRESS_TEST_CAPACITY - 1] = '\0';
    assert(yozora_address_resolve(large, YOZORA_SEARCH_DUCKDUCKGO, out, sizeof out) == YOZORA_ADDRESS_URL);
    large[YOZORA_ADDRESS_TEST_CAPACITY - 1] = 'a';
    large[YOZORA_ADDRESS_TEST_CAPACITY] = '\0';
    assert(yozora_address_resolve(large, YOZORA_SEARCH_DUCKDUCKGO, out, sizeof out) == YOZORA_ADDRESS_INVALID);
    free(large);
    puts("address: urls, searches, hosts, encoding and capacity limits passed");
    return 0;
}
