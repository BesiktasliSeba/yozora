#include "yozora_dns.h"

#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const yozora_dns_preset presets[] = {
    {"Cloudflare", "1.1.1.1", "https://cloudflare-dns.com/dns-query"},
    {"Google", "8.8.8.8", "https://dns.google/dns-query"},
    {"Quad9", "9.9.9.9", "https://dns.quad9.net/dns-query"},
    {"AdGuard", "94.140.14.14", "https://dns.adguard-dns.com/dns-query"},
};

/* the second address of each public resolver is the same server */
static const struct {
    const char *address;
    size_t preset;
} aliases[] = {
    {"1.1.1.1", 0}, {"1.0.0.1", 0}, {"8.8.8.8", 1}, {"8.8.4.4", 1}, {"9.9.9.9", 2}, {"149.112.112.112", 2}, {"94.140.14.14", 3}, {"94.140.15.15", 3},
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char current[YOZORA_DNS_CAPACITY];

size_t yozora_dns_preset_count(void)
{
    return sizeof presets / sizeof presets[0];
}

const yozora_dns_preset *yozora_dns_preset_at(size_t index)
{
    return index < yozora_dns_preset_count() ? &presets[index] : NULL;
}

static int ascii_iequal(const char *a, const char *b, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
    return 1;
}

/* four decimal numbers up to 255 with no leading zero beyond a lone 0 */
static int is_ipv4(const char *host, size_t length)
{
    unsigned groups = 0;
    size_t i = 0;
    while (i < length) {
        size_t start = i;
        unsigned value = 0;
        while (i < length && host[i] >= '0' && host[i] <= '9') {
            value = value * 10 + (unsigned)(host[i] - '0');
            if (value > 255)
                return 0;
            ++i;
        }
        size_t digits = i - start;
        if (digits == 0 || digits > 3 || (digits > 1 && host[start] == '0'))
            return 0;
        ++groups;
        if (i < length) {
            if (host[i] != '.' || groups == 4)
                return 0;
            ++i;
            if (i == length)
                return 0;
        }
    }
    return groups == 4;
}

/* groups of one to four hex digits split by colons: eight of them, or fewer around one "::" that stands for the zeros left out */
static int is_ipv6(const char *host, size_t length)
{
    if (length < 2 || length > 39)
        return 0;
    size_t i = 0;
    unsigned groups = 0;
    int gap = 0;
    if (host[0] == ':') {
        if (host[1] != ':')
            return 0;
        gap = 1;
        i = 2;
        if (i == length)
            return 1;
    }
    while (i < length) {
        size_t digits = 0;
        while (i < length && isxdigit((unsigned char)host[i])) {
            if (++digits > 4)
                return 0;
            ++i;
        }
        if (digits == 0)
            return 0;
        ++groups;
        if (i == length)
            break;
        if (host[i] != ':')
            return 0;
        ++i;
        if (i == length)
            return 0;
        if (host[i] == ':') {
            if (gap)
                return 0;
            gap = 1;
            ++i;
            if (i == length)
                break;
        }
    }
    return gap ? groups <= 7 : groups == 8;
}

/* labels of letters, digits and hyphens, none empty, none starting or ending with a hyphen, at most 63 each and 253 in all */
static int is_hostname(const char *host, size_t length)
{
    if (length == 0 || length > 253)
        return 0;
    size_t label = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)host[i];
        if (c == '.') {
            if (label == 0 || host[i - 1] == '-')
                return 0;
            label = 0;
            continue;
        }
        if (!(isalnum(c) || c == '-') || c > 127)
            return 0;
        if (label == 0 && c == '-')
            return 0;
        if (++label > 63)
            return 0;
    }
    return label > 0 && host[length - 1] != '-';
}

/* a name whose last label is only digits is a malformed address (1.1.1, 256.1.1.1), it is never looked up as a name */
static int ends_in_number(const char *host, size_t length)
{
    size_t start = length;
    while (start > 0 && host[start - 1] != '.')
        --start;
    if (start == length)
        return 0;
    for (size_t i = start; i < length; ++i)
        if (host[i] < '0' || host[i] > '9')
            return 0;
    return 1;
}

/* a server is a name with a dot in it or an address: a lone word is a mistake, a provider is picked from the list by its name */
static int has_dot(const char *host, size_t length)
{
    return memchr(host, '.', length) != NULL;
}

static int is_port(const char *text, size_t length)
{
    if (length == 0 || length > 5)
        return 0;
    unsigned value = 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9')
            return 0;
        value = value * 10 + (unsigned)(text[i] - '0');
    }
    return value >= 1 && value <= 65535;
}

static int path_is_clean(const char *path, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)path[i];
        if (c <= 0x20 || c >= 0x7f || c == '?' || c == '#' || c == '\\' || c == '"' || c == '\'' || c == '<' || c == '>')
            return 0;
    }
    return 1;
}

