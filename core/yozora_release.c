#include "yozora_release.h"

#include <ctype.h>
#include <string.h>

#define MAX_DEPTH 16
#define MAX_ASSETS 32
#define RAW_NOTES_MAX 16384
#define ASSET_NAME_MAX 96

typedef struct {
    const char *p;
    const char *end;
} cursor;

typedef struct {
    char name[ASSET_NAME_MAX];
    char url[YOZORA_RELEASE_URL_MAX];
    uint64_t size;
} asset;

static void skip_space(cursor *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r'))
        ++c->p;
}

static int hex_value(int ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static size_t utf8_put(unsigned long code, char *out)
{
    if (code < 0x80) {
        out[0] = (char)code;
        return 1;
    }
    if (code < 0x800) {
        out[0] = (char)(0xc0 | (code >> 6));
        out[1] = (char)(0x80 | (code & 0x3f));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = (char)(0xe0 | (code >> 12));
        out[1] = (char)(0x80 | ((code >> 6) & 0x3f));
        out[2] = (char)(0x80 | (code & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (code >> 18));
    out[1] = (char)(0x80 | ((code >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((code >> 6) & 0x3f));
    out[3] = (char)(0x80 | (code & 0x3f));
    return 4;
}

static int read_hex4(const char *p, const char *end, unsigned long *value)
{
    unsigned long v = 0;
    if (end - p < 4)
        return 0;
    for (int i = 0; i < 4; ++i) {
        int h = hex_value((unsigned char)p[i]);
        if (h < 0)
            return 0;
        v = v * 16 + (unsigned long)h;
    }
    *value = v;
    return 1;
}

/* reads a string at the opening quote into out, cut at capacity, returns 0 when the string is malformed */
static int parse_string(cursor *c, char *out, size_t capacity)
{
    size_t used = 0;
    if (c->p >= c->end || *c->p != '"')
        return 0;
    ++c->p;
    while (c->p < c->end && *c->p != '"') {
        char piece[4];
        size_t length = 1;
        unsigned char ch = (unsigned char)*c->p;
        if (ch < 0x20)
            return 0;
        if (ch == '\\') {
            ++c->p;
            if (c->p >= c->end)
                return 0;
            switch (*c->p) {
            case '"': piece[0] = '"'; break;
            case '\\': piece[0] = '\\'; break;
            case '/': piece[0] = '/'; break;
            case 'b': piece[0] = '\b'; break;
            case 'f': piece[0] = '\f'; break;
            case 'n': piece[0] = '\n'; break;
            case 'r': piece[0] = '\r'; break;
            case 't': piece[0] = '\t'; break;
            case 'u': {
                unsigned long code, low;
                if (!read_hex4(c->p + 1, c->end, &code))
                    return 0;
                c->p += 4;
                if (code >= 0xd800 && code < 0xdc00 && c->end - c->p >= 7 && c->p[1] == '\\' && c->p[2] == 'u' && read_hex4(c->p + 3, c->end, &low) &&
                    low >= 0xdc00 && low < 0xe000) {
                    code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                    c->p += 6;
                } else if (code >= 0xd800 && code < 0xe000) {
                    code = 0xfffd;
                }
                length = utf8_put(code, piece);
                break;
            }
            default:
                return 0;
            }
        } else {
            piece[0] = (char)ch;
        }
        if (capacity && used + length < capacity) {
            memcpy(out + used, piece, length);
            used += length;
        }
        ++c->p;
    }
    if (c->p >= c->end)
        return 0;
    ++c->p;
    if (capacity)
        out[used] = '\0';
    return 1;
}

static int skip_value(cursor *c, int depth);

static int skip_container(cursor *c, int depth, char close, int object)
{
    if (depth > MAX_DEPTH)
        return 0;
    ++c->p;
    skip_space(c);
    if (c->p < c->end && *c->p == close) {
        ++c->p;
        return 1;
    }
    for (;;) {
        skip_space(c);
        if (object) {
            if (!parse_string(c, NULL, 0))
                return 0;
            skip_space(c);
            if (c->p >= c->end || *c->p != ':')
                return 0;
            ++c->p;
        }
        if (!skip_value(c, depth + 1))
            return 0;
        skip_space(c);
        if (c->p >= c->end)
            return 0;
        if (*c->p == ',') {
            ++c->p;
            continue;
        }
        if (*c->p == close) {
            ++c->p;
            return 1;
        }
        return 0;
    }
}

static int skip_value(cursor *c, int depth)
{
    skip_space(c);
    if (c->p >= c->end)
        return 0;
    if (*c->p == '"')
        return parse_string(c, NULL, 0);
    if (*c->p == '{')
        return skip_container(c, depth, '}', 1);
    if (*c->p == '[')
        return skip_container(c, depth, ']', 0);
    const char *start = c->p;
    while (c->p < c->end && (isalnum((unsigned char)*c->p) || *c->p == '-' || *c->p == '+' || *c->p == '.'))
        ++c->p;
    return c->p > start;
}

static int read_literal(cursor *c, const char *word)
{
    size_t n = strlen(word);
    if ((size_t)(c->end - c->p) < n || memcmp(c->p, word, n) != 0)
        return 0;
    c->p += n;
    return 1;
}

static int read_number(cursor *c, uint64_t *value)
{
    uint64_t v = 0;
    int digits = 0;
    while (c->p < c->end && *c->p >= '0' && *c->p <= '9') {
        if (digits++ > 15)
            return 0;
        v = v * 10 + (uint64_t)(*c->p - '0');
        ++c->p;
    }
    *value = v;
    return digits > 0;
}

static int tag_valid(const char *tag)
{
    size_t length = strlen(tag);
    if (!length || length >= YOZORA_RELEASE_TAG_MAX)
        return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)tag[i];
        if (!isalnum(ch) && ch != '.' && ch != '_' && ch != '-' && ch != '+')
            return 0;
    }
    return 1;
}

static int starts_with(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static void copy_bounded(char *out, size_t capacity, const char *text)
{
    size_t length = strlen(text);
    if (length >= capacity)
        length = capacity - 1;
    memcpy(out, text, length);
    out[length] = '\0';
}

static int build_asset_name(const char *tag, const char *suffix, char *out, size_t capacity)
{
    size_t need = strlen("Yozora-") + strlen(tag) + strlen(suffix) + 1;
    if (need > capacity)
        return 0;
    strcpy(out, "Yozora-");
    strcat(out, tag);
    strcat(out, suffix);
    return 1;
}

static int build_download_url(const char *tag, const char *suffix, char *out, size_t capacity)
{
    char name[ASSET_NAME_MAX];
    if (!build_asset_name(tag, suffix, name, sizeof name))
        return 0;
    size_t need = strlen(YOZORA_RELEASE_DOWNLOAD_PREFIX) + strlen(tag) + 1 + strlen(name) + 1;
    if (need > capacity)
        return 0;
    strcpy(out, YOZORA_RELEASE_DOWNLOAD_PREFIX);
    strcat(out, tag);
    strcat(out, "/");
    strcat(out, name);
    return 1;
}

typedef struct {
    char *out;
    size_t capacity;
    size_t used;
    int newlines;
} text_sink;

static void sink_put(text_sink *s, char ch)
{
    if (ch == '\n') {
        while (s->used && s->out[s->used - 1] == ' ')
            --s->used;
        if (!s->used || s->newlines >= 2)
            return;
        ++s->newlines;
    } else if (ch == ' ' && (!s->used || s->newlines)) {
        return;
    } else {
        s->newlines = 0;
    }
    if (s->used + 1 < s->capacity)
        s->out[s->used++] = ch;
}

static void sink_text(text_sink *s, const char *text)
{
    for (; *text; ++text)
        sink_put(s, *text);
}

static void sink_finish(text_sink *s)
{
    while (s->used && (s->out[s->used - 1] == '\n' || s->out[s->used - 1] == ' '))
        --s->used;
    s->out[s->used] = '\0';
}

static void markdown_line(text_sink *s, const char *line, size_t length)
{
    size_t i = 0;
    while (i < length && (line[i] == ' ' || line[i] == '\t'))
        ++i;
    if (length - i >= 3 && !memcmp(line + i, "```", 3))
        return;
    size_t hashes = 0;
    while (i + hashes < length && line[i + hashes] == '#')
        ++hashes;
    if (hashes && hashes <= 6 && i + hashes < length && line[i + hashes] == ' ') {
        i += hashes;
        while (i < length && line[i] == ' ')
            ++i;
    } else if (i + 1 < length && (line[i] == '-' || line[i] == '*' || line[i] == '+') && line[i + 1] == ' ') {
        sink_text(s, "\xe2\x80\xa2 ");
        i += 2;
        while (i < length && line[i] == ' ')
            ++i;
    }
    for (; i < length; ++i) {
        char ch = line[i];
        if ((ch == '*' || ch == '_') && i + 1 < length && line[i + 1] == ch) {
            ++i;
            continue;
        }
        if (ch == '`')
            continue;
        if (ch == '[') {
            const char *close = memchr(line + i, ']', length - i);
            if (close && close + 1 < line + length && close[1] == '(') {
                const char *paren = memchr(close, ')', (size_t)(line + length - close));
                if (paren) {
                    for (const char *q = line + i + 1; q < close; ++q)
                        sink_put(s, *q);
                    i = (size_t)(paren - line);
                    continue;
                }
            }
        }
        sink_put(s, ch == '\t' ? ' ' : ch);
    }
}

static void markdown_text(text_sink *s, const char *text, size_t length)
{
    size_t start = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i == length || text[i] == '\n') {
            size_t end = i;
            if (end > start && text[end - 1] == '\r')
                --end;
            markdown_line(s, text + start, end - start);
            sink_put(s, '\n');
            start = i + 1;
        }
    }
}

static size_t decode_entities(const char *text, size_t length, char *out, size_t capacity)
{
    size_t used = 0;
    for (size_t i = 0; i < length && used + 5 < capacity;) {
        if (text[i] == '&') {
            const char *semi = memchr(text + i, ';', length - i < 10 ? length - i : 10);
            if (semi) {
                size_t n = (size_t)(semi - (text + i)) - 1;
                const char *name = text + i + 1;
                unsigned long code = 0;
                int known = 1;
                if (n == 2 && !memcmp(name, "lt", 2)) code = '<';
                else if (n == 2 && !memcmp(name, "gt", 2)) code = '>';
                else if (n == 3 && !memcmp(name, "amp", 3)) code = '&';
                else if (n == 4 && !memcmp(name, "quot", 4)) code = '"';
                else if (n == 4 && !memcmp(name, "apos", 4)) code = '\'';
                else if (n == 4 && !memcmp(name, "nbsp", 4)) code = ' ';
                else if (n >= 2 && name[0] == '#') {
                    int hex = name[1] == 'x' || name[1] == 'X';
                    size_t k = hex ? 2 : 1;
                    if (k >= n)
                        known = 0;
                    for (; k < n && known; ++k) {
                        int h = hex ? hex_value((unsigned char)name[k]) : (name[k] >= '0' && name[k] <= '9' ? name[k] - '0' : -1);
                        if (h < 0 || code > 0x10ffff)
                            known = 0;
                        else
                            code = code * (hex ? 16 : 10) + (unsigned long)h;
                    }
                    if (code > 0x10ffff)
                        known = 0;
                } else {
                    known = 0;
                }
                if (known) {
                    used += utf8_put(code, out + used);
                    i += n + 2;
                    continue;
                }
            }
        }
        out[used++] = text[i++];
    }
    return used;
}

static int tag_is(const char *name, size_t length, const char *word)
{
    size_t n = strlen(word);
    if (length < n)
        return 0;
    for (size_t i = 0; i < n; ++i)
        if (tolower((unsigned char)name[i]) != word[i])
            return 0;
    return length == n || !isalnum((unsigned char)name[n]);
}

static void html_text(text_sink *s, const char *text, size_t length)
{
    for (size_t i = 0; i < length;) {
        if (text[i] != '<') {
            sink_put(s, text[i] == '\t' || text[i] == '\r' || text[i] == '\n' ? ' ' : text[i]);
            ++i;
            continue;
        }
        const char *close = memchr(text + i, '>', length - i);
        if (!close) {
            break;
        }
        const char *name = text + i + 1;
        int closing = *name == '/';
        if (closing)
            ++name;
        size_t n = (size_t)(close - name);
        if (!closing && tag_is(name, n, "li"))
            sink_text(s, "\n\xe2\x80\xa2 ");
        else if (tag_is(name, n, "br") || tag_is(name, n, "p") || tag_is(name, n, "ul") || tag_is(name, n, "ol") ||
                 tag_is(name, n, "div") || (n >= 1 && tolower((unsigned char)name[0]) == 'h' && n >= 2 && isdigit((unsigned char)name[1])))
            sink_put(s, '\n');
        i = (size_t)(close - text) + 1;
    }
}

void yozora_release_notes_plain(const char *text, size_t length, int html, char *out, size_t capacity)
{
    if (!out || !capacity)
        return;
    out[0] = '\0';
    if (!text)
        return;
    text_sink sink = { out, capacity, 0, 0 };
    if (html) {
        char decoded[RAW_NOTES_MAX];
        if (length > sizeof decoded)
            length = sizeof decoded;
        char stripped[RAW_NOTES_MAX];
        size_t n = decode_entities(text, length, decoded, sizeof decoded);
        text_sink inner = { stripped, sizeof stripped, 0, 0 };
        html_text(&inner, decoded, n);
        sink_finish(&inner);
        n = decode_entities(stripped, inner.used, decoded, sizeof decoded);
        for (size_t i = 0; i < n; ++i)
            sink_put(&sink, decoded[i]);
    } else {
        markdown_text(&sink, text, length);
    }
    sink_finish(&sink);
    if (sink.used + 2 >= capacity) {
        size_t cut = sink.used;
        while (cut && out[cut - 1] != '\n')
            --cut;
        if (cut) {
            out[cut - 1] = '\0';
        }
    }
}

int yozora_version_compare(const char *a, const char *b, int *result)
{
    unsigned long left[4] = { 0, 0, 0, 0 }, right[4] = { 0, 0, 0, 0 };
    const char *sides[2] = { a, b };
    unsigned long *numbers[2] = { left, right };
    if (!a || !b || !result)
        return 0;
    for (int side = 0; side < 2; ++side) {
        const char *p = sides[side];
        while (*p && !isdigit((unsigned char)*p))
            ++p;
        if (!*p)
            return 0;
        for (int index = 0; index < 4 && isdigit((unsigned char)*p); ++index) {
            unsigned long v = 0;
            int digits = 0;
            while (isdigit((unsigned char)*p)) {
                if (digits++ < 9)
                    v = v * 10 + (unsigned long)(*p - '0');
                ++p;
            }
            numbers[side][index] = v;
            if (*p == '.' && isdigit((unsigned char)p[1]))
                ++p;
            else
                break;
        }
    }
    *result = 0;
    for (int i = 0; i < 4; ++i) {
        if (left[i] != right[i]) {
            *result = left[i] < right[i] ? -1 : 1;
            break;
        }
    }
    return 1;
}

int yozora_release_parse_sha256(const char *text, size_t length, uint8_t out[32])
{
    size_t i = 0;
    if (!text || !out)
        return 0;
    while (i < length && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r'))
        ++i;
    if (length - i < 64)
        return 0;
    for (int k = 0; k < 32; ++k) {
        int high = hex_value((unsigned char)text[i + 2 * k]);
        int low = hex_value((unsigned char)text[i + 2 * k + 1]);
        if (high < 0 || low < 0)
            return 0;
        out[k] = (uint8_t)(high * 16 + low);
    }
    i += 64;
    return i == length || text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r';
}

static void pick_assets(const char *tag, const asset *assets, size_t count, yozora_release *out)
{
    char ipa[ASSET_NAME_MAX], sha[ASSET_NAME_MAX];
    if (!build_asset_name(tag, ".ipa", ipa, sizeof ipa) || !build_asset_name(tag, ".ipa.sha256", sha, sizeof sha))
        return;
    for (size_t i = 0; i < count; ++i) {
        if (!starts_with(assets[i].url, YOZORA_RELEASE_DOWNLOAD_PREFIX))
            continue;
        if (!strcmp(assets[i].name, ipa)) {
            copy_bounded(out->ipa_url, sizeof out->ipa_url, assets[i].url);
            out->ipa_size = assets[i].size;
        } else if (!strcmp(assets[i].name, sha)) {
            copy_bounded(out->sha_url, sizeof out->sha_url, assets[i].url);
        }
    }
}

static int parse_assets(cursor *c, asset *assets, size_t *count)
{
    skip_space(c);
    if (c->p >= c->end || *c->p != '[')
        return skip_value(c, 1);
    ++c->p;
    skip_space(c);
    if (c->p < c->end && *c->p == ']') {
        ++c->p;
        return 1;
    }
    for (;;) {
        skip_space(c);
        if (c->p >= c->end || *c->p != '{')
            return 0;
        asset item;
        memset(&item, 0, sizeof item);
        ++c->p;
        skip_space(c);
        if (c->p < c->end && *c->p == '}') {
            ++c->p;
        } else {
            for (;;) {
                char key[32];
                skip_space(c);
                if (!parse_string(c, key, sizeof key))
                    return 0;
                skip_space(c);
                if (c->p >= c->end || *c->p != ':')
                    return 0;
                ++c->p;
                skip_space(c);
                if (!strcmp(key, "name")) {
                    if (!parse_string(c, item.name, sizeof item.name))
                        return 0;
                } else if (!strcmp(key, "browser_download_url")) {
                    if (!parse_string(c, item.url, sizeof item.url))
                        return 0;
                } else if (!strcmp(key, "size")) {
                    if (!read_number(c, &item.size))
                        return 0;
                } else if (!skip_value(c, 2)) {
                    return 0;
                }
                skip_space(c);
                if (c->p >= c->end)
                    return 0;
                if (*c->p == ',') {
                    ++c->p;
                    continue;
                }
                if (*c->p == '}') {
                    ++c->p;
                    break;
                }
                return 0;
            }
        }
        if (*count < MAX_ASSETS)
            assets[(*count)++] = item;
        skip_space(c);
        if (c->p >= c->end)
            return 0;
        if (*c->p == ',') {
            ++c->p;
            continue;
        }
        if (*c->p == ']') {
            ++c->p;
            return 1;
        }
        return 0;
    }
}

yozora_release_status yozora_release_parse_json(const char *json, size_t length, yozora_release *out)
{
    if (!json || !out)
        return YOZORA_RELEASE_BAD_ARGUMENT;
    memset(out, 0, sizeof *out);
    cursor c = { json, json + length };
    char raw[RAW_NOTES_MAX];
    asset assets[MAX_ASSETS];
    size_t asset_count = 0;
    int refused = 0;
    raw[0] = '\0';
    skip_space(&c);
    if (c.p >= c.end || *c.p != '{')
        return YOZORA_RELEASE_MALFORMED;
    ++c.p;
    skip_space(&c);
    if (c.p < c.end && *c.p == '}')
        return YOZORA_RELEASE_MALFORMED;
    for (;;) {
        char key[32];
        skip_space(&c);
        if (!parse_string(&c, key, sizeof key))
            return YOZORA_RELEASE_MALFORMED;
        skip_space(&c);
        if (c.p >= c.end || *c.p != ':')
            return YOZORA_RELEASE_MALFORMED;
        ++c.p;
        skip_space(&c);
        int ok;
        if (!strcmp(key, "tag_name") && c.p < c.end && *c.p == '"') {
            ok = parse_string(&c, out->tag, sizeof out->tag);
        } else if (!strcmp(key, "body") && c.p < c.end && *c.p == '"') {
            ok = parse_string(&c, raw, sizeof raw);
        } else if (!strcmp(key, "assets")) {
            ok = parse_assets(&c, assets, &asset_count);
        } else if ((!strcmp(key, "draft") || !strcmp(key, "prerelease")) && c.p < c.end && *c.p == 't') {
            ok = read_literal(&c, "true");
            refused = refused || ok;
        } else {
            ok = skip_value(&c, 1);
        }
        if (!ok)
            return YOZORA_RELEASE_MALFORMED;
        skip_space(&c);
        if (c.p >= c.end)
            return YOZORA_RELEASE_MALFORMED;
        if (*c.p == ',') {
            ++c.p;
            continue;
        }
        if (*c.p == '}')
            break;
        return YOZORA_RELEASE_MALFORMED;
    }
    if (refused || !tag_valid(out->tag)) {
        memset(out, 0, sizeof *out);
        return YOZORA_RELEASE_MALFORMED;
    }
    yozora_release_notes_plain(raw, strlen(raw), 0, out->notes, sizeof out->notes);
    pick_assets(out->tag, assets, asset_count, out);
    return YOZORA_RELEASE_OK;
}

static const char *find_text(const char *text, const char *end, const char *needle)
{
    size_t n = strlen(needle);
    for (; (size_t)(end - text) >= n; ++text)
        if (!memcmp(text, needle, n))
            return text;
    return NULL;
}

yozora_release_status yozora_release_parse_atom(const char *feed, size_t length, yozora_release *out)
{
    if (!feed || !out)
        return YOZORA_RELEASE_BAD_ARGUMENT;
    memset(out, 0, sizeof *out);
    const char *end = feed + length;
    const char *entry = find_text(feed, end, "<entry>");
    if (!entry)
        return YOZORA_RELEASE_MALFORMED;
    const char *entry_end = find_text(entry, end, "</entry>");
    if (entry_end)
        end = entry_end;
    const char *marker = find_text(entry, end, "/releases/tag/");
    if (!marker)
        return YOZORA_RELEASE_MALFORMED;
    marker += strlen("/releases/tag/");
    const char *stop = marker;
    while (stop < end && *stop != '"' && *stop != '\'')
        ++stop;
    if (stop == marker || (size_t)(stop - marker) >= sizeof out->tag) {
        return YOZORA_RELEASE_MALFORMED;
    }
    memcpy(out->tag, marker, (size_t)(stop - marker));
    out->tag[stop - marker] = '\0';
    if (!tag_valid(out->tag)) {
        memset(out, 0, sizeof *out);
        return YOZORA_RELEASE_MALFORMED;
    }
    const char *content = find_text(entry, end, "<content");
    if (content) {
        const char *open = memchr(content, '>', (size_t)(end - content));
        const char *close = open ? find_text(open, end, "</content>") : NULL;
        if (open && close)
            yozora_release_notes_plain(open + 1, (size_t)(close - open - 1), 1, out->notes, sizeof out->notes);
    }
    build_download_url(out->tag, ".ipa", out->ipa_url, sizeof out->ipa_url);
    build_download_url(out->tag, ".ipa.sha256", out->sha_url, sizeof out->sha_url);
    return YOZORA_RELEASE_OK;
}
