/* glibc hides the POSIX socket and thread calls from a strict c99 build */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "yozora_stream_proxy.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "yozora_playlist.h"

#define MAX_REDIRECTS 5
#define TOKEN_BYTES 16
#define TOKEN_CHARS (TOKEN_BYTES * 2)
/* the rewritten playlist is larger than the source, every relative address becomes a full one */
#define REWRITTEN_MAX (4 * 1024 * 1024)
#define SEND_CHUNK 65536

typedef struct {
    char *url;
} entry;

typedef struct {
    unsigned id;
    int live;
    char token[TOKEN_CHARS + 1];
    char *cookie;
    char *referer;
    char host[256];
    int https;
    entry *entries;
    size_t count;
    size_t capacity;
    /* a local stream (see yozora_proxy_register_local) has these and no entries */
    yozora_proxy_local_read local_read;
    yozora_proxy_local_ref local_retain;
    yozora_proxy_local_ref local_release;
    void *local_context;
} stream;

struct yozora_proxy {
    int listen_fd;
    int port;
    pthread_t acceptor;
    int acceptor_started;
    pthread_mutex_t lock;
    pthread_cond_t idle;
    int stopping;
    /* the listening socket is gone (the system takes them from an app it suspended) and the accepting thread has ended */
    int dead;
    unsigned active;
    unsigned next_stream;
    stream streams[YOZORA_PROXY_MAX_STREAMS];
    /* the sockets of the transfers under way, shut down by stop so that a thread blocked on one returns */
    int client_fds[YOZORA_PROXY_MAX_CONNECTIONS];
    char ca_bundle[1024];
    char user_agent[256];
};

/* helpers */

static int has_prefix_nocase(const char *text, const char *prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) return 0;
        ++text;
        ++prefix;
    }
    return 1;
}

/* the lowercase host of an http or https address, without a port or a user name; 0 when there is none */
static int host_of(const char *url, char *out, size_t capacity, int *https) {
    const char *start, *end;
    size_t length, i;
    if (has_prefix_nocase(url, "https://")) {
        start = url + 8;
        if (https) *https = 1;
    } else if (has_prefix_nocase(url, "http://")) {
        start = url + 7;
        if (https) *https = 0;
    } else {
        return 0;
    }
    end = start;
    while (*end && *end != '/' && *end != '?' && *end != '#') ++end;
    for (i = 0; start + i < end; ++i)
        if (start[i] == '@') return 0;
    if (*start == '[') {
        const char *close = memchr(start, ']', (size_t)(end - start));
        if (!close) return 0;
        end = close + 1;
    } else {
        const char *colon = memchr(start, ':', (size_t)(end - start));
        if (colon) end = colon;
    }
    length = (size_t)(end - start);
    if (!length || length >= capacity) return 0;
    for (i = 0; i < length; ++i) out[i] = (char)tolower((unsigned char)start[i]);
    out[length] = '\0';
    return 1;
}

static int send_all(int fd, const char *data, size_t length) {
    while (length) {
        ssize_t n;
#ifdef MSG_NOSIGNAL
        n = send(fd, data, length > SEND_CHUNK ? SEND_CHUNK : length, MSG_NOSIGNAL);
#else
        n = send(fd, data, length > SEND_CHUNK ? SEND_CHUNK : length, 0);
#endif
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        data += n;
        length -= (size_t)n;
    }
    return 1;
}

static char *copy_string(const char *text) {
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy) memcpy(copy, text, length + 1);
    return copy;
}

