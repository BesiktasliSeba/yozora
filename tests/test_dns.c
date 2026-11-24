#include "yozora_dns.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition, ...)                                                              \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            printf("FAIL line %d: ", __LINE__);                                            \
            printf(__VA_ARGS__);                                                           \
            printf("\n");                                                                  \
            ++failures;                                                                    \
        }                                                                                  \
    } while (0)

typedef struct {
    const char *input;
    yozora_dns_status status;
    const char *url;
} normalize_case;

static const normalize_case normalize_cases[] = {
    /* the system resolver */
    {"", YOZORA_DNS_EMPTY, ""},
    {"   ", YOZORA_DNS_EMPTY, ""},
    {"\t\n", YOZORA_DNS_EMPTY, ""},
    /* the known public resolvers, by any of their addresses */
    {"1.1.1.1", YOZORA_DNS_OK, "https://cloudflare-dns.com/dns-query"},
    {"1.0.0.1", YOZORA_DNS_OK, "https://cloudflare-dns.com/dns-query"},
    {" 8.8.8.8 ", YOZORA_DNS_OK, "https://dns.google/dns-query"},
    {"8.8.4.4", YOZORA_DNS_OK, "https://dns.google/dns-query"},
    {"9.9.9.9", YOZORA_DNS_OK, "https://dns.quad9.net/dns-query"},
    {"149.112.112.112", YOZORA_DNS_OK, "https://dns.quad9.net/dns-query"},
    {"94.140.14.14", YOZORA_DNS_OK, "https://dns.adguard-dns.com/dns-query"},
    {"94.140.15.15", YOZORA_DNS_OK, "https://dns.adguard-dns.com/dns-query"},
    /* any other address or name is asked at /dns-query */
    {"77.88.8.8", YOZORA_DNS_OK, "https://77.88.8.8/dns-query"},
    {"dns.example.org", YOZORA_DNS_OK, "https://dns.example.org/dns-query"},
    {"DNS.Example.ORG", YOZORA_DNS_OK, "https://dns.example.org/dns-query"},
    {"dns.example.org:8443", YOZORA_DNS_OK, "https://dns.example.org:8443/dns-query"},
    {"2606:4700:4700::1111", YOZORA_DNS_OK, "https://[2606:4700:4700::1111]/dns-query"},
    {"[2606:4700:4700::1111]", YOZORA_DNS_OK, "https://[2606:4700:4700::1111]/dns-query"},
    {"[::1]:8443", YOZORA_DNS_OK, "https://[::1]:8443/dns-query"},
    {"::1", YOZORA_DNS_OK, "https://[::1]/dns-query"},
    {"1:2:3:4:5:6:7:8", YOZORA_DNS_OK, "https://[1:2:3:4:5:6:7:8]/dns-query"},
    /* an https address keeps its path and gets the usual one when it has none */
    {"https://dns.google/dns-query", YOZORA_DNS_OK, "https://dns.google/dns-query"},
    {"https://dns.google", YOZORA_DNS_OK, "https://dns.google/dns-query"},
    {"https://dns.google/", YOZORA_DNS_OK, "https://dns.google/"},
    {"HTTPS://Doh.Example.net/Custom/Path", YOZORA_DNS_OK, "https://doh.example.net/Custom/Path"},
    {"https://doh.example.net:444/q", YOZORA_DNS_OK, "https://doh.example.net:444/q"},
    {"https://1.1.1.1/dns-query", YOZORA_DNS_OK, "https://1.1.1.1/dns-query"},
    /* what must not be asked */
    {"http://dns.google/dns-query", YOZORA_DNS_INSECURE, ""},
    {"HTTP://1.1.1.1", YOZORA_DNS_INSECURE, ""},
    {"tls://dns.google", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"ftp://dns.google/x", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"cloudflare", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"localhost", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"1.1.1", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"1.1.1.1.1", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"256.1.1.1", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"01.1.1.1", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"1.1.1.", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"dns..google", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"-dns.google", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"dns-.google", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"dns.google-", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"dns_google.com", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"dns.google:0", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"dns.google:65536", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"dns.google:80a", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"dns.google:", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"user@dns.google", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"https://user:pass@dns.google/dns-query", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"https://dns.google/dns-query?dns=x", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"https://dns.google/dns-query#x", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"https://dns.google/a b", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"https://dns.google/\\x", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"https:///dns-query", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"https://", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"/dns-query", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"dns.google dns.example", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"dns.g\xc3\xb6ogle.com", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"[::1", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"[::1]x", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"[1.1.1.1]", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"1::2::3", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"1:2:3:4:5:6:7", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"1:2:3:4:5:6:7:8:9", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"12345::1", YOZORA_DNS_INVALID_ADDRESS, ""},
    {":1", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"1:", YOZORA_DNS_INVALID_ADDRESS, ""},
    {"::g", YOZORA_DNS_INVALID_ADDRESS, ""},
};

