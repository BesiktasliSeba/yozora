#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yozora_mp4.h"
#include "yozora_ts.h"

static int failures;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_mp4_ts: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

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

typedef struct {
    int inits;
    unsigned tracks;
    yozora_mp4_track track[YOZORA_MP4_MAX_TRACKS];
    int video_count, audio_count;
    int64_t video_dts[8], video_pts[8], audio_dts[8];
    int video_sync[8];
    uint32_t video_size[8];
    uint8_t video_first_byte[8];
    int data_ok;
    int abort_after;
    int seen;
} capture;

static int on_init(void *context, const yozora_mp4_track *tracks, unsigned count)
{
    capture *c = context;
    ++c->inits;
    c->tracks = count;
    memcpy(c->track, tracks, count * sizeof tracks[0]);
    return 0;
}

static int on_sample(void *context, const yozora_mp4_track *track, const yozora_mp4_sample *sample)
{
    capture *c = context;
    ++c->seen;
    if (c->abort_after && c->seen >= c->abort_after)
        return 1;
    if (track->kind == YOZORA_MP4_TRACK_VIDEO) {
        int i = c->video_count++;
        c->video_dts[i] = sample->dts;
        c->video_pts[i] = sample->pts;
        c->video_sync[i] = sample->sync;
        c->video_size[i] = sample->size;
        c->video_first_byte[i] = sample->size > 4 ? sample->data[4] : 0;
        if (sample->size != video_sample_size(i))
            c->data_ok = 0;
    } else {
        int i = c->audio_count++;
        c->audio_dts[i] = sample->dts;
        if (sample->size != sizeof aac_frames[i] || memcmp(sample->data, aac_frames[i], sizeof aac_frames[i]))
            c->data_ok = 0;
    }
    return 0;
}

typedef struct {
    uint8_t packets[256][YOZORA_TS_PACKET];
    int count;
    int fail_at;
} ts_out;

static int ts_sink(void *context, const uint8_t packet[YOZORA_TS_PACKET])
{
    ts_out *o = context;
    if (o->fail_at && o->count + 1 >= o->fail_at)
        return 1;
    if (o->count >= 256)
        return 1;
    memcpy(o->packets[o->count++], packet, YOZORA_TS_PACKET);
    return 0;
}