static int random_token(char *out) {
    static const char digits[] = "0123456789abcdef";
    unsigned char bytes[TOKEN_BYTES];
    size_t got = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    int i;
    if (fd < 0) return 0;
    while (got < sizeof bytes) {
        ssize_t n = read(fd, bytes + got, sizeof bytes - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    if (got != sizeof bytes) return 0;
    for (i = 0; i < TOKEN_BYTES; ++i) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    out[TOKEN_CHARS] = '\0';
    return 1;
}

/* request */

yozora_proxy_request_status yozora_proxy_parse_request(const char *head, size_t length, yozora_proxy_request *out) {
    const char *line_end, *path, *path_end, *cursor, *end = head + length;
    size_t i, method_length;
    if (!head || !out) return YOZORA_PROXY_REQUEST_BAD;
    memset(out, 0, sizeof *out);
    if (length == 0 || length > YOZORA_PROXY_HEAD_MAX) return YOZORA_PROXY_REQUEST_BAD;
    line_end = memchr(head, '\n', length);
    if (!line_end) return YOZORA_PROXY_REQUEST_BAD;
    for (i = 0; i < length; ++i)
        if (head[i] == '\0') return YOZORA_PROXY_REQUEST_BAD;
    /* the first line: METHOD SP target SP HTTP/1.x */
    path = memchr(head, ' ', (size_t)(line_end - head));
    if (!path) return YOZORA_PROXY_REQUEST_BAD;
    method_length = (size_t)(path - head);
    ++path;
    path_end = memchr(path, ' ', (size_t)(line_end - path));
    if (!path_end || path_end == path) return YOZORA_PROXY_REQUEST_BAD;
    cursor = path_end + 1;
    if (end - cursor < 8 || memcmp(cursor, "HTTP/1.", 7)) return YOZORA_PROXY_REQUEST_BAD;
    if (!(cursor[7] == '0' || cursor[7] == '1')) return YOZORA_PROXY_REQUEST_BAD;
    if (method_length == 4 && !memcmp(head, "HEAD", 4)) out->head_only = 1;
    else if (!(method_length == 3 && !memcmp(head, "GET", 3))) return YOZORA_PROXY_REQUEST_METHOD;
    /* the target is /s/<32 lowercase hex>/<index>[.ext] and nothing else, no query and no dots up the tree */
    if (path_end - path < 3 + TOKEN_CHARS + 2 || memcmp(path, "/s/", 3)) return YOZORA_PROXY_REQUEST_NOT_FOUND;
    for (i = 0; i < TOKEN_CHARS; ++i) {
        char c = path[3 + i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return YOZORA_PROXY_REQUEST_NOT_FOUND;
        out->token[i] = c;
    }
    out->token[TOKEN_CHARS] = '\0';
    cursor = path + 3 + TOKEN_CHARS;
    if (cursor >= path_end || *cursor != '/') return YOZORA_PROXY_REQUEST_NOT_FOUND;
    ++cursor;
    /* the name: letters, digits and . _ - only, so no slash, no query and no way up the tree. a name that does not start with a dot */
    {
        const char *name_start = cursor;
        if (cursor >= path_end || path_end - cursor > (ptrdiff_t)sizeof out->name - 1 || *cursor == '.') return YOZORA_PROXY_REQUEST_NOT_FOUND;
        for (; cursor < path_end; ++cursor)
            if (!isalnum((unsigned char)*cursor) && *cursor != '.' && *cursor != '_' && *cursor != '-') return YOZORA_PROXY_REQUEST_NOT_FOUND;
        memcpy(out->name, name_start, (size_t)(path_end - name_start));
        out->name[path_end - name_start] = '\0';
        cursor = name_start;
    }
    /* an address of a network stream: digits, then an extension of letters and digits that only tells the
       system player what the stream is */
    if (isdigit((unsigned char)*cursor)) {
        unsigned long index = 0;
        int digits = 0, valid = 1;
        while (cursor < path_end && isdigit((unsigned char)*cursor)) {
            index = index * 10 + (unsigned long)(*cursor - '0');
            if (++digits > 6) { valid = 0; break; }
            ++cursor;
        }
        if (valid && index >= YOZORA_PROXY_MAX_ENTRIES) valid = 0;
        if (valid && cursor < path_end) {
            if (*cursor != '.' || path_end - cursor < 2 || path_end - cursor > 8) valid = 0;
            else
                for (++cursor; cursor < path_end; ++cursor)
                    if (!isalnum((unsigned char)*cursor)) { valid = 0; break; }
        }
        if (valid) {
            out->numeric = 1;
            out->index = (unsigned)index;
        }
    }
    /* the headers: only a single bytes range is passed on */
    cursor = line_end + 1;
    while (cursor < end) {
        const char *next = memchr(cursor, '\n', (size_t)(end - cursor));
        const char *value;
        size_t name_length;
        if (!next) return YOZORA_PROXY_REQUEST_BAD;
        if (next - cursor <= 1 && (next == cursor || *cursor == '\r')) break;
        value = memchr(cursor, ':', (size_t)(next - cursor));
        if (value) {
            name_length = (size_t)(value - cursor);
            if (name_length == 5 && has_prefix_nocase(cursor, "range")) {
                const char *p = value + 1, *stop = next;
                size_t used = 0;
                int digits_before = 0, digits_after = 0;
                if (stop > p && stop[-1] == '\r') --stop;
                while (p < stop && (*p == ' ' || *p == '\t')) ++p;
                while (stop > p && (stop[-1] == ' ' || stop[-1] == '\t')) --stop;
                if (out->range[0] || stop - p < 7 || !has_prefix_nocase(p, "bytes=")) return YOZORA_PROXY_REQUEST_RANGE;
                p += 6;
                while (p < stop && isdigit((unsigned char)*p)) {
                    if (used + 1 >= sizeof out->range || ++digits_before > 18) return YOZORA_PROXY_REQUEST_RANGE;
                    out->range[used++] = *p++;
                }
                if (p >= stop || *p != '-') return YOZORA_PROXY_REQUEST_RANGE;
                out->range[used++] = *p++;
                while (p < stop && isdigit((unsigned char)*p)) {
                    if (used + 1 >= sizeof out->range || ++digits_after > 18) return YOZORA_PROXY_REQUEST_RANGE;
                    out->range[used++] = *p++;
                }
                /* anything left (a comma, a second range, text) is more than the proxy passes on; a bare "-" asks for nothing */
                if (p != stop || (!digits_before && !digits_after)) return YOZORA_PROXY_REQUEST_RANGE;
                out->range[used] = '\0';
            }
        }
        cursor = next + 1;
    }
    return YOZORA_PROXY_REQUEST_OK;
}

/* streams */

typedef struct {
    char url[YOZORA_URL_MAX];
    char cookie[YOZORA_PROXY_MAX_COOKIE + 1];
    char referer[YOZORA_PROXY_MAX_REFERER + 1];
    char host[256];
    int https;
} stream_copy;

static stream *find_by_token(yozora_proxy *proxy, const char *token) {
    int i;
    for (i = 0; i < YOZORA_PROXY_MAX_STREAMS; ++i)
        if (proxy->streams[i].live && !strcmp(proxy->streams[i].token, token)) return &proxy->streams[i];
    return NULL;
}

static void free_stream(stream *s) {
    size_t i;
    if (s->local_release) s->local_release(s->local_context);
    for (i = 0; i < s->count; ++i) free(s->entries[i].url);
    free(s->entries);
    free(s->cookie);
    free(s->referer);
    memset(s, 0, sizeof *s);
}

/* the path extension of an address (m3u8, ts, mp4), which the system player reads as the kind of stream,
   empty when none */
static void extension_of(const char *url, char *out, size_t capacity) {
    const char *start = strstr(url, "://"), *end, *dot;
    out[0] = '\0';
    if (!start) return;
    start += 3;
    start = strchr(start, '/');
    if (!start) return;
    end = start;
    while (*end && *end != '?' && *end != '#') ++end;
    dot = end;
    while (dot > start && *dot != '.' && *dot != '/') --dot;
    if (*dot != '.' || end - dot < 2 || end - dot > 6 || capacity < (size_t)(end - dot) + 1) return;
    for (const char *p = dot + 1; p < end; ++p)
        if (!isalnum((unsigned char)*p)) return;
    memcpy(out, dot, (size_t)(end - dot));
    out[end - dot] = '\0';
}

static int local_url(const yozora_proxy *proxy, const stream *s, size_t index, const char *origin, char *out, size_t capacity) {
    char extension[8];
    extension_of(origin, extension, sizeof extension);
    return snprintf(out, capacity, "http://127.0.0.1:%d/s/%s/%zu%s", proxy->port, s->token, index, extension) < (int)capacity;
}

/* adds an address to a live stream (once: the same address gets the same entry) and gives its proxy address; 0 on success */
static int add_entry(yozora_proxy *proxy, stream *s, const char *url, char *out, size_t capacity) {
    size_t i;
    for (i = 0; i < s->count; ++i)
        if (!strcmp(s->entries[i].url, url)) return local_url(proxy, s, i, url, out, capacity) ? 0 : 1;
    if (s->count >= YOZORA_PROXY_MAX_ENTRIES) return 1;
    if (s->count == s->capacity) {
        size_t grown = s->capacity ? s->capacity * 2 : 32;
        entry *bigger;
        if (grown > YOZORA_PROXY_MAX_ENTRIES) grown = YOZORA_PROXY_MAX_ENTRIES;
        bigger = realloc(s->entries, grown * sizeof *bigger);
        if (!bigger) return 1;
        s->entries = bigger;
        s->capacity = grown;
    }
    s->entries[s->count].url = copy_string(url);
    if (!s->entries[s->count].url) return 1;
    ++s->count;
    return local_url(proxy, s, s->count - 1, url, out, capacity) ? 0 : 1;
}

yozora_proxy_status yozora_proxy_register(yozora_proxy *proxy, const char *origin_url, const char *cookie, const char *referer,
                                          char *out, size_t capacity, unsigned *stream_id) {
    stream *s = NULL;
    char host[256];
    int https = 0, i;
    yozora_proxy_status status = YOZORA_PROXY_OK;
    if (out && capacity) out[0] = '\0';
    if (!proxy || !origin_url || !out || !capacity) return YOZORA_PROXY_BAD_ARGUMENT;
    if (strlen(origin_url) >= YOZORA_URL_MAX || (cookie && strlen(cookie) > YOZORA_PROXY_MAX_COOKIE) || (referer && strlen(referer) > YOZORA_PROXY_MAX_REFERER))
        return YOZORA_PROXY_BAD_ARGUMENT;
    if (!host_of(origin_url, host, sizeof host, &https)) return YOZORA_PROXY_BAD_URL;
    /* a header value with a line break would split the request that carries it */
    for (const char *p = cookie ? cookie : ""; *p; ++p)
        if ((unsigned char)*p < 0x20 && *p != '\t') return YOZORA_PROXY_BAD_ARGUMENT;
    for (const char *p = referer ? referer : ""; *p; ++p)
        if ((unsigned char)*p < 0x20 && *p != '\t') return YOZORA_PROXY_BAD_ARGUMENT;
    for (const char *p = origin_url; *p; ++p)
        if ((unsigned char)*p <= 0x20 || (unsigned char)*p == 0x7f) return YOZORA_PROXY_BAD_URL;
    pthread_mutex_lock(&proxy->lock);
    if (proxy->stopping) {
        pthread_mutex_unlock(&proxy->lock);
        return YOZORA_PROXY_SYSTEM;
    }
    for (i = 0; i < YOZORA_PROXY_MAX_STREAMS; ++i) {
        if (!proxy->streams[i].live) {
            s = &proxy->streams[i];
            break;
        }
    }
    if (!s) {
        pthread_mutex_unlock(&proxy->lock);
        return YOZORA_PROXY_LIMIT;
    }
    memset(s, 0, sizeof *s);
    if (!random_token(s->token)) {
        pthread_mutex_unlock(&proxy->lock);
        return YOZORA_PROXY_SYSTEM;
    }
    s->id = ++proxy->next_stream;
    s->cookie = copy_string(cookie ? cookie : "");
    s->referer = copy_string(referer ? referer : "");
    snprintf(s->host, sizeof s->host, "%s", host);
    s->https = https;
    s->live = 1;
    if (!s->cookie || !s->referer || add_entry(proxy, s, origin_url, out, capacity)) {
        status = YOZORA_PROXY_SYSTEM;
        free_stream(s);
        out[0] = '\0';
    } else if (stream_id) {
        *stream_id = s->id;
    }
    pthread_mutex_unlock(&proxy->lock);
    return status;
}

yozora_proxy_status yozora_proxy_register_local(yozora_proxy *proxy, yozora_proxy_local_read read, yozora_proxy_local_ref retain, yozora_proxy_local_ref release,
                                                void *context, char *out, size_t capacity, unsigned *stream_id) {
    stream *s = NULL;
    int i;
    if (out && capacity) out[0] = '\0';
    if (!proxy || !read || !retain || !release || !out || !capacity) return YOZORA_PROXY_BAD_ARGUMENT;
    pthread_mutex_lock(&proxy->lock);
    if (proxy->stopping) {
        pthread_mutex_unlock(&proxy->lock);
        return YOZORA_PROXY_SYSTEM;
    }
    for (i = 0; i < YOZORA_PROXY_MAX_STREAMS; ++i) {
        if (!proxy->streams[i].live) {
            s = &proxy->streams[i];
            break;
        }
    }
    if (!s) {
        pthread_mutex_unlock(&proxy->lock);
        return YOZORA_PROXY_LIMIT;
    }
    memset(s, 0, sizeof *s);
    if (!random_token(s->token) || snprintf(out, capacity, "http://127.0.0.1:%d/s/%s/", proxy->port, s->token) >= (int)capacity) {
        out[0] = '\0';
        memset(s, 0, sizeof *s);
        pthread_mutex_unlock(&proxy->lock);
        return YOZORA_PROXY_SYSTEM;
    }
    s->id = ++proxy->next_stream;
    s->local_read = read;
    s->local_retain = retain;
    s->local_release = release;
    s->local_context = context;
    /* the stream keeps the reference that the caller gives it, released with the stream */
    s->live = 1;
    if (stream_id) *stream_id = s->id;
    pthread_mutex_unlock(&proxy->lock);
    return YOZORA_PROXY_OK;
}

void yozora_proxy_release(yozora_proxy *proxy, unsigned stream_id) {
    int i;
    if (!proxy || !stream_id) return;
    pthread_mutex_lock(&proxy->lock);
    for (i = 0; i < YOZORA_PROXY_MAX_STREAMS; ++i)
        if (proxy->streams[i].live && proxy->streams[i].id == stream_id) free_stream(&proxy->streams[i]);
    pthread_mutex_unlock(&proxy->lock);
}

static int copy_entry(yozora_proxy *proxy, const yozora_proxy_request *request, stream_copy *copy, unsigned *stream_id) {
    stream *s;
    int found = 0;
    pthread_mutex_lock(&proxy->lock);
    s = find_by_token(proxy, request->token);
    if (s && request->numeric && !s->local_read && request->index < s->count) {
        snprintf(copy->url, sizeof copy->url, "%s", s->entries[request->index].url);
        snprintf(copy->cookie, sizeof copy->cookie, "%s", s->cookie);
        snprintf(copy->referer, sizeof copy->referer, "%s", s->referer);
        snprintf(copy->host, sizeof copy->host, "%s", s->host);
        copy->https = s->https;
        *stream_id = s->id;
        found = 1;
    }
    pthread_mutex_unlock(&proxy->lock);
    return found;
}

static int stream_is_live(yozora_proxy *proxy, unsigned stream_id) {
    int i, live = 0;
    pthread_mutex_lock(&proxy->lock);
    for (i = 0; i < YOZORA_PROXY_MAX_STREAMS; ++i)
        if (proxy->streams[i].live && proxy->streams[i].id == stream_id) live = 1;
    pthread_mutex_unlock(&proxy->lock);
    return live;
}

/* transfer */

enum { MODE_UNDECIDED = 0, MODE_DISCARD, MODE_BUFFER, MODE_STREAM };

typedef struct {
    yozora_proxy *proxy;
    unsigned stream_id;
    int fd;
    int head_only;
    int status;
    char ctype[160];
    char clength[24];
    char crange[96];
    int accepts_ranges;
    char location[YOZORA_URL_MAX];
    int mode;
    int head_sent;
    int send_failed;
    int too_large;
    char *buffer;
    size_t buffered;
} transfer;

static const char *status_text(int status) {
    switch (status) {
    case 200: return "OK";
    case 206: return "Partial Content";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 410: return "Gone";
    case 416: return "Range Not Satisfiable";
    case 431: return "Request Header Fields Too Large";
    case 503: return "Service Unavailable";
    default: return "Bad Gateway";
    }
}

/* the answers of a server that the player gets as they are; every other status of the server is a failure of the proxy */
static int passes_through(int status) {
    return status == 200 || status == 206 || status == 403 || status == 404 || status == 410 || status == 416;
}

static int is_redirect(int status) { return status == 301 || status == 302 || status == 303 || status == 307 || status == 308; }

static int send_text_answer(int fd, int status) {
    char message[256];
    const char *text = status_text(status);
    int length = snprintf(message, sizeof message,
                          "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n%s\n",
                          status, text, strlen(text) + 1, text);
    return length > 0 && (size_t)length < sizeof message && send_all(fd, message, (size_t)length);
}

static int control_free(const char *text) {
    for (; *text; ++text)
        if ((unsigned char)*text < 0x20 || (unsigned char)*text == 0x7f) return 0;
    return 1;
}

static int send_head(transfer *t, int status, const char *ctype, const char *length, const char *range, int ranges) {
    char head[1024];
    int n = snprintf(head, sizeof head, "HTTP/1.1 %d %s\r\n", status, status_text(status));
    size_t used;
    if (n <= 0 || (size_t)n >= sizeof head) return 0;
    used = (size_t)n;
    if (ctype && ctype[0] && control_free(ctype)) {
        n = snprintf(head + used, sizeof head - used, "Content-Type: %s\r\n", ctype);
        if (n <= 0 || (size_t)n >= sizeof head - used) return 0;
        used += (size_t)n;
    }
    if (length && length[0]) {
        n = snprintf(head + used, sizeof head - used, "Content-Length: %s\r\n", length);
        if (n <= 0 || (size_t)n >= sizeof head - used) return 0;
        used += (size_t)n;
    }
    if (range && range[0] && control_free(range)) {
        n = snprintf(head + used, sizeof head - used, "Content-Range: %s\r\n", range);
        if (n <= 0 || (size_t)n >= sizeof head - used) return 0;
        used += (size_t)n;
    }
    n = snprintf(head + used, sizeof head - used, "%sConnection: close\r\nCache-Control: no-store\r\n\r\n", ranges ? "Accept-Ranges: bytes\r\n" : "");
    if (n <= 0 || (size_t)n >= sizeof head - used) return 0;
    used += (size_t)n;
    t->head_sent = 1;
    if (!send_all(t->fd, head, used)) {
        t->send_failed = 1;
        return 0;
    }
    return 1;
}

static int digits_only(const char *text) {
    if (!*text) return 0;
    for (; *text; ++text)
        if (!isdigit((unsigned char)*text)) return 0;
    return 1;
}

static size_t header_callback(char *buffer, size_t size, size_t count, void *user) {
    transfer *t = user;
    size_t length = size * count, name_length;
    /* a location of a CDN carries a signed query that runs to thousands of bytes */
    char line[YOZORA_URL_MAX + 160], *colon, *value;
    if (length >= sizeof line) return length;
    memcpy(line, buffer, length);
    while (length && (line[length - 1] == '\r' || line[length - 1] == '\n')) --length;
    line[length] = '\0';
    if (!strncmp(line, "HTTP/", 5)) {
        /* a new response (the hop of a redirect, or the one after a 100): what was read of the last one is dropped */
        char *space = strchr(line, ' ');
        t->status = space ? atoi(space + 1) : 0;
        t->ctype[0] = t->clength[0] = t->crange[0] = t->location[0] = '\0';
        t->accepts_ranges = 0;
        return size * count;
    }
    colon = strchr(line, ':');
    if (!colon) return size * count;
    name_length = (size_t)(colon - line);
    value = colon + 1;
    while (*value == ' ' || *value == '\t') ++value;
    if (name_length == 12 && has_prefix_nocase(line, "content-type") && strlen(value) < sizeof t->ctype) snprintf(t->ctype, sizeof t->ctype, "%s", value);
    else if (name_length == 14 && has_prefix_nocase(line, "content-length") && strlen(value) < sizeof t->clength && digits_only(value)) snprintf(t->clength, sizeof t->clength, "%s", value);
    else if (name_length == 13 && has_prefix_nocase(line, "content-range") && strlen(value) < sizeof t->crange) snprintf(t->crange, sizeof t->crange, "%s", value);
    else if (name_length == 13 && has_prefix_nocase(line, "accept-ranges") && has_prefix_nocase(value, "bytes")) t->accepts_ranges = 1;
    else if (name_length == 8 && has_prefix_nocase(line, "location") && strlen(value) < sizeof t->location) snprintf(t->location, sizeof t->location, "%s", value);
    return size * count;
}

static int playlist_type(const char *ctype) {
    return has_prefix_nocase(ctype, "application/vnd.apple.mpegurl") || has_prefix_nocase(ctype, "application/x-mpegurl") ||
           has_prefix_nocase(ctype, "audio/mpegurl") || has_prefix_nocase(ctype, "audio/x-mpegurl");
}

static size_t write_callback(char *data, size_t size, size_t count, void *user) {
    transfer *t = user;
    size_t length = size * count;
    if (t->mode == MODE_UNDECIDED) {
        if ((is_redirect(t->status) && t->location[0]) || !passes_through(t->status)) {
            t->mode = MODE_DISCARD;
        } else if ((t->status == 200 || t->status == 206) && (playlist_type(t->ctype) || yozora_playlist_is((const unsigned char *)data, length))) {
            t->mode = MODE_BUFFER;
        } else {
            t->mode = MODE_STREAM;
            if (!send_head(t, t->status, t->ctype, t->clength, t->crange, t->status == 206 || t->accepts_ranges)) return 0;
        }
    }
    if (t->mode == MODE_DISCARD) return length;
    if (t->mode == MODE_BUFFER) {
        char *bigger;
        if (t->buffered + length > YOZORA_PROXY_PLAYLIST_MAX) {
            t->too_large = 1;
            return 0;
        }
        bigger = realloc(t->buffer, t->buffered + length + 1);
        if (!bigger) {
            t->too_large = 1;
            return 0;
        }
        t->buffer = bigger;
        memcpy(t->buffer + t->buffered, data, length);
        t->buffered += length;
        return length;
    }
    if (!send_all(t->fd, data, length)) {
        t->send_failed = 1;
        return 0;
    }
    return length;
}

/* curl asks every second or so whether to go on: a stopped proxy or a released stream ends the transfer */
static int progress_callback(void *user, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    transfer *t = user;
    int stop;
    (void)a; (void)b; (void)c; (void)d;
    pthread_mutex_lock(&t->proxy->lock);
    stop = t->proxy->stopping;
    pthread_mutex_unlock(&t->proxy->lock);
    return stop || !stream_is_live(t->proxy, t->stream_id) ? 1 : 0;
}

typedef struct {
    yozora_proxy *proxy;
    unsigned stream_id;
} map_context;

/* the proxy address of an address found in a playlist */
static int map_child(void *context, const char *absolute, char *out, size_t capacity) {
    map_context *m = context;
    int i, result = 1;
    pthread_mutex_lock(&m->proxy->lock);
    for (i = 0; i < YOZORA_PROXY_MAX_STREAMS; ++i) {
        if (m->proxy->streams[i].live && m->proxy->streams[i].id == m->stream_id) {
            result = add_entry(m->proxy, &m->proxy->streams[i], absolute, out, capacity);
            break;
        }
    }
    pthread_mutex_unlock(&m->proxy->lock);
    return result;
}

static int answer_playlist(transfer *t, const char *effective_url) {
    char *rewritten = malloc(REWRITTEN_MAX);
    char length[24];
    size_t written = 0;
    map_context context;
    yozora_playlist_status status;
    int ok = 0;
    if (!rewritten) return 0;
    t->buffer[t->buffered] = '\0';
    context.proxy = t->proxy;
    context.stream_id = t->stream_id;
    status = yozora_playlist_rewrite(t->buffer, t->buffered, effective_url, map_child, &context, rewritten, REWRITTEN_MAX, &written);
    if (status == YOZORA_PLAYLIST_OK) {
        snprintf(length, sizeof length, "%zu", written);
        /* a playlist that came as a range is answered whole, its length changed */
        ok = send_head(t, 200, t->ctype[0] && playlist_type(t->ctype) ? t->ctype : "application/vnd.apple.mpegurl", length, NULL, 0) &&
             (t->head_only || send_all(t->fd, rewritten, written));
        if (!ok) t->send_failed = 1;
    }
    free(rewritten);
    return ok;
}

/* one fetch of one address, a redirect is the caller's business. returns the curl code */
static CURLcode fetch_once(transfer *t, CURL *easy, const yozora_proxy_request *request, const stream_copy *copy, const char *url, const char *user_agent, const char *ca) {
    char host[256];
    int https = 0;
    t->status = 0;
    t->ctype[0] = t->clength[0] = t->crange[0] = t->location[0] = '\0';
    t->accepts_ranges = 0;
    t->mode = MODE_UNDECIDED;
    t->buffered = 0;
    t->too_large = 0;
    free(t->buffer);
    t->buffer = NULL;
    curl_easy_reset(easy);
    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(easy, CURLOPT_CAINFO, ca);
    /* the checks that make https worth using: set out loud so that no default of a build can lower them */
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, user_agent);
    if (copy->referer[0]) curl_easy_setopt(easy, CURLOPT_REFERER, copy->referer);
    /* the cookie goes to the host it was made for, over the scheme it was made for, and not along a redirect to another host */
    if (copy->cookie[0] && host_of(url, host, sizeof host, &https) && !strcmp(host, copy->host) && https == copy->https)
        curl_easy_setopt(easy, CURLOPT_COOKIE, copy->cookie);
    if (request->range[0]) curl_easy_setopt(easy, CURLOPT_RANGE, request->range);
    if (request->head_only) curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, t);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, t);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, t);
    return curl_easy_perform(easy);
}

