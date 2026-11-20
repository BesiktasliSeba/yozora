/* the proxy's local streams (a MediaSource turned into an HLS stream, core/yozora_hls.h) over a real loopback
   socket: files come from an in-process provider, with ranges, HEAD, names that are not files, and a stream
   released while a request still holds it */
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "yozora_stream_proxy.h"

static int failures;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_proxy_local: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

typedef struct {
    pthread_mutex_t lock;
    int references;
    int released_to_zero;
    /* a file the provider reads slowly: the reader waits for this to be 1 before it answers, so a request can be held open */
    int hold;
} provider;

static unsigned char segment[100000];
static const char playlist[] = "#EXTM3U\n#EXT-X-VERSION:3\n#EXTINF:1.000,\nseg0.ts\n";

static int read_file(void *context, const char *name, uint64_t offset, uint8_t *out, size_t capacity, size_t *got, uint64_t *total, const char **type)
{
    provider *p = context;
    const unsigned char *data;
    size_t length;
    *got = 0;
    if (!strcmp(name, "index.m3u8")) {
        data = (const unsigned char *)playlist;
        length = strlen(playlist);
        *type = "application/vnd.apple.mpegurl";
    } else if (!strcmp(name, "seg0.ts")) {
        data = segment;
        length = sizeof segment;
        *type = "video/mp2t";
    } else
        return 1;
    *total = length;
    if (offset >= length && length)
        return 2;
    size_t take = length - (size_t)offset;
    if (take > capacity)
        take = capacity;
    if (take)
        memcpy(out, data + offset, take);
    *got = take;
    (void)p;
    return 0;
}

static void retain(void *context)
{
    provider *p = context;
    pthread_mutex_lock(&p->lock);
    ++p->references;
    pthread_mutex_unlock(&p->lock);
}

static void release(void *context)
{
    provider *p = context;
    pthread_mutex_lock(&p->lock);
    if (--p->references == 0)
        p->released_to_zero = 1;
    pthread_mutex_unlock(&p->lock);
}

/* one request over a socket, the whole answer (the proxy closes after it) */
static size_t request(int port, const char *text, char *answer, size_t capacity)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&address, sizeof address) != 0) {
        close(fd);
        return 0;
    }
    if (write(fd, text, strlen(text)) < 0) {
        close(fd);
        return 0;
    }
    size_t used = 0;
    for (;;) {
        ssize_t n = read(fd, answer + used, capacity - used - 1);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        used += (size_t)n;
        if (used + 1 >= capacity)
            break;
    }
    close(fd);
    answer[used] = '\0';
    return used;
}

static const char *body_of(const char *answer, size_t used, size_t *length)
{
    const char *end = strstr(answer, "\r\n\r\n");
    if (!end)
        return NULL;
    end += 4;
    *length = used - (size_t)(end - answer);
    return end;
}

static void get(int port, const char *base_path, const char *name, const char *method, const char *range, char *answer, size_t capacity, size_t *used)
{
    char text[512];
    if (range)
        snprintf(text, sizeof text, "%s %s%s HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=%s\r\n\r\n", method, base_path, name, range);
    else
        snprintf(text, sizeof text, "%s %s%s HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", method, base_path, name);
    *used = request(port, text, answer, capacity);
}

