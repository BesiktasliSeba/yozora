#include "yozora_icons.h"

#include "yozora_playlist.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define TAG_MAX 4096
#define ATTR_MAX 2048
#define CANDIDATES_MAX 64

static int equal_nocase(const char *text, size_t length, const char *word)
{
    size_t i = 0;
    for (; word[i]; ++i) {
        if (i >= length || tolower((unsigned char)text[i]) != word[i])
            return 0;
    }
    return 1;
}

/* the position of word (lower case) in text after from, or length when it is not there */
static size_t find_nocase(const char *text, size_t length, size_t from, const char *word)
{
    size_t size = strlen(word);
    for (size_t i = from; i + size <= length; ++i) {
        if (equal_nocase(text + i, length - i, word))
            return i;
    }
    return length;
}

/* the value of one attribute of the tag text[0, length), copied and cut at capacity. 0 when it has none.
   quotes are single, double or none, names are compared without case */
static int attribute(const char *tag, size_t length, const char *name, char *out, size_t capacity)
{
    size_t i = 0, size = strlen(name);
    while (i < length) {
        while (i < length && (isspace((unsigned char)tag[i]) || tag[i] == '/'))
            ++i;
        size_t start = i;
        while (i < length && !isspace((unsigned char)tag[i]) && tag[i] != '=' && tag[i] != '/')
            ++i;
        size_t name_length = i - start;
        while (i < length && isspace((unsigned char)tag[i]))
            ++i;
        if (i >= length || tag[i] != '=') {
            continue;
        }
        ++i;
        while (i < length && isspace((unsigned char)tag[i]))
            ++i;
        size_t value_start = i, value_end;
        if (i < length && (tag[i] == '"' || tag[i] == '\'')) {
            char quote = tag[i++];
            value_start = i;
            while (i < length && tag[i] != quote)
                ++i;
            value_end = i;
            if (i < length)
                ++i;
        } else {
            while (i < length && !isspace((unsigned char)tag[i]))
                ++i;
            value_end = i;
        }
        if (name_length == size && equal_nocase(tag + start, name_length, name)) {
            size_t count = value_end - value_start;
            if (count >= capacity)
                return 0;
            memcpy(out, tag + value_start, count);
            out[count] = 0;
            return 1;
        }
    }
    return 0;
}

/* a token of a space separated list (rel, sizes) is there */
static int has_token(const char *list, const char *token)
{
    size_t size = strlen(token);
    const char *p = list;
    while (*p) {
        while (*p && isspace((unsigned char)*p))
            ++p;
        const char *start = p;
        while (*p && !isspace((unsigned char)*p))
            ++p;
        if ((size_t)(p - start) == size && equal_nocase(start, size, token))
            return 1;
    }
    return 0;
}

/* the largest side of "16x16 32x32 180x180" */
static unsigned largest_side(const char *sizes)
{
    unsigned best = 0;
    const char *p = sizes;
    while (*p) {
        if (isdigit((unsigned char)*p)) {
            unsigned long width = 0, height = 0;
            while (isdigit((unsigned char)*p) && width < 100000)
                width = width * 10 + (unsigned long)(*p++ - '0');
            if (*p == 'x' || *p == 'X') {
                ++p;
                while (isdigit((unsigned char)*p) && height < 100000)
                    height = height * 10 + (unsigned long)(*p++ - '0');
            }
            unsigned long side = width > height ? width : height;
            if (side > best && side < 100000)
                best = (unsigned)side;
        } else {
            ++p;
        }
    }
    return best;
}

/* the address without query and fragment ends in .svg */
static int is_vector(const char *url)
{
    size_t end = strcspn(url, "?#");
    return end >= 4 && equal_nocase(url + end - 4, 4, ".svg");
}

/* &amp; of an attribute is &, the one entity an address in html carries */
static void decode_ampersands(char *text)
{
    char *out = text;
    for (const char *in = text; *in;) {
        if (strncmp(in, "&amp;", 5) == 0) {
            *out++ = '&';
            in += 5;
        } else {
            *out++ = *in++;
        }
    }
    *out = 0;
}

