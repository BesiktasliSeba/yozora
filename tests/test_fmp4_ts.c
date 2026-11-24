#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yozora_hls.h"
#include "yozora_mp4.h"
#include "yozora_ts.h"

static int failures;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_fmp4_ts: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

typedef struct {
    uint8_t *data;
    size_t size, capacity;
} bytes;

static void put(bytes *b, const void *data, size_t size)
{
    if (b->size + size > b->capacity) {
        b->capacity = (b->size + size) * 2 + 64;
        b->data = realloc(b->data, b->capacity);
    }
    memcpy(b->data + b->size, data, size);
    b->size += size;
}
static void put8(bytes *b, unsigned v) { uint8_t x = (uint8_t)v; put(b, &x, 1); }
static void put16(bytes *b, unsigned v) { put8(b, v >> 8); put8(b, v); }
static void put32(bytes *b, uint32_t v) { put16(b, v >> 16); put16(b, v & 0xffff); }
static void put_zero(bytes *b, size_t n) { while (n--) put8(b, 0); }

/* writes a box: the size is patched when it is closed */
static size_t open_box(bytes *b, const char *type)
{
    size_t at = b->size;
    put32(b, 0);
    put(b, type, 4);
    return at;
}
static void close_box(bytes *b, size_t at)
{
    uint32_t size = (uint32_t)(b->size - at);
    b->data[at] = (uint8_t)(size >> 24);
    b->data[at + 1] = (uint8_t)(size >> 16);
    b->data[at + 2] = (uint8_t)(size >> 8);
    b->data[at + 3] = (uint8_t)size;
}

static const uint8_t sps[] = { 0x67, 0x64, 0x00, 0x1f, 0xac, 0xd9, 0x40, 0x50, 0x05, 0xbb };
static const uint8_t pps[] = { 0x68, 0xeb, 0xe3, 0xcb, 0x22, 0xc0 };
static const uint8_t asc[] = { 0x12, 0x10 }; /* AAC-LC, 44100 Hz, stereo */

static void build_avcc(bytes *b)
{
    put8(b, 1); put8(b, sps[1]); put8(b, sps[2]); put8(b, sps[3]);
    put8(b, 0xff);
    put8(b, 0xe1); put16(b, sizeof sps); put(b, sps, sizeof sps);
    put8(b, 1); put16(b, sizeof pps); put(b, pps, sizeof pps);
}

static void descriptor(bytes *b, unsigned tag, size_t length)
{
    put8(b, tag);
    /* a length of four bytes with the continuation bit, as encoders write it */
    put8(b, 0x80 | (length >> 21)); put8(b, 0x80 | ((length >> 14) & 0x7f)); put8(b, 0x80 | ((length >> 7) & 0x7f)); put8(b, length & 0x7f);
}

static void build_trak(bytes *b, unsigned id, int video)
{
    size_t trak = open_box(b, "trak");
    size_t tkhd = open_box(b, "tkhd");
    put32(b, 0); put32(b, 0); put32(b, 0); put32(b, id); put_zero(b, 68);
    close_box(b, tkhd);
    size_t mdia = open_box(b, "mdia");
    size_t mdhd = open_box(b, "mdhd");
    put32(b, 0); put32(b, 0); put32(b, 0); put32(b, video ? 15360 : 44100); put32(b, 0); put32(b, 0);
    close_box(b, mdhd);
    size_t hdlr = open_box(b, "hdlr");
    put32(b, 0); put32(b, 0); put(b, video ? "vide" : "soun", 4); put_zero(b, 12); put8(b, 0);
    close_box(b, hdlr);
    size_t minf = open_box(b, "minf");
    size_t stbl = open_box(b, "stbl");
    size_t stsd = open_box(b, "stsd");
    put32(b, 0); put32(b, 1);
    if (video) {
        size_t avc1 = open_box(b, "avc1");
        put_zero(b, 6); put16(b, 1); put_zero(b, 16); put16(b, 640); put16(b, 360); put_zero(b, 50);
        size_t avcc = open_box(b, "avcC");
        build_avcc(b);
        close_box(b, avcc);
        close_box(b, avc1);
    } else {
        size_t mp4a = open_box(b, "mp4a");
        put_zero(b, 6); put16(b, 1); put_zero(b, 8); put16(b, 2); put16(b, 16); put32(b, 0); put32(b, 44100u << 16);
        size_t esds = open_box(b, "esds");
        put32(b, 0);
        descriptor(b, 0x03, 3 + 5 + 13 + 5 + sizeof asc + 3);
        put16(b, 1); put8(b, 0);
        descriptor(b, 0x04, 13 + 5 + sizeof asc);
        put8(b, 0x40); put8(b, 0x15); put_zero(b, 11);
        descriptor(b, 0x05, sizeof asc);
        put(b, asc, sizeof asc);
        descriptor(b, 0x06, 1); put8(b, 2);
        close_box(b, esds);
        close_box(b, mp4a);
    }
    close_box(b, stsd);
    close_box(b, stbl);
    close_box(b, minf);
    close_box(b, mdia);
    close_box(b, trak);
}

