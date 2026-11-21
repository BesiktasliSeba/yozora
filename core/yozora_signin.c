#include "yozora_signin.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* a link longer than this is not read at all: a query field is capped lower, and the whole stays bounded */
#define LINK_CAP 16384

/* schemes the callback must never use: a page can navigate to them, the system or Yozora owns them */
static const char *const blocked_schemes[] = {
    "http", "https", "javascript", "data", "file", "blob", "about", "view-source", "ws", "wss", "ftp", "filesystem",
    "itms-services", "yozora-auth", "mailto", "tel", "sms", "facetime"
};

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int starts_with_ci(const char *text, const char *prefix)
{
    for (; *prefix; ++text, ++prefix) {
        if (lower((unsigned char)*text) != lower((unsigned char)*prefix))
            return 0;
    }
    return 1;
}

static int hex_value(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* a decoded byte that no address or name may hold */
static int forbidden_byte(unsigned char c)
{
    return c <= 0x20 || c == 0x7f;
}

/* percent-decodes len bytes of in into out, rejecting a bad escape, a forbidden byte or a value that does not
   fit capacity with its terminator. '+' stays a plus */
static int decode(const char *in, size_t len, char *out, size_t capacity)
{
    size_t at = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)in[i];
        if (c == '%') {
            int a = i + 1 < len ? hex_value((unsigned char)in[i + 1]) : -1;
            int b = i + 2 < len ? hex_value((unsigned char)in[i + 2]) : -1;
            if (a < 0 || b < 0)
                return 0;
            c = (unsigned char)(a * 16 + b);
            i += 2;
        }
        if (forbidden_byte(c) || at + 1 >= capacity)
            return 0;
        out[at++] = (char)c;
    }
    out[at] = '\0';
    return 1;
}

/* http or https with a host and no credentials, which a person could be shown and a page could load */
static int web_address(const char *text)
{
    const char *rest;
    if (starts_with_ci(text, "https://"))
        rest = text + 8;
    else if (starts_with_ci(text, "http://"))
        rest = text + 7;
    else
        return 0;
    size_t authority = strcspn(rest, "/?#");
    if (!authority)
        return 0;
    for (size_t i = 0; i < authority; ++i) {
        if (rest[i] == '@' || rest[i] == '\\')
            return 0;
    }
    return 1;
}