static void parsing(void)
{
    bytes init = { 0 }, segment = { 0 };
    build_init(&init);
    build_segment(&segment, 30720, 88200, 1);

    capture c;
    memset(&c, 0, sizeof c);
    c.data_ok = 1;
    yozora_mp4_sink sink = { on_init, on_sample, &c };
    yozora_mp4 *parser = NULL;
    CHECK(yozora_mp4_create(&sink, &parser) == YOZORA_MP4_OK);
    /* a page appends in pieces of any size: one byte at a time reaches every boundary */
    for (size_t i = 0; i < init.size; ++i)
        CHECK(yozora_mp4_feed(parser, init.data + i, 1) == YOZORA_MP4_OK);
    CHECK(c.inits == 1 && c.tracks == 2);
    CHECK(c.track[0].id == 1 && c.track[0].kind == YOZORA_MP4_TRACK_VIDEO && c.track[0].codec == YOZORA_MP4_CODEC_AVC1);
    CHECK(c.track[0].timescale == 15360 && c.track[0].width == 640 && c.track[0].height == 360);
    /* the avcC box as written: 6 bytes of header, the SPS with its length, the PPS count and the PPS with its length */
    CHECK(c.track[0].config_size == 6 + 2 + sizeof sps + 1 + 2 + sizeof pps);
    CHECK(c.track[1].id == 2 && c.track[1].kind == YOZORA_MP4_TRACK_AUDIO && c.track[1].codec == YOZORA_MP4_CODEC_MP4A);
    CHECK(c.track[1].timescale == 44100 && c.track[1].channels == 2 && c.track[1].sample_rate == 44100);
    CHECK(c.track[1].config_size == 2 && c.track[1].config[0] == 0x12 && c.track[1].config[1] == 0x10);
    char codec[32];
    CHECK(yozora_mp4_track_codec_string(&c.track[0], codec, sizeof codec) && !strcmp(codec, "avc1.64001f"));
    CHECK(yozora_mp4_track_codec_string(&c.track[1], codec, sizeof codec) && !strcmp(codec, "mp4a.40.2"));
    CHECK(!yozora_mp4_track_codec_string(&c.track[1], codec, 4));

    for (size_t i = 0; i < segment.size; ++i)
        CHECK(yozora_mp4_feed(parser, segment.data + i, 1) == YOZORA_MP4_OK);
    CHECK(c.video_count == 3 && c.audio_count == 2 && c.data_ok);
    /* decode times run on from the tfdt by the duration, the presentation time adds the composition offset,
       the first flags make the first sample sync */
    CHECK(c.video_dts[0] == 30720 && c.video_dts[1] == 31744 && c.video_dts[2] == 32768);
    CHECK(c.video_pts[0] == 31744 && c.video_pts[1] == 33792 && c.video_pts[2] == 32768);
    CHECK(c.video_sync[0] == 1 && c.video_sync[1] == 0 && c.video_sync[2] == 0);
    CHECK(c.audio_dts[0] == 88200 && c.audio_dts[1] == 89224);
    CHECK(yozora_mp4_pending(parser) == 0);

    /* a segment without tfdt continues where the track stopped */
    bytes next = { 0 };
    build_segment(&next, 0, 0, 0);
    c.video_count = c.audio_count = 0;
    CHECK(yozora_mp4_feed(parser, next.data, next.size) == YOZORA_MP4_OK);
    CHECK(c.video_count == 3 && c.video_dts[0] == 33792);
    CHECK(c.audio_count == 2 && c.audio_dts[0] == 90248);

    /* an initialization segment appended again is announced again, the parser goes on */
    CHECK(yozora_mp4_feed(parser, init.data, init.size) == YOZORA_MP4_OK && c.inits == 2);
    yozora_mp4_destroy(parser);

    /* a callback that says stop stops the parse */
    memset(&c, 0, sizeof c);
    c.data_ok = 1;
    c.abort_after = 2;
    CHECK(yozora_mp4_create(&sink, &parser) == YOZORA_MP4_OK);
    CHECK(yozora_mp4_feed(parser, init.data, init.size) == YOZORA_MP4_OK);
    CHECK(yozora_mp4_feed(parser, segment.data, segment.size) == YOZORA_MP4_ABORTED);
    yozora_mp4_destroy(parser);
    free(next.data);
    free(init.data);
    free(segment.data);
}