/* local streams */

typedef struct {
    yozora_proxy_local_read read;
    yozora_proxy_local_ref release;
    void *context;
} local_ref;

/* the stream of the token when it is a local one, with a reference taken for the time of the answer */
static int take_local(yozora_proxy *proxy, const char *token, local_ref *out) {
    stream *s;
    int found = 0;
    pthread_mutex_lock(&proxy->lock);
    s = find_by_token(proxy, token);
    if (s && s->local_read) {
        s->local_retain(s->local_context);
        out->read = s->local_read;
        out->release = s->local_release;
        out->context = s->local_context;
        found = 1;
    }
    pthread_mutex_unlock(&proxy->lock);
    return found;
}

/* "a-b", "a-" or "-n" of a bytes range, against a file of total bytes: the first and the last byte, 0 when it cannot be satisfied */
static int local_range(const char *text, uint64_t total, uint64_t *first, uint64_t *last) {
    uint64_t a = 0, b = 0;
    int has_a = 0, has_b = 0;
    const char *p = text;
    while (isdigit((unsigned char)*p)) { a = a * 10 + (uint64_t)(*p - '0'); has_a = 1; ++p; }
    if (*p++ != '-') return 0;
    while (isdigit((unsigned char)*p)) { b = b * 10 + (uint64_t)(*p - '0'); has_b = 1; ++p; }
    if (*p || !total) return 0;
    if (!has_a) {
        if (!has_b || !b) return 0;
        *first = b >= total ? 0 : total - b;
        *last = total - 1;
        return 1;
    }
    if (a >= total || (has_b && b < a)) return 0;
    *first = a;
    *last = has_b && b < total - 1 ? b : total - 1;
    return 1;
}