static int rank(const yozora_icon_link *link)
{
    unsigned size = link->size ? link->size : (link->touch ? 120 : 32);
    return (int)size * 2 + (link->touch ? 1 : 0);
}

size_t yozora_icon_links(const char *html, size_t length, const char *base, yozora_icon_link *out, size_t capacity)
{
    if (!html || !base || !out || !capacity)
        return 0;
    if (length > YOZORA_ICON_SCAN_MAX)
        length = YOZORA_ICON_SCAN_MAX;
    yozora_icon_link *found = calloc(CANDIDATES_MAX, sizeof *found);
    if (!found)
        return 0;
    size_t count = 0;
    size_t i = 0;
    while (i < length && count < CANDIDATES_MAX) {
        const char *lt = memchr(html + i, '<', length - i);
        if (!lt)
            break;
        i = (size_t)(lt - html);
        if (i + 4 <= length && strncmp(html + i, "<!--", 4) == 0) {
            size_t end = find_nocase(html, length, i + 4, "-->");
            i = end < length ? end + 3 : length;
            continue;
        }
        /* the head ends where the body begins, and the text of a script or a style is not markup */
        if (equal_nocase(html + i, length - i, "</head") || equal_nocase(html + i, length - i, "<body"))
            break;
        if (equal_nocase(html + i, length - i, "<script") || equal_nocase(html + i, length - i, "<style")) {
            int script = equal_nocase(html + i, length - i, "<script");
            size_t end = find_nocase(html, length, i + 1, script ? "</script" : "</style");
            if (end >= length)
                break;
            i = end + 1;
            continue;
        }
        if (!(equal_nocase(html + i, length - i, "<link") && i + 5 < length && (isspace((unsigned char)html[i + 5]) || html[i + 5] == '/'))) {
            ++i;
            continue;
        }
        size_t start = i + 5, end = start;
        char quote = 0;
        while (end < length && end - start < TAG_MAX) {
            char c = html[end];
            if (quote) {
                if (c == quote)
                    quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') {
                break;
            }
            ++end;
        }
        if (end >= length || end - start >= TAG_MAX || html[end] != '>') {
            i = start;
            continue;
        }
        i = end + 1;
        char rel[ATTR_MAX], href[ATTR_MAX], sizes[256], type[128];
        if (!attribute(html + start, end - start, "rel", rel, sizeof rel) || !attribute(html + start, end - start, "href", href, sizeof href))
            continue;
        int touch = has_token(rel, "apple-touch-icon") || has_token(rel, "apple-touch-icon-precomposed");
        int icon = has_token(rel, "icon");
        if (!touch && !icon)
            continue;
        if (attribute(html + start, end - start, "type", type, sizeof type) && strstr(type, "svg"))
            continue;
        decode_ampersands(href);
        if (!href[0] || is_vector(href))
            continue;
        char resolved[YOZORA_URL_MAX];
        if (yozora_url_resolve(base, href, resolved, sizeof resolved) != YOZORA_PLAYLIST_OK || strlen(resolved) >= YOZORA_ICON_URL_MAX)
            continue;
        int duplicate = 0;
        for (size_t k = 0; k < count; ++k)
            duplicate = duplicate || strcmp(found[k].url, resolved) == 0;
        if (duplicate)
            continue;
        yozora_icon_link *link = &found[count++];
        memcpy(link->url, resolved, strlen(resolved) + 1);
        link->touch = touch;
        link->size = attribute(html + start, end - start, "sizes", sizes, sizeof sizes) ? largest_side(sizes) : 0;
    }
    /* insertion sort: at most a few dozen entries, equal ranks keep the order of the page */
    for (size_t a = 1; a < count; ++a) {
        yozora_icon_link current = found[a];
        size_t b = a;
        while (b > 0 && rank(&found[b - 1]) < rank(&current)) {
            found[b] = found[b - 1];
            --b;
        }
        found[b] = current;
    }
    size_t result = count < capacity ? count : capacity;
    memcpy(out, found, result * sizeof *out);
    free(found);
    return result;
}