static void refusing(void)
{
    bytes init = { 0 }, segment = { 0 };
    build_init(&init);
    build_segment(&segment, 0, 0, 1);
    capture c;
    memset(&c, 0, sizeof c);
    c.data_ok = 1;
    yozora_mp4_sink sink = { on_init, on_sample, &c };
    yozora_mp4 *parser = NULL;
    CHECK(yozora_mp4_create(&sink, &parser) == YOZORA_MP4_OK);
    /* a moof before the initialization segment */
    CHECK(yozora_mp4_feed(parser, segment.data, segment.size) == YOZORA_MP4_NO_INIT);
    yozora_mp4_reset(parser);
    CHECK(yozora_mp4_pending(parser) == 0);
    CHECK(yozora_mp4_feed(parser, init.data, init.size) == YOZORA_MP4_OK);

    /* a box of unknown length (size 0), and a box larger than the limit */
    uint8_t open_ended[8] = { 0, 0, 0, 0, 'f', 'r', 'e', 'e' };
    CHECK(yozora_mp4_feed(parser, open_ended, sizeof open_ended) == YOZORA_MP4_UNSUPPORTED);
    yozora_mp4_reset(parser);
    uint8_t huge[8] = { 0x7f, 0xff, 0xff, 0xff, 'm', 'd', 'a', 't' };
    CHECK(yozora_mp4_feed(parser, huge, sizeof huge) == YOZORA_MP4_TOO_LARGE);
    yozora_mp4_reset(parser);
    uint8_t tiny[8] = { 0, 0, 0, 4, 'f', 'r', 'e', 'e' };
    CHECK(yozora_mp4_feed(parser, tiny, sizeof tiny) == YOZORA_MP4_MALFORMED);
    yozora_mp4_reset(parser);

    /* the samples of a moof that run past its mdat: the last byte of the segment is cut off by a smaller mdat size */
    bytes cut = { 0 };
    put(&cut, segment.data, segment.size - 3);
    size_t mdat_at = 0;
    for (size_t i = 0; i + 4 <= cut.size; ++i) {
        if (!memcmp(cut.data + i, "mdat", 4))
            mdat_at = i - 4;
    }
    uint32_t smaller = (uint32_t)(cut.size - mdat_at);
    cut.data[mdat_at] = (uint8_t)(smaller >> 24); cut.data[mdat_at + 1] = (uint8_t)(smaller >> 16); cut.data[mdat_at + 2] = (uint8_t)(smaller >> 8); cut.data[mdat_at + 3] = (uint8_t)smaller;
    CHECK(yozora_mp4_feed(parser, cut.data, cut.size) == YOZORA_MP4_MALFORMED);
    yozora_mp4_reset(parser);
    free(cut.data);

    /* a track count of zero (a moov without a track that plays) */
    bytes empty = { 0 };
    size_t moov = open_box(&empty, "moov");
    close_box(&empty, moov);
    CHECK(yozora_mp4_feed(parser, empty.data, empty.size) == YOZORA_MP4_UNSUPPORTED);
    free(empty.data);
    yozora_mp4_destroy(parser);

    /* a video codec that is not H.264 */
    bytes vp9 = { 0 };
    build_init(&vp9);
    for (size_t i = 0; i + 4 <= vp9.size; ++i) {
        if (!memcmp(vp9.data + i, "avc1", 4)) {
            memcpy(vp9.data + i, "vp09", 4);
            break;
        }
    }
    CHECK(yozora_mp4_create(&sink, &parser) == YOZORA_MP4_OK);
    CHECK(yozora_mp4_feed(parser, vp9.data, vp9.size) == YOZORA_MP4_UNSUPPORTED);
    yozora_mp4_destroy(parser);
    free(vp9.data);
    free(init.data);
    free(segment.data);
}

/* the packets of one pid, joined into the PES packets that started on them: a tiny demuxer for the checks below */
typedef struct {
    int pes_count;
    uint8_t payload[4096];
    size_t payload_size;
    int64_t pts, dts;
    int has_dts;
    int random_access;
    int pcr;
    int continuity_ok;
} pid_view;