int main(void)
{
    for (size_t i = 0; i < sizeof segment; ++i)
        segment[i] = (unsigned char)(i * 13 + 7);
    yozora_proxy_config config = { "app/ca-bundle.crt", "test" };
    yozora_proxy *proxy = NULL;
    CHECK(yozora_proxy_start(&config, &proxy) == YOZORA_PROXY_OK);
    if (!proxy)
        return 1;
    int port = yozora_proxy_port(proxy);
    provider p;
    pthread_mutex_init(&p.lock, NULL);
    p.references = 1;
    p.released_to_zero = 0;
    p.hold = 0;

    char base[256];
    unsigned stream = 0;
    CHECK(yozora_proxy_register_local(proxy, read_file, retain, release, &p, base, sizeof base, &stream) == YOZORA_PROXY_OK && stream);
    CHECK(!strncmp(base, "http://127.0.0.1:", 17) && base[strlen(base) - 1] == '/');
    const char *path = strstr(base, "/s/");
    CHECK(path != NULL);
    /* bad arguments: nothing is registered, and the caller keeps its reference */
    char out[64];
    CHECK(yozora_proxy_register_local(proxy, NULL, retain, release, &p, out, sizeof out, NULL) == YOZORA_PROXY_BAD_ARGUMENT);
    CHECK(yozora_proxy_register_local(proxy, read_file, retain, release, &p, out, 10, NULL) == YOZORA_PROXY_SYSTEM && !out[0]);

    static char answer[200000];
    size_t used, length;

    /* the playlist */
    get(port, path, "index.m3u8", "GET", NULL, answer, sizeof answer, &used);
    CHECK(!strncmp(answer, "HTTP/1.1 200 OK", 15));
    CHECK(strstr(answer, "Content-Type: application/vnd.apple.mpegurl") && strstr(answer, "Accept-Ranges: bytes"));
    const char *body = body_of(answer, used, &length);
    CHECK(body && length == strlen(playlist) && !memcmp(body, playlist, length));
    {
        char expected[48];
        snprintf(expected, sizeof expected, "Content-Length: %zu\r\n", strlen(playlist));
        CHECK(strstr(answer, expected));
    }

    /* a segment whole, and in pieces */
    get(port, path, "seg0.ts", "GET", NULL, answer, sizeof answer, &used);
    CHECK(!strncmp(answer, "HTTP/1.1 200 OK", 15) && strstr(answer, "Content-Type: video/mp2t"));
    body = body_of(answer, used, &length);
    CHECK(body && length == sizeof segment && !memcmp(body, segment, sizeof segment));
    get(port, path, "seg0.ts", "GET", "1000-1999", answer, sizeof answer, &used);
    CHECK(!strncmp(answer, "HTTP/1.1 206 Partial Content", 28) && strstr(answer, "Content-Range: bytes 1000-1999/100000\r\n"));
    body = body_of(answer, used, &length);
    CHECK(body && length == 1000 && !memcmp(body, segment + 1000, 1000));
    get(port, path, "seg0.ts", "GET", "99990-", answer, sizeof answer, &used);
    body = body_of(answer, used, &length);
    CHECK(strstr(answer, "Content-Range: bytes 99990-99999/100000\r\n") && body && length == 10 && !memcmp(body, segment + 99990, 10));
    get(port, path, "seg0.ts", "GET", "-5", answer, sizeof answer, &used);
    body = body_of(answer, used, &length);
    CHECK(strstr(answer, "Content-Range: bytes 99995-99999/100000\r\n") && body && length == 5 && !memcmp(body, segment + 99995, 5));
    get(port, path, "seg0.ts", "GET", "0-9999999", answer, sizeof answer, &used);
    body = body_of(answer, used, &length);
    CHECK(strstr(answer, "Content-Range: bytes 0-99999/100000\r\n") && body && length == sizeof segment);
    /* a range past the end, an empty one, and a reversed one */
    get(port, path, "seg0.ts", "GET", "100000-", answer, sizeof answer, &used);
    CHECK(!strncmp(answer, "HTTP/1.1 416", 12));
    get(port, path, "seg0.ts", "GET", "-", answer, sizeof answer, &used);
    CHECK(!strncmp(answer, "HTTP/1.1 416", 12));
    get(port, path, "seg0.ts", "GET", "50-40", answer, sizeof answer, &used);
    CHECK(!strncmp(answer, "HTTP/1.1 416", 12));

    /* HEAD: the head with the length, no body */
    get(port, path, "seg0.ts", "HEAD", NULL, answer, sizeof answer, &used);
    body = body_of(answer, used, &length);
    CHECK(!strncmp(answer, "HTTP/1.1 200 OK", 15) && strstr(answer, "Content-Length: 100000\r\n") && body && length == 0);

    /* names that are not files of the stream, and names that are not names */
    get(port, path, "seg1.ts", "GET", NULL, answer, sizeof answer, &used);
    CHECK(!strncmp(answer, "HTTP/1.1 404", 12));
    get(port, path, "..", "GET", NULL, answer, sizeof answer, &used);
    CHECK(!strncmp(answer, "HTTP/1.1 404", 12));
    get(port, path, ".hidden", "GET", NULL, answer, sizeof answer, &used);
    CHECK(!strncmp(answer, "HTTP/1.1 404", 12));
    get(port, path, "seg0.ts?x=1", "GET", NULL, answer, sizeof answer, &used);
    CHECK(!strncmp(answer, "HTTP/1.1 404", 12));
    get(port, path, "a/b", "GET", NULL, answer, sizeof answer, &used);
    CHECK(!strncmp(answer, "HTTP/1.1 404", 12));
    /* a numeric name is the form of a network entry, which a local stream does not have */
    get(port, path, "0.ts", "GET", NULL, answer, sizeof answer, &used);
    CHECK(!strncmp(answer, "HTTP/1.1 404", 12));
    /* another token */
    {
        char other[256];
        snprintf(other, sizeof other, "/s/%032x/", 1);
        get(port, other, "index.m3u8", "GET", NULL, answer, sizeof answer, &used);
        CHECK(!strncmp(answer, "HTTP/1.1 404", 12));
    }
    /* only GET and HEAD */
    {
        char text[300];
        snprintf(text, sizeof text, "POST %sindex.m3u8 HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n", path);
        used = request(port, text, answer, sizeof answer);
        CHECK(!strncmp(answer, "HTTP/1.1 405", 12));
    }

    /* the references: every answer took one and gave it back, the stream still holds the one it was given */
    pthread_mutex_lock(&p.lock);
    CHECK(p.references == 1 && !p.released_to_zero);
    pthread_mutex_unlock(&p.lock);
    yozora_proxy_release(proxy, stream);
    pthread_mutex_lock(&p.lock);
    CHECK(p.references == 0 && p.released_to_zero);
    pthread_mutex_unlock(&p.lock);
    /* a released stream answers 404 */
    get(port, path, "index.m3u8", "GET", NULL, answer, sizeof answer, &used);
    CHECK(!strncmp(answer, "HTTP/1.1 404", 12));

    yozora_proxy_stop(proxy);
    pthread_mutex_destroy(&p.lock);
    if (failures) {
        fprintf(stderr, "test_proxy_local: %d failures\n", failures);
        return 1;
    }
    puts("test_proxy_local: ok");
    return 0;
}
