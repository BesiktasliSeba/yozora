#include "yozora_playlist.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

static int has_prefix_nocase(const char *text, const char *prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*text) != *prefix) return 0;
        ++text;
        ++prefix;
    }
    return 1;
}

/* the length of the scheme of ref when it has one ("http" and the colon not counted), 0 when it has none */
static size_t scheme_length(const char *ref) {
    size_t i = 0;
    if (!isalpha((unsigned char)ref[0])) return 0;
    for (i = 1; ref[i]; ++i) {
        unsigned char c = (unsigned char)ref[i];
        if (c == ':') return i;
        if (!isalnum(c) && c != '+' && c != '-' && c != '.') return 0;
    }
    return 0;
}

/* the length of "scheme://authority" at the start of an http or https address, 0 when it is neither */
static size_t origin_length(const char *url) {
    size_t start, i;
    if (has_prefix_nocase(url, "https://")) start = 8;
    else if (has_prefix_nocase(url, "http://")) start = 7;
    else return 0;
    for (i = start; url[i] && url[i] != '/' && url[i] != '?' && url[i] != '#'; ++i) {}
    return i > start ? i : 0;
}

/* removes . and .. segments from the path in place; the path ends at the first ? or the end */
static void remove_dot_segments(char *path) {
    char *query = strchr(path, '?');
    size_t path_length = query ? (size_t)(query - path) : strlen(path);
    char out[YOZORA_URL_MAX];
    size_t used = 0, i = 0;
    if (path_length == 0 || path_length >= sizeof out) return;
    while (i < path_length) {
        size_t rest = path_length - i;
        if (rest >= 3 && !memcmp(path + i, "../", 3)) { i += 3; continue; }
        if (rest >= 2 && !memcmp(path + i, "./", 2)) { i += 2; continue; }
        if (rest >= 3 && !memcmp(path + i, "/./", 3)) { i += 2; continue; }
        if (rest == 2 && !memcmp(path + i, "/.", 2)) { out[used++] = '/'; i += 2; continue; }
        if (rest >= 4 && !memcmp(path + i, "/../", 4)) {
            while (used > 0 && out[used - 1] != '/') --used;
            if (used > 0) --used;
            i += 3;
            continue;
        }
        if (rest == 3 && !memcmp(path + i, "/..", 3)) {
            while (used > 0 && out[used - 1] != '/') --used;
            if (used > 0) --used;
            out[used++] = '/';
            i += 3;
            continue;
        }
        if ((rest == 1 && path[i] == '.') || (rest == 2 && !memcmp(path + i, "..", 2))) { i += rest; continue; }
        /* one segment: the slash that opens it and everything up to the next slash */
        out[used++] = path[i++];
        while (i < path_length && path[i] != '/') out[used++] = path[i++];
    }
    memmove(path + used, path + path_length, strlen(path + path_length) + 1);
    memcpy(path, out, used);
}