static int callback_scheme_ok(const char *callback)
{
    size_t n = 0;
    if (!((callback[0] >= 'a' && callback[0] <= 'z') || (callback[0] >= 'A' && callback[0] <= 'Z')))
        return 0;
    while (callback[n] && callback[n] != ':') {
        char c = callback[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'))
            return 0;
        ++n;
    }
    if (callback[n] != ':')
        return 0;
    for (size_t i = 0; i < sizeof blocked_schemes / sizeof blocked_schemes[0]; ++i) {
        size_t length = strlen(blocked_schemes[i]);
        if (length == n && starts_with_ci(callback, blocked_schemes[i]))
            return 0;
    }
    return strchr(callback, '#') == NULL;
}

/* a cookie name is an rfc 6265 token: no separators, spaces or controls */
static int cookie_name_ok(const char *name)
{
    if (!*name)
        return 0;
    for (; *name; ++name) {
        unsigned char c = (unsigned char)*name;
        if (c <= 0x20 || c >= 0x7f || strchr("()<>@,;:\\\"/[]?={}", c))
            return 0;
    }
    return 1;
}

typedef struct {
    int seen_url, seen_callback, seen_until, seen_cookie, seen_state;
} seen_fields;

/* stores one decoded field. a repeated key is ambiguous, so it is malformed */
static int take_field(yozora_signin_request *out, seen_fields *seen, const char *key, size_t key_length, const char *value,
                      size_t value_length)
{
#define FIELD(name, member, flag) \
    if (key_length == sizeof(name) - 1 && memcmp(key, name, key_length) == 0) { \
        if (seen->flag) \
            return 0; \
        seen->flag = 1; \
        return decode(value, value_length, out->member, sizeof out->member); \
    }
    FIELD("url", url, seen_url)
    FIELD("callback", callback, seen_callback)
    FIELD("until", until, seen_until)
    FIELD("cookie", cookie, seen_cookie)
    FIELD("state", state, seen_state)
#undef FIELD
    /* a field of a later version is ignored */
    return 1;
}

yozora_signin_status yozora_signin_parse(const char *link, yozora_signin_request *out)
{
    static const char prefix[] = "yozora-auth://start";
    if (!out)
        return YOZORA_SIGNIN_MALFORMED;
    memset(out, 0, sizeof *out);
    if (!link || !starts_with_ci(link, "yozora-auth:"))
        return YOZORA_SIGNIN_NOT_SIGNIN;
    size_t length = 0;
    while (length <= LINK_CAP && link[length])
        ++length;
    if (length > LINK_CAP || !starts_with_ci(link, prefix))
        return YOZORA_SIGNIN_MALFORMED;
    const char *at = link + sizeof prefix - 1;
    if (*at == '/')
        ++at;
    if (*at != '?')
        return YOZORA_SIGNIN_MALFORMED;
    ++at;
    const char *end = at + strcspn(at, "#");
    seen_fields seen = { 0, 0, 0, 0, 0 };
    while (at < end) {
        size_t pair = (size_t)(end - at);
        const char *amp = memchr(at, '&', pair);
        if (amp)
            pair = (size_t)(amp - at);
        if (pair) {
            const char *equals = memchr(at, '=', pair);
            size_t key_length = equals ? (size_t)(equals - at) : pair;
            const char *value = equals ? equals + 1 : at + pair;
            size_t value_length = equals ? pair - key_length - 1 : 0;
            if (!take_field(out, &seen, at, key_length, value, value_length)) {
                memset(out, 0, sizeof *out);
                return YOZORA_SIGNIN_MALFORMED;
            }
        }
        at += pair + (amp ? 1 : 0);
    }
    yozora_signin_status status = YOZORA_SIGNIN_OK;
    if (!web_address(out->url) || (out->until[0] && strncmp(out->url, out->until, strlen(out->until)) == 0))
        status = YOZORA_SIGNIN_BAD_URL;
    else if (!callback_scheme_ok(out->callback))
        status = YOZORA_SIGNIN_BAD_CALLBACK;
    else if (!out->until[0] && !out->cookie[0])
        status = YOZORA_SIGNIN_NO_END;
    else if ((out->until[0] && !web_address(out->until)) || (out->cookie[0] && !cookie_name_ok(out->cookie)))
        status = YOZORA_SIGNIN_BAD_END;
    if (status != YOZORA_SIGNIN_OK)
        memset(out, 0, sizeof *out);
    return status;
}

/* 1 when the header (name=value; name=value) has a cookie of this name with a value */
static int cookie_present(const char *header, const char *name)
{
    size_t name_length = strlen(name);
    const char *at = header;
    while (*at) {
        while (*at == ' ' || *at == ';')
            ++at;
        size_t item = strcspn(at, ";");
        const char *equals = memchr(at, '=', item);
        if (equals && (size_t)(equals - at) == name_length && memcmp(at, name, name_length) == 0 && (size_t)(equals - at) + 1 < item)
            return 1;
        at += item;
    }
    return 0;
}

int yozora_signin_finished(const yozora_signin_request *request, const char *page_url, const char *cookie_header)
{
    if (!request)
        return 0;
    if (request->until[0] && page_url && strncmp(page_url, request->until, strlen(request->until)) == 0)
        return 1;
    return request->cookie[0] && cookie_header && cookie_present(cookie_header, request->cookie);
}

typedef struct {
    char *out;
    size_t capacity;
    size_t length;
    int overflow;
} builder;

static void put(builder *b, char c)
{
    if (b->length + 1 >= b->capacity) {
        b->overflow = 1;
        return;
    }
    b->out[b->length++] = c;
}

static void put_text(builder *b, const char *text)
{
    for (; *text; ++text)
        put(b, *text);
}

static void put_encoded(builder *b, const char *text)
{
    static const char digits[] = "0123456789ABCDEF";
    for (; *text; ++text) {
        unsigned char c = (unsigned char)*text;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') {
            put(b, (char)c);
        } else {
            put(b, '%');
            put(b, digits[c >> 4]);
            put(b, digits[c & 15]);
        }
    }
}

static void put_field(builder *b, const char *name, const char *value)
{
    if (!value || !*value)
        return;
    put(b, '&');
    put_text(b, name);
    put(b, '=');
    put_encoded(b, value);
}

size_t yozora_signin_result(const yozora_signin_request *request, yozora_signin_outcome outcome, const char *page_url,
                            const char *cookie_header, char *out, size_t capacity)
{
    if (!out || !capacity)
        return 0;
    out[0] = '\0';
    if (!request || !request->callback[0])
        return 0;
    builder b = { out, capacity, 0, 0 };
    put_text(&b, request->callback);
    char last = request->callback[strlen(request->callback) - 1];
    /* the callback may already carry a query, or end where a query field goes */
    if (last != '?' && last != '&')
        put(&b, strchr(request->callback, '?') ? '&' : '?');
    const char *name = outcome == YOZORA_SIGNIN_DONE ? "done" : (outcome == YOZORA_SIGNIN_CANCELLED ? "cancelled" : "too_large");
    put_text(&b, "status=");
    put_text(&b, name);
    put_field(&b, "url", page_url);
    if (outcome == YOZORA_SIGNIN_DONE)
        put_field(&b, "cookies", cookie_header);
    put_field(&b, "state", request->state);
    if (b.overflow) {
        out[0] = '\0';
        return 0;
    }
    out[b.length] = '\0';
    return b.length;
}