static void build_init(bytes *b)
{
    size_t ftyp = open_box(b, "ftyp");
    put(b, "iso5", 4); put32(b, 0); put(b, "iso5", 4);
    close_box(b, ftyp);
    size_t moov = open_box(b, "moov");
    size_t mvhd = open_box(b, "mvhd");
    put_zero(b, 100);
    close_box(b, mvhd);
    build_trak(b, 1, 1);
    build_trak(b, 2, 0);
    size_t mvex = open_box(b, "mvex");
    for (unsigned id = 1; id <= 2; ++id) {
        size_t trex = open_box(b, "trex");
        put32(b, 0); put32(b, id); put32(b, 1); put32(b, id == 1 ? 512 : 1024); put32(b, 0); put32(b, id == 1 ? 0x01010000u : 0x02000000u);
        close_box(b, trex);
    }
    close_box(b, mvex);
    close_box(b, moov);
}

/* the video samples of the test: an IDR, a P frame and a B frame (decode order), each as one or two length prefixed NAL units */
static const uint8_t idr_nal[] = { 0x65, 0x88, 0x84, 0x00, 0x33, 0xff };
static const uint8_t p_nal[] = { 0x41, 0x9a, 0x24, 0x6c };
static const uint8_t b_nal[] = { 0x01, 0x9e, 0x11 };
static const uint8_t sei_nal[] = { 0x06, 0x05, 0x01, 0xff, 0x80 };

static void build_video_sample(bytes *b, int index)
{
    if (index == 0) { put32(b, sizeof idr_nal); put(b, idr_nal, sizeof idr_nal); }
    else if (index == 1) { put32(b, sizeof sei_nal); put(b, sei_nal, sizeof sei_nal); put32(b, sizeof p_nal); put(b, p_nal, sizeof p_nal); }
    else { put32(b, sizeof b_nal); put(b, b_nal, sizeof b_nal); }
}
static size_t video_sample_size(int index) { return index == 0 ? 4 + sizeof idr_nal : index == 1 ? 8 + sizeof sei_nal + sizeof p_nal : 4 + sizeof b_nal; }

static const uint8_t aac_frames[2][5] = { { 0x21, 0x10, 0x04, 0x60, 0x8c }, { 0x21, 0x00, 0x49, 0x90, 0x02 } };