yozora_playlist_status yozora_url_resolve(const char *base, const char *ref, char *out, size_t capacity) {
    /* a base and a reference of up to the bound each, joined, always fit */
    char built[2 * YOZORA_URL_MAX];
    size_t origin, ref_length, scheme, used = 0, hash;
    if (!out || !capacity) return YOZORA_PLAYLIST_BAD_ARGUMENT;
    out[0] = '\0';
    if (!base || !ref) return YOZORA_PLAYLIST_BAD_ARGUMENT;
    origin = origin_length(base);
    if (!origin) return YOZORA_PLAYLIST_BAD_URL;
    while (*ref == ' ' || *ref == '\t') ++ref;
    ref_length = strlen(ref);
    while (ref_length && (ref[ref_length - 1] == ' ' || ref[ref_length - 1] == '\t' || ref[ref_length - 1] == '\r')) --ref_length;
    if (!ref_length) return YOZORA_PLAYLIST_BAD_URL;
    if (ref_length >= YOZORA_URL_MAX || strlen(base) >= YOZORA_URL_MAX) return YOZORA_PLAYLIST_TOO_LARGE;
    /* the fragment never goes to a server */
    for (hash = 0; hash < ref_length && ref[hash] != '#'; ++hash) {}
    ref_length = hash;
    if (!ref_length) {
        /* a reference that is only a fragment is the base itself */
        size_t base_length = strlen(base);
        for (hash = 0; hash < base_length && base[hash] != '#'; ++hash) {}
        if (hash >= capacity) return YOZORA_PLAYLIST_TOO_LARGE;
        memcpy(out, base, hash);
        out[hash] = '\0';
        return YOZORA_PLAYLIST_OK;
    }
    scheme = scheme_length(ref);
    if (scheme) {
        if (!has_prefix_nocase(ref, "http:") && !has_prefix_nocase(ref, "https:")) return YOZORA_PLAYLIST_REFUSED;
        if (!origin_length(ref)) return YOZORA_PLAYLIST_BAD_URL;
        memcpy(built, ref, ref_length);
        used = ref_length;
    } else if (ref_length >= 2 && ref[0] == '/' && ref[1] == '/') {
        size_t colon = has_prefix_nocase(base, "https:") ? 6 : 5;
        memcpy(built, base, colon);
        memcpy(built + colon, ref, ref_length);
        used = colon + ref_length;
    } else if (ref[0] == '/') {
        memcpy(built, base, origin);
        memcpy(built + origin, ref, ref_length);
        used = origin + ref_length;
    } else {
        /* the base without its query and fragment, then either the query alone or the directory of the base path and the reference */
        size_t base_end = strlen(base), directory;
        size_t i;
        for (i = origin; i < base_end; ++i)
            if (base[i] == '?' || base[i] == '#') { base_end = i; break; }
        if (ref[0] == '?') {
            memcpy(built, base, base_end);
            memcpy(built + base_end, ref, ref_length);
            used = base_end + ref_length;
        } else {
            directory = base_end;
            while (directory > origin && base[directory - 1] != '/') --directory;
            if (directory == origin) {
                /* a base with an authority and no path: the reference hangs off the root */
                memcpy(built, base, origin);
                built[origin] = '/';
                directory = origin + 1;
                memcpy(built + directory, ref, ref_length);
                used = directory + ref_length;
            } else {
                memcpy(built, base, directory);
                memcpy(built + directory, ref, ref_length);
                used = directory + ref_length;
            }
        }
    }
    if (used >= sizeof built) return YOZORA_PLAYLIST_TOO_LARGE;
    built[used] = '\0';
    /* dot segments are removed from the path only, which starts after the authority */
    remove_dot_segments(built + origin_length(built));
    if (strlen(built) >= capacity || strlen(built) >= YOZORA_URL_MAX) return YOZORA_PLAYLIST_TOO_LARGE;
    memcpy(out, built, strlen(built) + 1);
    return YOZORA_PLAYLIST_OK;
}