/* answers a request for a file of a local stream. a playlist is read once into memory so one playlist is
   sent, not pieces of two while it grows. a segment never changes and is read in chunks */
static void serve_local(int fd, const yozora_proxy_request *request, const local_ref *ref) {
    transfer t;
    uint8_t chunk[SEND_CHUNK];
    uint8_t *snapshot = NULL;
    size_t got = 0, name_length = strlen(request->name);
    uint64_t total = 0, first = 0, last = 0, offset;
    const char *content_type = NULL;
    char length[24], range[96];
    int status, partial = 0;
    memset(&t, 0, sizeof t);
    t.fd = fd;
    t.head_only = request->head_only;
    status = ref->read(ref->context, request->name, 0, NULL, 0, &got, &total, &content_type);
    if (status != 0 || !content_type) {
        send_text_answer(fd, 404);
        return;
    }
    if (name_length > 5 && !strcmp(request->name + name_length - 5, ".m3u8")) {
        if (total > YOZORA_PROXY_PLAYLIST_MAX) {
            send_text_answer(fd, 502);
            return;
        }
        snapshot = malloc(total ? (size_t)total : 1);
        if (!snapshot) {
            send_text_answer(fd, 503);
            return;
        }
        status = ref->read(ref->context, request->name, 0, snapshot, (size_t)total, &got, &total, &content_type);
        if (status != 0 || got != total) {
            free(snapshot);
            send_text_answer(fd, 502);
            return;
        }
    }
    if (request->range[0]) {
        if (!local_range(request->range, total, &first, &last)) {
            free(snapshot);
            send_text_answer(fd, 416);
            return;
        }
        partial = 1;
    } else if (total) {
        last = total - 1;
    }
    snprintf(length, sizeof length, "%llu", (unsigned long long)(total ? last - first + 1 : 0));
    snprintf(range, sizeof range, "bytes %llu-%llu/%llu", (unsigned long long)first, (unsigned long long)last, (unsigned long long)total);
    if (!send_head(&t, partial ? 206 : 200, content_type, length, partial ? range : "", 1) || request->head_only || !total) {
        free(snapshot);
        return;
    }
    for (offset = first; offset <= last;) {
        size_t want = last - offset + 1 < sizeof chunk ? (size_t)(last - offset + 1) : sizeof chunk;
        if (snapshot) {
            memcpy(chunk, snapshot + offset, want);
            got = want;
        } else {
            status = ref->read(ref->context, request->name, offset, chunk, want, &got, &total, &content_type);
            /* a segment that was let go while it was sent ends the connection: the player asks again and is told 404 */
            if (status != 0 || got != want) break;
        }
        if (!send_all(fd, (const char *)chunk, got)) break;
        offset += got;
    }
    free(snapshot);
}