static void test_normalize(void)
{
    for (size_t i = 0; i < sizeof normalize_cases / sizeof normalize_cases[0]; ++i) {
        const normalize_case *c = &normalize_cases[i];
        char out[YOZORA_DNS_CAPACITY];
        memset(out, 'x', sizeof out);
        yozora_dns_status status = yozora_dns_normalize(c->input, out, sizeof out);
        CHECK(status == c->status, "'%s': status %d, wanted %d", c->input, (int)status, (int)c->status);
        CHECK(strcmp(out, c->url) == 0, "'%s': '%s', wanted '%s'", c->input, out, c->url);
    }
}

static void test_limits(void)
{
    char longest[400], out[YOZORA_DNS_CAPACITY];
    /* a name of 253 characters is valid as a name but the address would not fit */
    memset(longest, 'a', sizeof longest);
    longest[sizeof longest - 1] = '\0';
    CHECK(yozora_dns_normalize(longest, out, sizeof out) == YOZORA_DNS_TOO_LONG && out[0] == '\0', "a 399 character text was not refused");
    /* the longest text that is allowed through still has to fit with its scheme and path, and is never cut */
    char host[YOZORA_DNS_CAPACITY];
    size_t at = 0;
    while (at < 240) {
        memset(host + at, 'a', 60);
        at += 60;
        host[at++] = '.';
    }
    host[at - 1] = '\0';
    yozora_dns_status status = yozora_dns_normalize(host, out, sizeof out);
    CHECK(status == YOZORA_DNS_TOO_LONG || status == YOZORA_DNS_OK, "a long name gave status %d", (int)status);
    if (status == YOZORA_DNS_OK)
        CHECK(strlen(out) < sizeof out && strncmp(out, "https://", 8) == 0, "a long name came back cut");
    /* a small buffer is never overrun and gives an empty text */
    char small[12];
    memset(small, 'x', sizeof small);
    CHECK(yozora_dns_normalize("dns.example.org", small, sizeof small) == YOZORA_DNS_TOO_LONG && small[0] == '\0', "a buffer of 12 was not refused");
    CHECK(yozora_dns_normalize("1.1.1.1", small, sizeof small) == YOZORA_DNS_TOO_LONG && small[0] == '\0', "a preset in a buffer of 12 was cut");
    CHECK(yozora_dns_normalize(NULL, out, sizeof out) == YOZORA_DNS_INVALID_ADDRESS, "a null input was accepted");
    CHECK(yozora_dns_normalize("1.1.1.1", NULL, 10) == YOZORA_DNS_INVALID_ADDRESS, "a null output was accepted");
    CHECK(yozora_dns_normalize("1.1.1.1", out, 0) == YOZORA_DNS_INVALID_ADDRESS, "a zero capacity was accepted");
}