int yozora_playlist_is(const unsigned char *data, size_t length) {
    size_t i = 0;
    if (!data) return 0;
    if (length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) i = 3;
    while (i < length && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' || data[i] == '\n')) ++i;
    return length - i >= 7 && !memcmp(data + i, "#EXTM3U", 7);
}

typedef struct {
    char *out;
    size_t capacity;
    size_t used;
} sink;

static int put(sink *s, const char *bytes, size_t length) {
    if (length >= s->capacity - s->used) return 0;
    memcpy(s->out + s->used, bytes, length);
    s->used += length;
    return 1;
}

/* the address in [start, start+length) goes through resolution and the map; other schemes are copied as they are */
static yozora_playlist_status put_address(sink *s, const char *start, size_t length, const char *base, yozora_playlist_map map, void *context) {
    char ref[YOZORA_URL_MAX], absolute[YOZORA_URL_MAX], mapped[YOZORA_URL_MAX];
    yozora_playlist_status status;
    if (length == 0 || length >= sizeof ref) return length ? YOZORA_PLAYLIST_TOO_LARGE : (put(s, start, 0) ? YOZORA_PLAYLIST_OK : YOZORA_PLAYLIST_TOO_LARGE);
    memcpy(ref, start, length);
    ref[length] = '\0';
    status = yozora_url_resolve(base, ref, absolute, sizeof absolute);
    if (status == YOZORA_PLAYLIST_REFUSED) return put(s, start, length) ? YOZORA_PLAYLIST_OK : YOZORA_PLAYLIST_TOO_LARGE;
    if (status != YOZORA_PLAYLIST_OK) return status;
    if (map(context, absolute, mapped, sizeof mapped) != 0) return YOZORA_PLAYLIST_MAP_FAILED;
    return put(s, mapped, strlen(mapped)) ? YOZORA_PLAYLIST_OK : YOZORA_PLAYLIST_TOO_LARGE;
}

/* a tag line: every URI="..." value is replaced, the rest of the line is copied */
static yozora_playlist_status rewrite_tag(sink *s, const char *line, size_t length, const char *base, yozora_playlist_map map, void *context) {
    size_t i = 0, copied = 0;
    while (i + 5 <= length) {
        size_t value, end;
        yozora_playlist_status status;
        /* the attribute name ends before the =: URI is its own attribute, GROUP-ID or another name that holds it is not */
        if (memcmp(line + i, "URI=\"", 5) || (i > 0 && line[i - 1] != ',' && line[i - 1] != ':' && line[i - 1] != ' ')) { ++i; continue; }
        value = i + 5;
        for (end = value; end < length && line[end] != '"'; ++end) {}
        if (end >= length) break;
        if (!put(s, line + copied, value - copied)) return YOZORA_PLAYLIST_TOO_LARGE;
        status = put_address(s, line + value, end - value, base, map, context);
        if (status != YOZORA_PLAYLIST_OK) return status;
        copied = end;
        i = end + 1;
    }
    return put(s, line + copied, length - copied) ? YOZORA_PLAYLIST_OK : YOZORA_PLAYLIST_TOO_LARGE;
}

yozora_playlist_status yozora_playlist_rewrite(const char *text, size_t length, const char *base, yozora_playlist_map map, void *context,
                                               char *out, size_t capacity, size_t *written) {
    sink s;
    size_t position = 0;
    if (written) *written = 0;
    if (!text || !base || !map || !out || !capacity) return YOZORA_PLAYLIST_BAD_ARGUMENT;
    out[0] = '\0';
    s.out = out;
    s.capacity = capacity;
    s.used = 0;
    while (position < length) {
        size_t line_end = position, content, terminator;
        yozora_playlist_status status;
        while (line_end < length && text[line_end] != '\n') ++line_end;
        terminator = line_end < length ? 1 : 0;
        content = line_end;
        if (content > position && text[content - 1] == '\r') { --content; ++terminator; }
        if (content - position > YOZORA_PLAYLIST_LINE_MAX) {
            out[0] = '\0';
            return YOZORA_PLAYLIST_TOO_LARGE;
        }
        if (content > position && text[position] == '#') {
            status = rewrite_tag(&s, text + position, content - position, base, map, context);
        } else {
            size_t first = position, last = content;
            while (first < last && (text[first] == ' ' || text[first] == '\t')) ++first;
            while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t')) --last;
            if (first == last) {
                status = put(&s, text + position, content - position) ? YOZORA_PLAYLIST_OK : YOZORA_PLAYLIST_TOO_LARGE;
            } else {
                status = put(&s, text + position, first - position) ? YOZORA_PLAYLIST_OK : YOZORA_PLAYLIST_TOO_LARGE;
                if (status == YOZORA_PLAYLIST_OK) status = put_address(&s, text + first, last - first, base, map, context);
                if (status == YOZORA_PLAYLIST_OK && !put(&s, text + last, content - last)) status = YOZORA_PLAYLIST_TOO_LARGE;
            }
        }
        if (status != YOZORA_PLAYLIST_OK) {
            out[0] = '\0';
            return status;
        }
        if (terminator && !put(&s, text + content, terminator)) {
            out[0] = '\0';
            return YOZORA_PLAYLIST_TOO_LARGE;
        }
        position = line_end + (line_end < length ? 1 : 0);
    }
    out[s.used] = '\0';
    if (written) *written = s.used;
    return YOZORA_PLAYLIST_OK;
}