yozora_dns_status yozora_dns_normalize(const char *input, char *out, size_t capacity)
{
    if (out && capacity)
        out[0] = '\0';
    if (!input || !out || capacity == 0)
        return YOZORA_DNS_INVALID_ADDRESS;
    while (*input == ' ' || *input == '\t')
        ++input;
    size_t length = strlen(input);
    while (length && (input[length - 1] == ' ' || input[length - 1] == '\t' || input[length - 1] == '\n' || input[length - 1] == '\r'))
        --length;
    if (length == 0)
        return YOZORA_DNS_EMPTY;
    if (length >= YOZORA_DNS_CAPACITY)
        return YOZORA_DNS_TOO_LONG;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)input[i];
        if (c <= 0x20 || c >= 0x7f)
            return YOZORA_DNS_INVALID_ADDRESS;
    }
    const char *rest = input;
    size_t rest_length = length;
    if (rest_length >= 8 && ascii_iequal(rest, "https://", 8)) {
        rest += 8;
        rest_length -= 8;
    } else if (rest_length >= 7 && ascii_iequal(rest, "http://", 7)) {
        return YOZORA_DNS_INSECURE;
    } else if (memchr(rest, '/', rest_length) && strstr(rest, "://")) {
        return YOZORA_DNS_INVALID_ADDRESS;
    }
    /* the host part ends at the first slash, the path (if any) is kept */
    const char *slash = memchr(rest, '/', rest_length);
    size_t authority = slash ? (size_t)(slash - rest) : rest_length;
    const char *path = slash ? slash : "";
    size_t path_length = slash ? rest_length - authority : 0;
    if (authority == 0 || memchr(rest, '@', authority))
        return YOZORA_DNS_INVALID_ADDRESS;
    if (path_length && !path_is_clean(path, path_length))
        return YOZORA_DNS_INVALID_ADDRESS;
    const char *host = rest;
    size_t host_length = authority;
    const char *port = NULL;
    size_t port_length = 0;
    int bracketed = 0;
    if (host[0] == '[') {
        const char *close = memchr(host, ']', authority);
        if (!close)
            return YOZORA_DNS_INVALID_ADDRESS;
        size_t inner = (size_t)(close - host) - 1;
        size_t after = authority - (size_t)(close - host) - 1;
        if (after) {
            if (close[1] != ':')
                return YOZORA_DNS_INVALID_ADDRESS;
            port = close + 2;
            port_length = after - 1;
        }
        host += 1;
        host_length = inner;
        bracketed = 1;
        if (!is_ipv6(host, host_length))
            return YOZORA_DNS_INVALID_ADDRESS;
    } else if (memchr(host, ':', authority)) {
        /* a bare address with colons is an IPv6 address, one colon is a port */
        const char *colon = memchr(host, ':', authority);
        if (memchr(colon + 1, ':', authority - (size_t)(colon - host) - 1)) {
            if (!is_ipv6(host, authority))
                return YOZORA_DNS_INVALID_ADDRESS;
            bracketed = 1;
        } else {
            host_length = (size_t)(colon - host);
            port = colon + 1;
            port_length = authority - host_length - 1;
        }
    }
    if (port && !is_port(port, port_length))
        return YOZORA_DNS_INVALID_ADDRESS;
    if (!bracketed && !is_ipv4(host, host_length) && !(is_hostname(host, host_length) && has_dot(host, host_length) && !ends_in_number(host, host_length)))
        return YOZORA_DNS_INVALID_ADDRESS;
    /* a plain address of a known public resolver is that resolver, by its name */
    if (!bracketed && !port && path_length == 0 && rest == input) {
        for (size_t i = 0; i < sizeof aliases / sizeof aliases[0]; ++i) {
            if (strlen(aliases[i].address) == host_length && memcmp(aliases[i].address, host, host_length) == 0) {
                size_t url_length = strlen(presets[aliases[i].preset].url);
                if (url_length >= capacity)
                    return YOZORA_DNS_TOO_LONG;
                memcpy(out, presets[aliases[i].preset].url, url_length + 1);
                return YOZORA_DNS_OK;
            }
        }
    }
    if (path_length == 0) {
        path = "/dns-query";
        path_length = 10;
    }
    /* the host is written in lower case, the path as it was typed */
    int written = snprintf(out, capacity, "https://%s", bracketed ? "[" : "");
    if (written < 0 || (size_t)written >= capacity) {
        out[0] = '\0';
        return YOZORA_DNS_TOO_LONG;
    }
    size_t used = (size_t)written;
    size_t needed = used + host_length + (bracketed ? 1 : 0) + (port ? 1 + port_length : 0) + path_length + 1;
    if (needed > capacity) {
        out[0] = '\0';
        return YOZORA_DNS_TOO_LONG;
    }
    for (size_t i = 0; i < host_length; ++i)
        out[used++] = (char)tolower((unsigned char)host[i]);
    if (bracketed)
        out[used++] = ']';
    if (port) {
        out[used++] = ':';
        memcpy(out + used, port, port_length);
        used += port_length;
    }
    memcpy(out + used, path, path_length);
    used += path_length;
    out[used] = '\0';
    return YOZORA_DNS_OK;
}

void yozora_dns_label(const char *url, char *out, size_t capacity)
{
    if (!out || capacity == 0)
        return;
    out[0] = '\0';
    if (!url || !url[0])
        return;
    for (size_t i = 0; i < yozora_dns_preset_count(); ++i) {
        if (strcmp(presets[i].url, url) == 0) {
            snprintf(out, capacity, "%s", presets[i].name);
            return;
        }
    }
    const char *host = strncmp(url, "https://", 8) == 0 ? url + 8 : url;
    size_t length = strcspn(host, "/");
    if (length >= capacity)
        length = capacity - 1;
    memcpy(out, host, length);
    out[length] = '\0';
}

void yozora_dns_set(const char *url)
{
    pthread_mutex_lock(&lock);
    if (!url || !url[0])
        current[0] = '\0';
    else if (strlen(url) < sizeof current)
        memcpy(current, url, strlen(url) + 1);
    pthread_mutex_unlock(&lock);
}

size_t yozora_dns_get(char *out, size_t capacity)
{
    size_t length = 0;
    pthread_mutex_lock(&lock);
    length = strlen(current);
    if (out && capacity) {
        if (length >= capacity) {
            out[0] = '\0';
            length = 0;
        } else
            memcpy(out, current, length + 1);
    }
    pthread_mutex_unlock(&lock);
    return length;
}
