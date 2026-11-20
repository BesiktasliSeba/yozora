#include "yozora_address.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GOOGLE_PREFIX "https://www.google.com/search?q="
/* the page without scripts: it loads fast on slow devices */
#define DUCKDUCKGO_PREFIX "https://duckduckgo.com/?q="
#define YANDEX_PREFIX "https://yandex.ru/search/?text="

static const char *search_prefix(yozora_search_engine core) {
    switch (core) {
    case YOZORA_SEARCH_DUCKDUCKGO: return DUCKDUCKGO_PREFIX;
    case YOZORA_SEARCH_YANDEX: return YANDEX_PREFIX;
    default: return GOOGLE_PREFIX;
    }
}

static int has_prefix(const char *text, const char *prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*text) != *prefix) return 0;
        ++text;
        ++prefix;
    }
    return 1;
}

static int is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int copy_checked(const char *prefix, const char *text, size_t length, char *out, size_t capacity) {
    size_t prefix_length = strlen(prefix);
    if (prefix_length + length >= capacity) return 0;
    memcpy(out, prefix, prefix_length);
    memcpy(out + prefix_length, text, length);
    out[prefix_length + length] = '\0';
    return 1;
}

static yozora_address_kind search_address(const char *text, size_t length, yozora_search_engine core, char *out, size_t capacity) {
    static const char hex[] = "0123456789ABCDEF";
    const char *prefix = search_prefix(core);
    size_t used = strlen(prefix);
    size_t i;
    if (used >= capacity) return YOZORA_ADDRESS_INVALID;
    memcpy(out, prefix, used);
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (isalnum(c) && c < 0x80) {
            if (used + 1 >= capacity) return YOZORA_ADDRESS_INVALID;
            out[used++] = (char)c;
        } else if (c == '-' || c == '_' || c == '.' || c == '~') {
            if (used + 1 >= capacity) return YOZORA_ADDRESS_INVALID;
            out[used++] = (char)c;
        } else if (is_space(c)) {
            if (used + 1 >= capacity) return YOZORA_ADDRESS_INVALID;
            out[used++] = '+';
        } else {
            if (used + 3 >= capacity) return YOZORA_ADDRESS_INVALID;
            out[used++] = '%';
            out[used++] = hex[c >> 4];
            out[used++] = hex[c & 15];
        }
    }
    out[used] = '\0';
    return YOZORA_ADDRESS_SEARCH;
}

static int all_digits_or_dots(const char *text, size_t length) {
    size_t i;
    for (i = 0; i < length; ++i)
        if (!isdigit((unsigned char)text[i]) && text[i] != '.') return 0;
    return length > 0;
}

/* host and optional port, ended by a path, a query or a fragment */
static size_t authority_length(const char *text) {
    size_t length = 0;
    while (text[length] && text[length] != '/' && text[length] != '?' && text[length] != '#') ++length;
    return length;
}

static size_t host_length(const char *authority, size_t length) {
    size_t i;
    if (length && authority[0] == '[') {
        for (i = 1; i < length; ++i)
            if (authority[i] == ']') return i + 1;
        return 0;
    }
    for (i = 0; i < length; ++i)
        if (authority[i] == ':') break;
    return i;
}

static int port_valid(const char *authority, size_t length, size_t host_end) {
    size_t i;
    unsigned long port = 0;
    if (host_end == length) return 1;
    if (authority[host_end] != ':' || host_end + 1 == length) return 0;
    for (i = host_end + 1; i < length; ++i) {
        if (!isdigit((unsigned char)authority[i])) return 0;
        port = port * 10 + (unsigned long)(authority[i] - '0');
        if (port > 65535) return 0;
    }
    return 1;
}

static int host_chars_valid(const char *host, size_t length) {
    size_t i;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)host[i];
        if (!(isalnum(c) || c == '-' || c == '_' || c == '.' || c >= 0x80)) return 0;
    }
    return 1;
}

static int dotted_quad(const char *host, size_t length) {
    size_t dots = 0, run = 0, i;
    if (!all_digits_or_dots(host, length)) return 0;
    for (i = 0; i < length; ++i) {
        if (host[i] == '.') {
            if (!run) return 0;
            ++dots;
            run = 0;
        } else if (++run > 3) {
            return 0;
        }
    }
    return dots == 3 && run > 0;
}

/* a bare word is a search, a dotted name, an address or localhost is a page */
static int looks_like_host(const char *authority, size_t length, int *plain_http) {
    size_t end = host_length(authority, length);
    const char *host = authority;
    *plain_http = 0;
    if (!end || !port_valid(authority, length, end)) return 0;
    if (host[0] == '[') {
        *plain_http = 1;
        return 1;
    }
    if (!host_chars_valid(host, end) || host[0] == '.' || host[end - 1] == '.') return 0;
    if (end == 9 && !strncmp(host, "localhost", 9)) {
        *plain_http = 1;
        return 1;
    }
    if (all_digits_or_dots(host, end)) {
        if (!dotted_quad(host, end)) return 0;
        *plain_http = 1;
        return 1;
    }
    if (!memchr(host, '.', end)) return 0;
    if (end >= 6 && !strncmp(host + end - 6, ".local", 6)) *plain_http = 1;
    return 1;
}