static void build_segment(bytes *b, int64_t video_start, int64_t audio_start, int with_tfdt)
{
    bytes mdat = { 0 };
    for (int i = 0; i < 3; ++i) build_video_sample(&mdat, i);
    size_t video_bytes = mdat.size;
    for (int i = 0; i < 2; ++i) put(&mdat, aac_frames[i], sizeof aac_frames[i]);

    size_t moof = open_box(b, "moof");
    size_t mfhd = open_box(b, "mfhd");
    put32(b, 0); put32(b, 1);
    close_box(b, mfhd);
    size_t offsets[2];
    for (unsigned t = 0; t < 2; ++t) {
        size_t traf = open_box(b, "traf");
        size_t tfhd = open_box(b, "tfhd");
        put32(b, 0x020000); put32(b, t + 1);
        close_box(b, tfhd);
        if (with_tfdt) {
            size_t tfdt = open_box(b, "tfdt");
            put32(b, 0x01000000); put32(b, 0); put32(b, (uint32_t)(t == 0 ? video_start : audio_start));
            close_box(b, tfdt);
        }
        size_t trun = open_box(b, "trun");
        if (t == 0) {
            /* version 1 (signed offsets), data offset, first sample flags, per sample duration, size and offset */
            put32(b, 0x01000000 | 0x1 | 0x4 | 0x100 | 0x200 | 0x800);
            put32(b, 3);
            offsets[0] = b->size; put32(b, 0);
            put32(b, 0x02000000);
            int64_t cts[3] = { 1024, 2048, 0 }; /* IDR shown after a 1024 delay, P two frames late, B first */
            for (int i = 0; i < 3; ++i) { put32(b, 512 * 2); put32(b, (uint32_t)video_sample_size(i)); put32(b, (uint32_t)cts[i]); }
        } else {
            put32(b, 0x1 | 0x200);
            put32(b, 2);
            offsets[1] = b->size; put32(b, 0);
            for (int i = 0; i < 2; ++i) put32(b, (uint32_t)sizeof aac_frames[i]);
        }
        close_box(b, trun);
        close_box(b, traf);
    }
    close_box(b, moof);
    size_t moof_size = b->size - moof;
    uint32_t video_offset = (uint32_t)(moof_size + 8);
    uint32_t audio_offset = (uint32_t)(moof_size + 8 + video_bytes);
    b->data[offsets[0]] = (uint8_t)(video_offset >> 24); b->data[offsets[0] + 1] = (uint8_t)(video_offset >> 16); b->data[offsets[0] + 2] = (uint8_t)(video_offset >> 8); b->data[offsets[0] + 3] = (uint8_t)video_offset;
    b->data[offsets[1]] = (uint8_t)(audio_offset >> 24); b->data[offsets[1] + 1] = (uint8_t)(audio_offset >> 16); b->data[offsets[1] + 2] = (uint8_t)(audio_offset >> 8); b->data[offsets[1] + 3] = (uint8_t)audio_offset;
    size_t box = open_box(b, "mdat");
    put(b, mdat.data, mdat.size);
    close_box(b, box);
    free(mdat.data);
}


/* counts the packets by pid */
static void count_packets(const uint8_t *data, size_t size, int *pat, int *pmt, int *video, int *audio, int *video_starts, int *keyframes)
{
    *pat = *pmt = *video = *audio = *video_starts = *keyframes = 0;
    for (size_t at = 0; at + YOZORA_TS_PACKET <= size; at += YOZORA_TS_PACKET) {
        const uint8_t *p = data + at;
        CHECK(p[0] == 0x47);
        unsigned pid = ((unsigned)(p[1] & 0x1f) << 8) | p[2];
        if (pid == 0) ++*pat;
        else if (pid == YOZORA_TS_PID_PMT) ++*pmt;
        else if (pid == YOZORA_TS_PID_VIDEO) {
            ++*video;
            if (p[1] & 0x40) ++*video_starts;
            if ((p[3] & 0x20) && p[4] > 0 && (p[5] & 0x40)) ++*keyframes;
        } else if (pid == YOZORA_TS_PID_AUDIO) ++*audio;
    }
}

/* with a directory as argument the fixtures of tests/data are written there */
static int write_fixture(const char *directory, const char *name, const bytes *b)
{
    char path[1024];
    if (snprintf(path, sizeof path, "%s/%s", directory, name) >= (int)sizeof path)
        return 0;
    FILE *file = fopen(path, "wb");
    int ok = file && fwrite(b->data, 1, b->size, file) == b->size;
    if (file && fclose(file) != 0)
        ok = 0;
    return ok;
}

