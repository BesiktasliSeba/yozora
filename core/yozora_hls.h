#ifndef YOZORA_HLS_H
#define YOZORA_HLS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a MediaSource as an HLS stream for the ios 5 and 6 system player, which plays nothing a page appends:
   samples of one playback epoch are cut into MPEG-TS segments at key frames and served from memory as an
   EVENT playlist that only grows. a seek that leaves what is held starts a new epoch and object. a segment is
   cut when the next key frame is known and the audio reaches past it, so each holds both tracks. reference
   counted, every call safe from any thread, times in 90 kHz units of page media time */

typedef struct yozora_hls yozora_hls;

typedef enum {
    YOZORA_HLS_OK = 0,
    YOZORA_HLS_BAD_ARGUMENT,
    /* an avcC box or an AudioSpecificConfig that the muxer refuses, or a track that was not asked for */
    YOZORA_HLS_BAD_CONFIG,
    /* a time that goes back in decode order, or a presentation time before its decode time */
    YOZORA_HLS_BAD_SAMPLE,
    YOZORA_HLS_NO_MEMORY,
    /* the samples waiting for a segment, or the segments, past the limits */
    YOZORA_HLS_TOO_LARGE,
    /* after yozora_hls_end */
    YOZORA_HLS_ENDED
} yozora_hls_status;

#define YOZORA_HLS_MAX_PENDING_BYTES (24u * 1024u * 1024u)
#define YOZORA_HLS_MAX_SEGMENTS 8192u
/* a segment is not cut before it holds this much, so a stream of key frames at every frame does not make a file of each */
#define YOZORA_HLS_MIN_SEGMENT 45000
/* a gop that never ends is cut at the next key frame after the limit, an error when there is none for twice
   as long */
#define YOZORA_HLS_MAX_SEGMENT (60 * 90000)
#define YOZORA_HLS_OFFSET 900000

/* want_video and want_audio say which tracks the epoch has: no segment is cut before the config of each of them was given */
yozora_hls_status yozora_hls_create(int want_video, int want_audio, yozora_hls **out);
void yozora_hls_retain(yozora_hls *hls);
void yozora_hls_release(yozora_hls *hls);

yozora_hls_status yozora_hls_set_video(yozora_hls *hls, const uint8_t *avcc, size_t size);
yozora_hls_status yozora_hls_set_audio(yozora_hls *hls, const uint8_t *asc, size_t size);
yozora_hls_status yozora_hls_add_video(yozora_hls *hls, int64_t dts, int64_t pts, int sync, const uint8_t *data, size_t size);
yozora_hls_status yozora_hls_add_audio(yozora_hls *hls, int64_t pts, const uint8_t *data, size_t size);
/* no more samples: what is left is cut into a last segment and the playlist gets its end */
yozora_hls_status yozora_hls_end(yozora_hls *hls);

/* the media time of the start of the playlist, 0 until the first key frame came */
int64_t yozora_hls_origin(yozora_hls *hls);
/* the length of the segments that are cut, in 90 kHz units */
int64_t yozora_hls_duration(yozora_hls *hls);
unsigned yozora_hls_segment_count(yozora_hls *hls);
int yozora_hls_ended(yozora_hls *hls);
/* frees segment bytes that end before the time (from the playlist start), the playlist still names them and a
   request for one is a 404. the player calls it as the position moves so a long video does not stay in memory */
void yozora_hls_trim(yozora_hls *hls, int64_t before);
/* the bytes that the segments and the samples waiting hold */
size_t yozora_hls_memory(yozora_hls *hls);

typedef enum {
    YOZORA_HLS_READ_OK = 0,
    YOZORA_HLS_READ_NOT_FOUND,
    /* an offset past the end */
    YOZORA_HLS_READ_RANGE
} yozora_hls_read_status;

#define YOZORA_HLS_NAME_PLAYLIST "index.m3u8"
/* reads a file of the stream, "index.m3u8" or "seg<N>.ts", from offset at most capacity bytes into out.
   *total is the length of the whole file, content_type is a static string */
yozora_hls_read_status yozora_hls_read(yozora_hls *hls, const char *name, uint64_t offset, uint8_t *out, size_t capacity, size_t *got, uint64_t *total,
                                       const char **content_type);

/* one fragmented mp4 media segment as an mpeg-ts segment, h264 and aac only, the caller frees *out */
typedef enum {
    YOZORA_FMP4_OK = 0,
    YOZORA_FMP4_BAD_ARGUMENT,
    YOZORA_FMP4_MALFORMED,
    YOZORA_FMP4_UNSUPPORTED,
    YOZORA_FMP4_TOO_LARGE,
    YOZORA_FMP4_NO_MEMORY
} yozora_fmp4_status;
#define YOZORA_FMP4_MAX_INPUT (24u * 1024u * 1024u)
yozora_fmp4_status yozora_fmp4_to_ts(const uint8_t *init, size_t init_size, const uint8_t *segment, size_t segment_size, uint8_t **out, size_t *out_size);

#ifdef __cplusplus
}
#endif
#endif
