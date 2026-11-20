#ifndef YOZORA_STREAM_PROXY_H
#define YOZORA_STREAM_PROXY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a loopback proxy AVPlayer fetches streams through: the system player has an old trust store (too old on ios
   5 and 6 for current CDN certificates) and sends neither cookies nor referer. libcurl verifies the chain and
   host against the given root file, sends cookie and referer and passes ranges on. HLS playlists are
   rewritten so every address comes back. a stream gets a proxy address with a random token so another process
   cannot ask for what it cannot name, and the cookie goes only to the registered host over the same scheme */

typedef struct yozora_proxy yozora_proxy;

typedef enum {
    YOZORA_PROXY_OK = 0,
    YOZORA_PROXY_BAD_ARGUMENT,
    YOZORA_PROXY_BAD_URL,
    /* a socket, a thread or the random source failed, errno says which */
    YOZORA_PROXY_SYSTEM,
    /* too many streams are registered */
    YOZORA_PROXY_LIMIT
} yozora_proxy_status;

typedef struct {
    /* the file of root certificates, required: the proxy never falls back to another trust store and never turns the checks off */
    const char *ca_bundle;
    const char *user_agent;
} yozora_proxy_config;

#define YOZORA_PROXY_MAX_STREAMS 16
#define YOZORA_PROXY_MAX_CONNECTIONS 8
#define YOZORA_PROXY_MAX_ENTRIES 2048
#define YOZORA_PROXY_MAX_COOKIE 4096
#define YOZORA_PROXY_MAX_REFERER 512
#define YOZORA_PROXY_PLAYLIST_MAX (1024 * 1024)

/* starts listening on 127.0.0.1 at a port the system picks, with the thread that accepts */
yozora_proxy_status yozora_proxy_start(const yozora_proxy_config *config, yozora_proxy **out);
int yozora_proxy_port(const yozora_proxy *proxy);
/* 0 once the proxy cannot take connections (the system closed its socket), stop it and start another, the
   streams of the old one are gone */
int yozora_proxy_alive(yozora_proxy *proxy);
/* the proxy address of an http or https address (no user name or password), cookie and referer may be NULL.
   *stream is the id to release it with */
yozora_proxy_status yozora_proxy_register(yozora_proxy *proxy, const char *origin_url, const char *cookie, const char *referer,
                                          char *out, size_t capacity, unsigned *stream);
/* a stream whose files come from the process, not the network: a MediaSource stream (core/yozora_hls.h). read
   gives file `name` (letters, digits and . _ -, at most 63) from offset, at most capacity bytes into out,
   *total is the file length and content_type a static string. returns 0, 1 for a missing file, 2 for an
   offset past the end, capacity 0 asks the length only. retain and release count references to context, the
   proxy holds one per answer so a stream released meanwhile stays valid */
typedef int (*yozora_proxy_local_read)(void *context, const char *name, uint64_t offset, uint8_t *out, size_t capacity, size_t *got, uint64_t *total,
                                       const char **content_type);
typedef void (*yozora_proxy_local_ref)(void *context);
yozora_proxy_status yozora_proxy_register_local(yozora_proxy *proxy, yozora_proxy_local_read read, yozora_proxy_local_ref retain, yozora_proxy_local_ref release,
                                                void *context, char *out, size_t capacity, unsigned *stream);
/* forgets a stream: its addresses answer 404 from then on, and a transfer under way ends */
void yozora_proxy_release(yozora_proxy *proxy, unsigned stream);
/* stops accepting, ends the transfers and waits for their threads */
void yozora_proxy_stop(yozora_proxy *proxy);

typedef enum {
    YOZORA_PROXY_REQUEST_OK = 0,
    /* 400: not a request the proxy reads */
    YOZORA_PROXY_REQUEST_BAD,
    /* 405: neither GET nor HEAD */
    YOZORA_PROXY_REQUEST_METHOD,
    /* 404: not an address of the proxy */
    YOZORA_PROXY_REQUEST_NOT_FOUND,
    /* 416: a range the proxy does not pass on (more than one, or not bytes) */
    YOZORA_PROXY_REQUEST_RANGE
} yozora_proxy_request_status;

typedef struct {
    int head_only;
    /* the range as curl takes it ("0-499", "500-", "-500"), empty without one */
    char range[48];
    char token[33];
    unsigned index;
    /* the last part of the path as written, the file of a local stream. numeric is 1 when it has the form of
       a network stream address (digits and an extension), which is what index was read from */
    char name[64];
    int numeric;
} yozora_proxy_request;

#define YOZORA_PROXY_HEAD_MAX 8192

/* reads the head of a request: its first line and the Range header. the others are not looked at */
yozora_proxy_request_status yozora_proxy_parse_request(const char *head, size_t length, yozora_proxy_request *out);

#ifdef __cplusplus
}
#endif
#endif