int main(int argc, char **argv)
{
    bytes init = { 0 }, segment = { 0 }, second = { 0 };
    build_init(&init);
    build_segment(&segment, 30720, 88200, 1);
    build_segment(&second, 30720 + 3 * 1024, 88200 + 2048, 1);
    if (argc == 2) {
        CHECK(write_fixture(argv[1], "fmp4-init.mp4", &init));
        CHECK(write_fixture(argv[1], "fmp4-segment.m4s", &segment));
    }

    uint8_t *ts = NULL;
    size_t ts_size = 0;
    CHECK(yozora_fmp4_to_ts(init.data, init.size, segment.data, segment.size, &ts, &ts_size) == YOZORA_FMP4_OK);
    CHECK(ts && ts_size && ts_size % YOZORA_TS_PACKET == 0);
    int pat, pmt, video, audio, starts, keys;
    count_packets(ts, ts_size, &pat, &pmt, &video, &audio, &starts, &keys);
    /* tables once, three video samples, two audio frames, one key frame */
    CHECK(pat == 1 && pmt == 1);
    CHECK(video >= 3 && starts == 3);
    CHECK(audio >= 2);
    CHECK(keys == 1);
    free(ts);

    /* same input gives the same segment, a later fragment gives a later one */
    uint8_t *again = NULL, *next = NULL;
    size_t again_size = 0, next_size = 0;
    CHECK(yozora_fmp4_to_ts(init.data, init.size, segment.data, segment.size, &ts, &ts_size) == YOZORA_FMP4_OK);
    CHECK(yozora_fmp4_to_ts(init.data, init.size, segment.data, segment.size, &again, &again_size) == YOZORA_FMP4_OK);
    CHECK(ts_size == again_size && !memcmp(ts, again, ts_size));
    CHECK(yozora_fmp4_to_ts(init.data, init.size, second.data, second.size, &next, &next_size) == YOZORA_FMP4_OK);
    CHECK(next_size && memcmp(ts, next, ts_size < next_size ? ts_size : next_size) != 0);
    free(ts); free(again); free(next);

    /* refusals */
    uint8_t *none = (uint8_t *)1;
    size_t none_size = 7;
    CHECK(yozora_fmp4_to_ts(NULL, 0, segment.data, segment.size, &none, &none_size) == YOZORA_FMP4_BAD_ARGUMENT && !none && !none_size);
    CHECK(yozora_fmp4_to_ts(init.data, init.size, NULL, 0, &none, &none_size) == YOZORA_FMP4_BAD_ARGUMENT);
    CHECK(yozora_fmp4_to_ts(init.data, init.size, segment.data, segment.size, NULL, &none_size) == YOZORA_FMP4_BAD_ARGUMENT);
    CHECK(yozora_fmp4_to_ts(init.data, init.size, segment.data, segment.size - 5, &none, &none_size) == YOZORA_FMP4_MALFORMED && !none);
    CHECK(yozora_fmp4_to_ts(segment.data, segment.size, segment.data, segment.size, &none, &none_size) != YOZORA_FMP4_OK && !none);
    CHECK(yozora_fmp4_to_ts(init.data, 8, segment.data, segment.size, &none, &none_size) != YOZORA_FMP4_OK && !none);
    CHECK(yozora_fmp4_to_ts(init.data, init.size, init.data, init.size, &none, &none_size) == YOZORA_FMP4_MALFORMED && !none);
    CHECK(yozora_fmp4_to_ts(init.data, YOZORA_FMP4_MAX_INPUT + 1, segment.data, segment.size, &none, &none_size) == YOZORA_FMP4_TOO_LARGE);

    /* another video codec is refused */
    bytes hevc = { 0 };
    build_init(&hevc);
    for (size_t i = 0; i + 4 < hevc.size; ++i) {
        if (!memcmp(hevc.data + i, "avc1", 4)) {
            memcpy(hevc.data + i, "hvc1", 4);
            break;
        }
    }
    CHECK(yozora_fmp4_to_ts(hevc.data, hevc.size, segment.data, segment.size, &none, &none_size) != YOZORA_FMP4_OK && !none);

    free(init.data); free(segment.data); free(second.data); free(hevc.data);
    if (failures) {
        fprintf(stderr, "test_fmp4_ts: %d failures\n", failures);
        return 1;
    }
    printf("test_fmp4_ts: ok\n");
    return 0;
}
