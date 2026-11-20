#ifndef YOZORA_TS_H
#define YOZORA_TS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* an MPEG transport stream writer for what AVPlayer plays on ios 5 and 6 from HLS: H.264 in Annex B and AAC
   in ADTS. samples from a page's fragmented mp4 (length prefixed NAL units, raw AAC frames) become 188 byte
   packets sent to a sink one by one, so nothing here allocates. times are 90 kHz units, the caller adds an
   offset so none is negative */

typedef enum {
    YOZORA_TS_OK = 0,
    YOZORA_TS_BAD_ARGUMENT,
    /* an avcC box or an AudioSpecificConfig that is not read */
    YOZORA_TS_BAD_CONFIG,
    /* a sample whose NAL units do not add up to its size */
    YOZORA_TS_BAD_SAMPLE,
    YOZORA_TS_NO_MEMORY,
    /* the sink returned nonzero */
    YOZORA_TS_ABORTED
} yozora_ts_status;

#define YOZORA_TS_PACKET 188
#define YOZORA_TS_PID_PMT 0x1000
#define YOZORA_TS_PID_VIDEO 0x100
#define YOZORA_TS_PID_AUDIO 0x101
#define YOZORA_TS_MAX_PARAMETER_SETS 1024
/* a frame of AAC is under 8 KiB with the header, a video sample is bounded by the box limit of the mp4 parser */
#define YOZORA_TS_MAX_VIDEO_SAMPLE (24u * 1024u * 1024u)

typedef int (*yozora_ts_sink)(void *context, const uint8_t packet[YOZORA_TS_PACKET]);

typedef struct {
    yozora_ts_sink sink;
    void *context;
    int has_video, has_audio;
    unsigned nal_length_size;
    /* the SPS and PPS of the avcC box in Annex B, written before each sync sample */
    uint8_t parameter_sets[YOZORA_TS_MAX_PARAMETER_SETS];
    size_t parameter_sets_size;
    unsigned aac_profile, aac_frequency_index, aac_channels;
    uint8_t cc_pat, cc_pmt, cc_video, cc_audio;
} yozora_ts_mux;

/* avcc is the avcC box body of a video track and asc the AudioSpecificConfig of an audio track (NULL, 0 for
   none), at least one is required. AAC-LC only, HE-AAC is refused because ADTS cannot carry it the way the
   system player reads it */
yozora_ts_status yozora_ts_init(yozora_ts_mux *mux, const uint8_t *avcc, size_t avcc_size, const uint8_t *asc, size_t asc_size, yozora_ts_sink sink, void *context);

/* the PAT and the PMT, at the start of every segment, with the counters continuing */
yozora_ts_status yozora_ts_write_tables(yozora_ts_mux *mux);
/* a video sample: dts and pts in 90 kHz units, sync for a key frame (preceded by the parameter sets and
   carrying the random access flag) */
yozora_ts_status yozora_ts_write_video(yozora_ts_mux *mux, int64_t dts, int64_t pts, int sync, const uint8_t *sample, size_t size);
yozora_ts_status yozora_ts_write_audio(yozora_ts_mux *mux, int64_t pts, const uint8_t *frame, size_t size);

/* value * 90000 / timescale, rounded, without overflow for any 64-bit value a file can hold up to the limits of an hour of media */
int64_t yozora_ts_scale(int64_t value, uint32_t timescale);

#ifdef __cplusplus
}
#endif
#endif