static void view_pid(const ts_out *o, unsigned pid, int pes_index, pid_view *view)
{
    memset(view, 0, sizeof *view);
    view->continuity_ok = 1;
    int index = -1, expect = -1;
    for (int p = 0; p < o->count; ++p) {
        const uint8_t *pkt = o->packets[p];
        unsigned this_pid = ((pkt[1] & 0x1fu) << 8) | pkt[2];
        if (this_pid != pid)
            continue;
        int counter = pkt[3] & 15;
        if (expect >= 0 && counter != expect)
            view->continuity_ok = 0;
        expect = (counter + 1) & 15;
        size_t at = 4;
        int start = (pkt[1] & 0x40) != 0;
        if (pkt[3] & 0x20) {
            size_t length = pkt[4];
            if (start && length >= 1) {
                if (pkt[5] & 0x40 && index + 1 == pes_index)
                    view->random_access = 1;
                if (pkt[5] & 0x10 && index + 1 == pes_index)
                    view->pcr = 1;
            }
            at = 5 + length;
        }
        if (start)
            ++index;
        if (index != pes_index)
            continue;
        if (start) {
            const uint8_t *h = pkt + at;
            view->pes_count = 1;
            unsigned flags = h[7];
            size_t header_length = h[8];
            const uint8_t *t = h + 9;
            view->pts = ((int64_t)((t[0] >> 1) & 7) << 30) | ((int64_t)t[1] << 22) | ((int64_t)(t[2] >> 1) << 15) | ((int64_t)t[3] << 7) | (t[4] >> 1);
            if (flags & 0x40) {
                const uint8_t *d = t + 5;
                view->dts = ((int64_t)((d[0] >> 1) & 7) << 30) | ((int64_t)d[1] << 22) | ((int64_t)(d[2] >> 1) << 15) | ((int64_t)d[3] << 7) | (d[4] >> 1);
                view->has_dts = 1;
            } else
                view->dts = view->pts;
            at += 9 + header_length;
        }
        size_t take = YOZORA_TS_PACKET - at;
        memcpy(view->payload + view->payload_size, pkt + at, take);
        view->payload_size += take;
    }
}