static void test_presets(void)
{
    CHECK(yozora_dns_preset_count() >= 4, "the list of servers is short");
    CHECK(yozora_dns_preset_at(yozora_dns_preset_count()) == NULL, "an index past the list gave a preset");
    for (size_t i = 0; i < yozora_dns_preset_count(); ++i) {
        const yozora_dns_preset *preset = yozora_dns_preset_at(i);
        char out[YOZORA_DNS_CAPACITY], label[64];
        CHECK(yozora_dns_normalize(preset->address, out, sizeof out) == YOZORA_DNS_OK && strcmp(out, preset->url) == 0, "the address of %s does not give its url", preset->name);
        CHECK(yozora_dns_normalize(preset->url, out, sizeof out) == YOZORA_DNS_OK && strcmp(out, preset->url) == 0, "the url of %s is not stable", preset->name);
        yozora_dns_label(preset->url, label, sizeof label);
        CHECK(strcmp(label, preset->name) == 0, "the label of %s is '%s'", preset->name, label);
    }
}

static void test_label(void)
{
    char label[64], tiny[5];
    yozora_dns_label("https://dns.example.org:8443/dns-query", label, sizeof label);
    CHECK(strcmp(label, "dns.example.org:8443") == 0, "label '%s'", label);
    yozora_dns_label("https://[::1]/dns-query", label, sizeof label);
    CHECK(strcmp(label, "[::1]") == 0, "label '%s'", label);
    yozora_dns_label("", label, sizeof label);
    CHECK(label[0] == '\0', "an empty address has a label");
    yozora_dns_label(NULL, label, sizeof label);
    CHECK(label[0] == '\0', "no address has a label");
    yozora_dns_label("https://dns.example.org/x", tiny, sizeof tiny);
    CHECK(strcmp(tiny, "dns.") == 0, "a short buffer: '%s'", tiny);
    yozora_dns_label("https://dns.example.org/x", tiny, 0);
}

static void *flip(void *argument)
{
    const char *urls[] = {"https://dns.google/dns-query", "https://cloudflare-dns.com/dns-query", ""};
    char out[YOZORA_DNS_CAPACITY];
    for (int i = 0; i < 2000; ++i) {
        if (argument) {
            yozora_dns_set(urls[i % 3]);
        } else {
            size_t length = yozora_dns_get(out, sizeof out);
            /* a reader sees one whole server, never a mix of two */
            int whole = length == 0 || strcmp(out, urls[0]) == 0 || strcmp(out, urls[1]) == 0;
            if (!whole || strlen(out) != length)
                return (void *)1;
        }
    }
    return NULL;
}

static void test_state(void)
{
    char out[YOZORA_DNS_CAPACITY], small[8];
    yozora_dns_set("");
    CHECK(yozora_dns_get(out, sizeof out) == 0 && out[0] == '\0', "the process does not start on the system resolver");
    yozora_dns_set("https://dns.google/dns-query");
    CHECK(yozora_dns_get(out, sizeof out) == strlen("https://dns.google/dns-query") && strcmp(out, "https://dns.google/dns-query") == 0, "the server was not kept");
    memset(small, 'x', sizeof small);
    CHECK(yozora_dns_get(small, sizeof small) == 0 && small[0] == '\0', "a buffer that is too small was cut instead of refused");
    char long_url[400];
    memset(long_url, 'a', sizeof long_url);
    long_url[sizeof long_url - 1] = '\0';
    yozora_dns_set(long_url);
    CHECK(yozora_dns_get(out, sizeof out) == strlen("https://dns.google/dns-query"), "an address that does not fit replaced the one in use");
    yozora_dns_set(NULL);
    CHECK(yozora_dns_get(out, sizeof out) == 0, "a null address did not go back to the system");
    pthread_t writer, reader_one, reader_two;
    void *result_one = NULL, *result_two = NULL;
    pthread_create(&writer, NULL, flip, (void *)1);
    pthread_create(&reader_one, NULL, flip, NULL);
    pthread_create(&reader_two, NULL, flip, NULL);
    pthread_join(writer, NULL);
    pthread_join(reader_one, &result_one);
    pthread_join(reader_two, &result_two);
    CHECK(result_one == NULL && result_two == NULL, "a reader saw a half written server");
}

int main(void)
{
    test_normalize();
    test_limits();
    test_presets();
    test_label();
    test_state();
    if (failures) {
        printf("dns: %d failures\n", failures);
        return 1;
    }
    printf("dns: ok\n");
    return 0;
}
