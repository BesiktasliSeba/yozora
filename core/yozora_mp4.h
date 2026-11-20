#ifndef YOZORA_MP4_H
#define YOZORA_MP4_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a parser for the fragmented mp4 a page appends to a MediaSource (init segment, then moof and mdat pairs).
   bytes arrive in any size, a box is parsed when whole. samples go to a callback in decode order and are
   valid during the call only. reads avc1 and avc3 video, mp4a audio (esds, AAC), mvex and tfhd defaults, trun
   with moof offsets (default-base-is-moof or none, as MSE players write). refuses other codecs, an explicit
   base-data-offset and a box of unknown length */

typedef struct yozora_mp4 yozora_mp4;

typedef enum {
    YOZORA_MP4_OK = 0,
    /* a box that is not what its type says, a size that does not add up, a sample outside its mdat */
    YOZORA_MP4_MALFORMED,
    /* a codec, a layout or a box length the parser does not read */
    YOZORA_MP4_UNSUPPORTED,
    /* a box, or the samples of one moof, past the hard limits */
    YOZORA_MP4_TOO_LARGE,
    /* a callback returned nonzero */
    YOZORA_MP4_ABORTED,
    YOZORA_MP4_NO_MEMORY,
    /* a sample before the initialization segment */
    YOZORA_MP4_NO_INIT
} yozora_mp4_status;

#define YOZORA_MP4_MAX_TRACKS 8
#define YOZORA_MP4_MAX_CONFIG 512
/* the largest box held in memory, a media segment of the highest quality of a short is far under it */
#define YOZORA_MP4_MAX_BOX (24u * 1024u * 1024u)
#define YOZORA_MP4_MAX_RUN_SAMPLES 65536u

#define YOZORA_MP4_FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define YOZORA_MP4_CODEC_AVC1 YOZORA_MP4_FOURCC('a', 'v', 'c', '1')
#define YOZORA_MP4_CODEC_AVC3 YOZORA_MP4_FOURCC('a', 'v', 'c', '3')
#define YOZORA_MP4_CODEC_MP4A YOZORA_MP4_FOURCC('m', 'p', '4', 'a')

typedef enum {
    YOZORA_MP4_TRACK_VIDEO = 1,
    YOZORA_MP4_TRACK_AUDIO = 2
} yozora_mp4_track_kind;

typedef struct {
    uint32_t id;
    uint32_t timescale;
    int kind;
    uint32_t codec;
    uint32_t width, height;
    uint32_t channels, sample_rate;
    /* the avcC box of a video track, the AudioSpecificConfig of an audio track */
    uint8_t config[YOZORA_MP4_MAX_CONFIG];
    uint32_t config_size;
} yozora_mp4_track;

typedef struct {
    int64_t dts, pts;
    uint32_t duration;
    int sync;
    const uint8_t *data;
    uint32_t size;
} yozora_mp4_sample;

typedef struct {
    /* the tracks of an initialization segment: called again when one is appended again, a return other than 0 stops the parse */
    int (*on_init)(void *context, const yozora_mp4_track *tracks, unsigned count);
    int (*on_sample)(void *context, const yozora_mp4_track *track, const yozora_mp4_sample *sample);
    void *context;
} yozora_mp4_sink;

yozora_mp4_status yozora_mp4_create(const yozora_mp4_sink *sink, yozora_mp4 **out);
void yozora_mp4_destroy(yozora_mp4 *parser);
/* takes the next bytes. after an error the parser holds nothing and expects a box boundary, so the caller
   resets it with yozora_mp4_reset */
yozora_mp4_status yozora_mp4_feed(yozora_mp4 *parser, const uint8_t *data, size_t size);
/* drops an incomplete box and a moof waiting for its mdat, the tracks of the initialization segment stay (the
   reset of a SourceBuffer parser state) */
void yozora_mp4_reset(yozora_mp4 *parser);
/* the bytes held for a box that is not complete */
size_t yozora_mp4_pending(const yozora_mp4 *parser);

/* the codec string a track announces in a MIME type ("avc1.64001f", "mp4a.40.2"), for the interface to check
   what the page appended */
int yozora_mp4_track_codec_string(const yozora_mp4_track *track, char *out, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