static void muxing(void)
{
    bytes avcc = { 0 };
    build_avcc(&avcc);
    ts_out *out = calloc(1, sizeof *out);
    yozora_ts_mux mux;
    CHECK(yozora_ts_init(&mux, avcc.data, avcc.size, asc, sizeof asc, ts_sink, out) == YOZORA_TS_OK);
    CHECK(mux.has_video && mux.has_audio && mux.nal_length_size == 4);
    CHECK(mux.aac_profile == 1 && mux.aac_frequency_index == 4 && mux.aac_channels == 2);
    CHECK(mux.parameter_sets_size == 4 + sizeof sps + 4 + sizeof pps);

    CHECK(yozora_ts_write_tables(&mux) == YOZORA_TS_OK);
    CHECK(out->count == 2);
    CHECK(out->packets[0][0] == 0x47 && (((out->packets[0][1] & 0x1f) << 8) | out->packets[0][2]) == 0 && (out->packets[0][1] & 0x40));
    CHECK(out->packets[0][5] == 0x00 && out->packets[1][5] == 0x02);
    CHECK((((out->packets[1][1] & 0x1f) << 8) | out->packets[1][2]) == YOZORA_TS_PID_PMT);

    /* a sync sample with its parameter sets, the sample of a B frame with pts before dts is not possible so pts equals dts there */
    bytes sample = { 0 };
    build_video_sample(&sample, 0);
    CHECK(yozora_ts_write_video(&mux, 900000, 903000, 1, sample.data, sample.size) == YOZORA_TS_OK);
    bytes sample1 = { 0 };
    build_video_sample(&sample1, 1);
    CHECK(yozora_ts_write_video(&mux, 903000, 903000, 0, sample1.data, sample1.size) == YOZORA_TS_OK);
    CHECK(yozora_ts_write_audio(&mux, 900500, aac_frames[0], sizeof aac_frames[0]) == YOZORA_TS_OK);
    CHECK(yozora_ts_write_audio(&mux, 902600, aac_frames[1], sizeof aac_frames[1]) == YOZORA_TS_OK);
    for (int i = 0; i < out->count; ++i)
        CHECK(out->packets[i][0] == 0x47);

    pid_view v;
    view_pid(out, YOZORA_TS_PID_VIDEO, 0, &v);
    CHECK(v.pes_count == 1 && v.pts == 903000 && v.dts == 900000 && v.has_dts && v.random_access && v.pcr && v.continuity_ok);
    static const uint8_t expected_idr[] = { 0, 0, 0, 1, 0x09, 0xf0, 0, 0, 0, 1, 0x67, 0x64, 0x00, 0x1f, 0xac, 0xd9, 0x40, 0x50, 0x05, 0xbb, 0, 0, 0, 1, 0x68, 0xeb, 0xe3, 0xcb, 0x22, 0xc0,
        0, 0, 0, 1, 0x65, 0x88, 0x84, 0x00, 0x33, 0xff };
    /* the payload of a packet with stuffing holds exactly what was written, up to the packet end */
    CHECK(v.payload_size >= sizeof expected_idr && !memcmp(v.payload, expected_idr, sizeof expected_idr));
    view_pid(out, YOZORA_TS_PID_VIDEO, 1, &v);
    static const uint8_t expected_p[] = { 0, 0, 0, 1, 0x09, 0xf0, 0, 0, 0, 1, 0x06, 0x05, 0x01, 0xff, 0x80, 0, 0, 0, 1, 0x41, 0x9a, 0x24, 0x6c };
    CHECK(v.pes_count == 1 && v.pts == 903000 && !v.has_dts && !v.random_access && v.pcr);
    CHECK(v.payload_size == sizeof expected_p && !memcmp(v.payload, expected_p, sizeof expected_p));
    CHECK(v.continuity_ok);

    view_pid(out, YOZORA_TS_PID_AUDIO, 1, &v);
    uint8_t expected_adts[7 + 5] = { 0xff, 0xf1, 0x50, 0x80, 0x01, 0x9f, 0xfc };
    memcpy(expected_adts + 7, aac_frames[1], 5);
    CHECK(v.pes_count == 1 && v.pts == 902600 && v.continuity_ok);
    CHECK(v.payload_size == sizeof expected_adts && !memcmp(v.payload, expected_adts, sizeof expected_adts));

    /* a long sample: many packets, the counter wraps and the payload comes back whole */
    size_t big_nal = 3000;
    bytes big = { 0 };
    put32(&big, (uint32_t)big_nal);
    for (size_t i = 0; i < big_nal; ++i) put8(&big, (unsigned)(i * 7 + 1));
    int before = out->count;
    CHECK(yozora_ts_write_video(&mux, 910000, 910000, 0, big.data, big.size) == YOZORA_TS_OK);
    /* the PES is 14 bytes of header and 3010 of payload: the first packet has room for 176 after its adaptation field, then 184 each */
    CHECK(out->count - before == 17);
    view_pid(out, YOZORA_TS_PID_VIDEO, 2, &v);
    CHECK(v.continuity_ok && v.pes_count == 1 && v.payload_size == 6 + 4 + big_nal);
    CHECK(!memcmp(v.payload, "\0\0\0\1\x09\xf0\0\0\0\1", 10));
    int big_ok = 1;
    for (size_t i = 0; i < big_nal; ++i) {
        if (v.payload[10 + i] != (uint8_t)(i * 7 + 1))
            big_ok = 0;
    }
    CHECK(big_ok);

    /* the tables again at the next segment: the counters go on */
    int tables_at = out->count;
    CHECK(yozora_ts_write_tables(&mux) == YOZORA_TS_OK);
    CHECK((out->packets[tables_at][3] & 15) == 1 && (out->packets[tables_at + 1][3] & 15) == 1);

    /* a sample whose NAL units do not add up, and bad arguments */
    uint8_t broken[] = { 0, 0, 0, 9, 0x65, 0x88 };
    CHECK(yozora_ts_write_video(&mux, 0, 0, 1, broken, sizeof broken) == YOZORA_TS_BAD_SAMPLE);
    uint8_t zero_length[] = { 0, 0, 0, 0 };
    CHECK(yozora_ts_write_video(&mux, 0, 0, 1, zero_length, sizeof zero_length) == YOZORA_TS_BAD_SAMPLE);
    uint8_t cut_prefix[] = { 0, 0 };
    CHECK(yozora_ts_write_video(&mux, 0, 0, 1, cut_prefix, sizeof cut_prefix) == YOZORA_TS_BAD_SAMPLE);
    CHECK(yozora_ts_write_audio(&mux, 0, aac_frames[0], 0) == YOZORA_TS_BAD_ARGUMENT);
    uint8_t huge_frame[9000] = { 0 };
    CHECK(yozora_ts_write_audio(&mux, 0, huge_frame, sizeof huge_frame) == YOZORA_TS_BAD_SAMPLE);

    /* a sink that fails stops the writer */
    ts_out *failing = calloc(1, sizeof *failing);
    failing->fail_at = 3;
    yozora_ts_mux other;
    CHECK(yozora_ts_init(&other, avcc.data, avcc.size, NULL, 0, ts_sink, failing) == YOZORA_TS_OK);
    CHECK(yozora_ts_write_tables(&other) == YOZORA_TS_OK);
    CHECK(yozora_ts_write_video(&other, 100, 100, 1, sample.data, sample.size) == YOZORA_TS_ABORTED);
    free(failing);

    /* configs that are refused: an avcC cut short, HE-AAC, a channel count of zero, an index past the table */
    yozora_ts_mux bad;
    CHECK(yozora_ts_init(&bad, avcc.data, 6, NULL, 0, ts_sink, out) == YOZORA_TS_BAD_CONFIG);
    uint8_t he_aac[] = { 0x2b, 0x92 };
    CHECK(yozora_ts_init(&bad, NULL, 0, he_aac, sizeof he_aac, ts_sink, out) == YOZORA_TS_BAD_CONFIG);
    uint8_t no_channels[] = { 0x12, 0x00 };
    CHECK(yozora_ts_init(&bad, NULL, 0, no_channels, sizeof no_channels, ts_sink, out) == YOZORA_TS_BAD_CONFIG);
    uint8_t high_index[] = { 0x17, 0x90 };
    CHECK(yozora_ts_init(&bad, NULL, 0, high_index, sizeof high_index, ts_sink, out) == YOZORA_TS_BAD_CONFIG);
    CHECK(yozora_ts_init(&bad, NULL, 0, NULL, 0, ts_sink, out) == YOZORA_TS_BAD_ARGUMENT);

    /* audio only: the clock moves to the audio pid */
    ts_out *audio_only = calloc(1, sizeof *audio_only);
    yozora_ts_mux a;
    CHECK(yozora_ts_init(&a, NULL, 0, asc, sizeof asc, ts_sink, audio_only) == YOZORA_TS_OK);
    CHECK(yozora_ts_write_tables(&a) == YOZORA_TS_OK);
    CHECK(yozora_ts_write_audio(&a, 5000, aac_frames[0], sizeof aac_frames[0]) == YOZORA_TS_OK);
    CHECK(audio_only->packets[1][13] == (0xe0 | (YOZORA_TS_PID_AUDIO >> 8)) && audio_only->packets[1][14] == (YOZORA_TS_PID_AUDIO & 0xff));
    view_pid(audio_only, YOZORA_TS_PID_AUDIO, 0, &v);
    CHECK(v.pcr);
    free(audio_only);

    /* the scale to 90 kHz: exact for whole ratios, rounded otherwise, no overflow for an hour at the largest timescale of a track */
    CHECK(yozora_ts_scale(15360, 15360) == 90000);
    CHECK(yozora_ts_scale(512, 15360) == 3000);
    CHECK(yozora_ts_scale(1024, 44100) == 2090);
    CHECK(yozora_ts_scale(0, 44100) == 0);
    CHECK(yozora_ts_scale(100, 0) == 0);
    CHECK(yozora_ts_scale((int64_t)3600 * 1000000, 1000000) == (int64_t)3600 * 90000);
    CHECK(yozora_ts_scale(-1024, 44100) == -2090);

    free(big.data);
    free(sample1.data);
    free(sample.data);
    free(avcc.data);
    free(out);
}

int main(void)
{
    parsing();
    refusing();
    muxing();
    if (failures) {
        fprintf(stderr, "test_mp4_ts: %d failures\n", failures);
        return 1;
    }
    puts("test_mp4_ts: ok");
    return 0;
}