yozora_address_kind yozora_address_resolve(const char *input, yozora_search_engine core, char *out, size_t capacity) {
    size_t length, authority, i;
    int plain_http;
    if (!out || !capacity) return YOZORA_ADDRESS_INVALID;
    out[0] = '\0';
    if (!input) return YOZORA_ADDRESS_INVALID;
    while (is_space((unsigned char)*input)) ++input;
    length = strlen(input);
    while (length && is_space((unsigned char)input[length - 1])) --length;
    if (!length) return YOZORA_ADDRESS_INVALID;
    for (i = 0; i < length; ++i)
        if ((unsigned char)input[i] < 0x20 && !is_space((unsigned char)input[i])) return YOZORA_ADDRESS_INVALID;
    for (i = 0; i < length; ++i) if (is_space((unsigned char)input[i])) break;
    if (i < length) return search_address(input, length, core, out, capacity);
    if (has_prefix(input, "http://") || has_prefix(input, "https://")) {
        size_t scheme = has_prefix(input, "https://") ? 8 : 7;
        if (length == scheme) return YOZORA_ADDRESS_INVALID;
        return copy_checked("", input, length, out, capacity) ? YOZORA_ADDRESS_URL : YOZORA_ADDRESS_INVALID;
    }
    for (i = 0; i + 2 < length; ++i)
        if (input[i] == ':' && input[i + 1] == '/' && input[i + 2] == '/') return YOZORA_ADDRESS_INVALID;
    authority = authority_length(input);
    if (authority > length) authority = length;
    if (looks_like_host(input, authority, &plain_http))
        return copy_checked(plain_http ? "http://" : "https://", input, length, out, capacity)
            ? YOZORA_ADDRESS_URL : YOZORA_ADDRESS_INVALID;
    return search_address(input, length, core, out, capacity);
}

int yozora_address_host(const char *url, char *out, size_t capacity) {
    const char *start;
    size_t length, end;
    if (!out || !capacity) return 0;
    out[0] = '\0';
    if (!url) return 0;
    if (has_prefix(url, "https://")) start = url + 8;
    else if (has_prefix(url, "http://")) start = url + 7;
    else return 0;
    length = authority_length(start);
    for (end = 0; end < length; ++end)
        if (start[end] == '@') {
            start += end + 1;
            length -= end + 1;
            break;
        }
    end = host_length(start, length);
    if (!end) return 0;
    if (end > 4 && has_prefix(start, "www.")) {
        start += 4;
        end -= 4;
    }
    if (end >= capacity) return 0;
    memcpy(out, start, end);
    out[end] = '\0';
    return 1;
}

static const char *const blocked_schemes[] = {
    "javascript", "data", "file", "blob", "about", "view-source", "ws", "wss", "ftp", "filesystem", "itms-services"
};

static int same_text_nocase(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return !*a && !*b;
}

/* the scheme of an absolute address, as RFC 3986 defines it: a letter, then letters, digits, plus, minus or dot */
static size_t scheme_length(const char *url) {
    size_t i;
    if ((unsigned char)url[0] >= 0x80 || !isalpha((unsigned char)url[0])) return 0;
    for (i = 1; url[i] && url[i] != ':'; ++i) {
        unsigned char c = (unsigned char)url[i];
        if (c >= 0x80 || !(isalnum(c) || c == '+' || c == '-' || c == '.')) return 0;
    }
    return url[i] == ':' ? i : 0;
}

yozora_external_kind yozora_address_external(const char *url, const char *page_url) {
    char host[256], page_host[256];
    size_t scheme, i;
    if (!url) return YOZORA_EXTERNAL_NONE;
    for (i = 0; url[i]; ++i)
        if ((unsigned char)url[i] < 0x20 || url[i] == 0x7f) return YOZORA_EXTERNAL_NONE;
    scheme = scheme_length(url);
    if (!scheme || scheme >= sizeof host) return YOZORA_EXTERNAL_NONE;
    if (has_prefix(url, "http://") || has_prefix(url, "https://")) {
        const char *start = url + (has_prefix(url, "https://") ? 8 : 7);
        size_t authority = authority_length(start);
        for (i = 0; i < authority; ++i)
            if (start[i] == '@') return YOZORA_EXTERNAL_NONE;
        if (!yozora_address_host(url, host, sizeof host)) return YOZORA_EXTERNAL_NONE;
        if (page_url && yozora_address_host(page_url, page_host, sizeof page_host) && same_text_nocase(host, page_host))
            return YOZORA_EXTERNAL_NONE;
        return YOZORA_EXTERNAL_UNIVERSAL;
    }
    memcpy(host, url, scheme);
    host[scheme] = '\0';
    for (i = 0; i < sizeof blocked_schemes / sizeof blocked_schemes[0]; ++i)
        if (same_text_nocase(host, blocked_schemes[i])) return YOZORA_EXTERNAL_NONE;
    return YOZORA_EXTERNAL_SCHEME;
}
