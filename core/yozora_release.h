#ifndef YOZORA_RELEASE_H
#define YOZORA_RELEASE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the latest release for the update screen. api.github.com limits an address to 60 requests an hour, releases.atom does not and holds the notes too */

#define YOZORA_RELEASE_TAG_MAX 64
#define YOZORA_RELEASE_NOTES_MAX 8192
#define YOZORA_RELEASE_URL_MAX 384
/* an asset address that does not start with it is never taken */
#define YOZORA_RELEASE_DOWNLOAD_PREFIX "https://github.com/sqmrak/yozora/releases/download/"

typedef enum {
    YOZORA_RELEASE_OK = 0,
    YOZORA_RELEASE_BAD_ARGUMENT,
    /* no tag, or one with characters outside letters, digits and ._-+ */
    YOZORA_RELEASE_MALFORMED
} yozora_release_status;

typedef struct {
    char tag[YOZORA_RELEASE_TAG_MAX];
    /* plain text, cut at a whole line */
    char notes[YOZORA_RELEASE_NOTES_MAX];
    /* Yozora-<tag>.ipa and its .sha256, empty when the release has none */
    char ipa_url[YOZORA_RELEASE_URL_MAX];
    char sha_url[YOZORA_RELEASE_URL_MAX];
    uint64_t ipa_size;
} yozora_release;

/* the answer of releases/latest, a draft or a prerelease is refused */
yozora_release_status yozora_release_parse_json(const char *json, size_t length, yozora_release *out);

/* the first entry of releases.atom, the assets are named from the tag and the size is 0 */
yozora_release_status yozora_release_parse_atom(const char *feed, size_t length, yozora_release *out);

/* markdown or html to plain text */
void yozora_release_notes_plain(const char *text, size_t length, int html, char *out, size_t capacity);

/* compares the first four numbers of two versions, the channel name after them is ignored. 0 when either has no number */
int yozora_version_compare(const char *a, const char *b, int *result);

/* 64 hex digits alone or followed by a file name */
int yozora_release_parse_sha256(const char *text, size_t length, uint8_t out[32]);

#ifdef __cplusplus
}
#endif
#endif