static void serve(yozora_proxy *proxy, int fd) {
    char head[YOZORA_PROXY_HEAD_MAX + 1];
    size_t length = 0;
    yozora_proxy_request request;
    yozora_proxy_request_status parsed;
    stream_copy *copy = NULL;
    transfer t;
    CURL *easy = NULL;
    char current[YOZORA_URL_MAX], next[YOZORA_URL_MAX];
    int hop;
    struct timeval timeout = { 10, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
#ifdef SO_NOSIGPIPE
    {
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    }
#endif
    /* the head ends at the first empty line, and is never longer than the bound */
    for (;;) {
        ssize_t n;
        if (length >= YOZORA_PROXY_HEAD_MAX) {
            send_text_answer(fd, 431);
            return;
        }
        n = recv(fd, head + length, YOZORA_PROXY_HEAD_MAX - length, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        length += (size_t)n;
        head[length] = '\0';
        if (strstr(head, "\r\n\r\n") || strstr(head, "\n\n")) break;
    }
    parsed = yozora_proxy_parse_request(head, length, &request);
    if (parsed != YOZORA_PROXY_REQUEST_OK) {
        send_text_answer(fd, parsed == YOZORA_PROXY_REQUEST_METHOD ? 405 : parsed == YOZORA_PROXY_REQUEST_NOT_FOUND ? 404 :
                                 parsed == YOZORA_PROXY_REQUEST_RANGE ? 416 : 400);
        return;
    }
    {
        local_ref ref;
        if (take_local(proxy, request.token, &ref)) {
            serve_local(fd, &request, &ref);
            ref.release(ref.context);
            return;
        }
    }
    copy = malloc(sizeof *copy);
    if (!copy) {
        send_text_answer(fd, 503);
        return;
    }
    memset(&t, 0, sizeof t);
    t.proxy = proxy;
    t.fd = fd;
    t.head_only = request.head_only;
    if (!copy_entry(proxy, &request, copy, &t.stream_id)) {
        send_text_answer(fd, 404);
        goto done;
    }
    easy = curl_easy_init();
    if (!easy) {
        send_text_answer(fd, 503);
        goto done;
    }
    snprintf(current, sizeof current, "%s", copy->url);
    for (hop = 0; hop <= MAX_REDIRECTS; ++hop) {
        CURLcode code = fetch_once(&t, easy, &request, copy, current, proxy->user_agent, proxy->ca_bundle);
        if (t.send_failed) goto done;
        if (code != CURLE_OK) {
            if (!t.head_sent) send_text_answer(fd, 502);
            goto done;
        }
        if (is_redirect(t.status) && t.location[0]) {
            if (hop == MAX_REDIRECTS || yozora_url_resolve(current, t.location, next, sizeof next) != YOZORA_PLAYLIST_OK) {
                send_text_answer(fd, 502);
                goto done;
            }
            memcpy(current, next, strlen(next) + 1);
            continue;
        }
        break;
    }
    if (t.head_sent) goto done;
    if (t.mode == MODE_BUFFER && t.buffer) {
        if (!answer_playlist(&t, current)) {
            if (!t.head_sent) send_text_answer(fd, 502);
        }
    } else if (passes_through(t.status)) {
        /* an answer with no body: a HEAD, an empty file, or a refusal that carried nothing */
        char zero[] = "0";
        /* the length of a playlist is not the length it is answered with, so a HEAD for one says none */
        const char *size = request.head_only ? (playlist_type(t.ctype) ? "" : t.clength) : zero;
        send_head(&t, t.status, t.ctype, size, t.crange, t.status == 206 || t.accepts_ranges);
    } else {
        send_text_answer(fd, 502);
    }
done:
    free(t.buffer);
    if (easy) curl_easy_cleanup(easy);
    free(copy);
}

typedef struct {
    yozora_proxy *proxy;
    int fd;
    int slot;
} connection;

static void *connection_main(void *argument) {
    connection *c = argument;
    yozora_proxy *proxy = c->proxy;
    serve(proxy, c->fd);
    shutdown(c->fd, SHUT_RDWR);
    close(c->fd);
    pthread_mutex_lock(&proxy->lock);
    proxy->client_fds[c->slot] = -1;
    if (--proxy->active == 0) pthread_cond_broadcast(&proxy->idle);
    pthread_mutex_unlock(&proxy->lock);
    free(c);
    return NULL;
}

static void *accept_main(void *argument) {
    yozora_proxy *proxy = argument;
    for (;;) {
        struct pollfd wait = { proxy->listen_fd, POLLIN, 0 };
        int fd, slot = -1, i, stopping;
        pthread_attr_t attributes;
        pthread_t thread;
        connection *c;
        int ready = poll(&wait, 1, 200);
        pthread_mutex_lock(&proxy->lock);
        stopping = proxy->stopping;
        pthread_mutex_unlock(&proxy->lock);
        if (stopping) return NULL;
        if (ready < 0 && errno != EINTR) ready = 0;
        if (ready > 0 && (wait.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            pthread_mutex_lock(&proxy->lock);
            proxy->dead = 1;
            pthread_mutex_unlock(&proxy->lock);
            return NULL;
        }
        if (ready <= 0 || !(wait.revents & POLLIN)) continue;
        fd = accept(proxy->listen_fd, NULL, NULL);
        if (fd < 0) {
            /* a connection that went away before it was taken is nothing, a socket that is not a socket any more is the end */
            if (errno == EBADF || errno == ENOTSOCK || errno == EINVAL) {
                pthread_mutex_lock(&proxy->lock);
                proxy->dead = 1;
                pthread_mutex_unlock(&proxy->lock);
                return NULL;
            }
            continue;
        }
        pthread_mutex_lock(&proxy->lock);
        for (i = 0; i < YOZORA_PROXY_MAX_CONNECTIONS; ++i) {
            if (proxy->client_fds[i] < 0) {
                slot = i;
                break;
            }
        }
        if (slot >= 0) {
            proxy->client_fds[slot] = fd;
            ++proxy->active;
        }
        pthread_mutex_unlock(&proxy->lock);
        if (slot < 0) {
            send_text_answer(fd, 503);
            close(fd);
            continue;
        }
        c = malloc(sizeof *c);
        pthread_attr_init(&attributes);
        pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
        if (c) {
            c->proxy = proxy;
            c->fd = fd;
            c->slot = slot;
        }
        if (!c || pthread_create(&thread, &attributes, connection_main, c) != 0) {
            free(c);
            pthread_mutex_lock(&proxy->lock);
            proxy->client_fds[slot] = -1;
            if (--proxy->active == 0) pthread_cond_broadcast(&proxy->idle);
            pthread_mutex_unlock(&proxy->lock);
            send_text_answer(fd, 503);
            close(fd);
        }
        pthread_attr_destroy(&attributes);
    }
}

/* lifecycle */

yozora_proxy_status yozora_proxy_start(const yozora_proxy_config *config, yozora_proxy **out) {
    yozora_proxy *proxy;
    struct sockaddr_in address;
    socklen_t size = sizeof address;
    int i, one = 1;
    if (out) *out = NULL;
    if (!config || !out || !config->ca_bundle || !config->user_agent) return YOZORA_PROXY_BAD_ARGUMENT;
    if (strlen(config->ca_bundle) >= sizeof proxy->ca_bundle || strlen(config->user_agent) >= sizeof proxy->user_agent || !control_free(config->user_agent))
        return YOZORA_PROXY_BAD_ARGUMENT;
    if (access(config->ca_bundle, R_OK) != 0) return YOZORA_PROXY_BAD_ARGUMENT;
    proxy = calloc(1, sizeof *proxy);
    if (!proxy) return YOZORA_PROXY_SYSTEM;
    snprintf(proxy->ca_bundle, sizeof proxy->ca_bundle, "%s", config->ca_bundle);
    snprintf(proxy->user_agent, sizeof proxy->user_agent, "%s", config->user_agent);
    for (i = 0; i < YOZORA_PROXY_MAX_CONNECTIONS; ++i) proxy->client_fds[i] = -1;
    pthread_mutex_init(&proxy->lock, NULL);
    pthread_cond_init(&proxy->idle, NULL);
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        free(proxy);
        return YOZORA_PROXY_SYSTEM;
    }
    proxy->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy->listen_fd < 0) goto failed;
    setsockopt(proxy->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(proxy->listen_fd, (struct sockaddr *)&address, sizeof address) != 0 || listen(proxy->listen_fd, 16) != 0 ||
        getsockname(proxy->listen_fd, (struct sockaddr *)&address, &size) != 0)
        goto failed;
    proxy->port = ntohs(address.sin_port);
    if (pthread_create(&proxy->acceptor, NULL, accept_main, proxy) != 0) goto failed;
    proxy->acceptor_started = 1;
    *out = proxy;
    return YOZORA_PROXY_OK;
failed:
    if (proxy->listen_fd >= 0) close(proxy->listen_fd);
    curl_global_cleanup();
    pthread_mutex_destroy(&proxy->lock);
    pthread_cond_destroy(&proxy->idle);
    free(proxy);
    return YOZORA_PROXY_SYSTEM;
}

int yozora_proxy_port(const yozora_proxy *proxy) { return proxy ? proxy->port : 0; }

int yozora_proxy_alive(yozora_proxy *proxy) {
    int alive;
    if (!proxy) return 0;
    pthread_mutex_lock(&proxy->lock);
    alive = !proxy->dead && !proxy->stopping;
    pthread_mutex_unlock(&proxy->lock);
    return alive;
}

void yozora_proxy_stop(yozora_proxy *proxy) {
    int i;
    if (!proxy) return;
    pthread_mutex_lock(&proxy->lock);
    proxy->stopping = 1;
    /* a thread blocked on its client or on the stream wakes up and finds the proxy stopped */
    for (i = 0; i < YOZORA_PROXY_MAX_CONNECTIONS; ++i)
        if (proxy->client_fds[i] >= 0) shutdown(proxy->client_fds[i], SHUT_RDWR);
    pthread_mutex_unlock(&proxy->lock);
    if (proxy->acceptor_started) pthread_join(proxy->acceptor, NULL);
    close(proxy->listen_fd);
    pthread_mutex_lock(&proxy->lock);
    while (proxy->active) pthread_cond_wait(&proxy->idle, &proxy->lock);
    for (i = 0; i < YOZORA_PROXY_MAX_STREAMS; ++i)
        if (proxy->streams[i].live) free_stream(&proxy->streams[i]);
    pthread_mutex_unlock(&proxy->lock);
    curl_global_cleanup();
    pthread_mutex_destroy(&proxy->lock);
    pthread_cond_destroy(&proxy->idle);
    free(proxy);
}
