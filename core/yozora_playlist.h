#ifndef YOZORA_PLAYLIST_H
#define YOZORA_PLAYLIST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YOZORA_PLAYLIST_OK = 0,
    YOZORA_PLAYLIST_BAD_ARGUMENT,
    /* a line, an address or the result does not fit its bound */
    YOZORA_PLAYLIST_TOO_LARGE,
    /* the address cannot be resolved: empty, or the base is not an http or https address */
    YOZORA_PLAYLIST_BAD_URL,
    /* the address is not http or https, so it is not a thing the proxy fetches */
    YOZORA_PLAYLIST_REFUSED,
    /* the callback that maps an address failed */
    YOZORA_PLAYLIST_MAP_FAILED
} yozora_playlist_status;

#define YOZORA_PLAYLIST_LINE_MAX 8192
#define YOZORA_URL_MAX 4096

/* RFC 3986 resolution of ref against an http or https base, without the fragment, dot segments removed. a ref
   with another scheme gives REFUSED, an empty ref or a base that is not http or https gives BAD_URL. out is
   empty on any status but OK */
yozora_playlist_status yozora_url_resolve(const char *base, const char *ref, char *out, size_t capacity);

/* 1 when the bytes open an HLS playlist: #EXTM3U after an optional byte order mark and white space */
int yozora_playlist_is(const unsigned char *data, size_t length);

/* gives the proxy address of a resolved absolute address, 0 on success */
typedef int (*yozora_playlist_map)(void *context, const char *absolute_url, char *out, size_t capacity);

/* copies an HLS playlist with every address (non tag lines and URI="..." attributes) replaced by what map
   gives for its resolution against base, so no segment leaves the proxy. master and media playlists, keys,
   maps and renditions follow one rule. other schemes (data:, skd:) and everything else is copied byte for
   byte. out is NUL terminated, *written is its length */
yozora_playlist_status yozora_playlist_rewrite(const char *text, size_t length, const char *base, yozora_playlist_map map, void *context,
                                               char *out, size_t capacity, size_t *written);

#ifdef __cplusplus
}
#endif
#endif
