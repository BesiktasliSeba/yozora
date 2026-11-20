#include "yozora_mp4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define F(a, b, c, d) YOZORA_MP4_FOURCC(a, b, c, d)
#define NEED_MORE ((yozora_mp4_status)-1)

typedef struct {
    uint32_t duration, size, flags;
    int32_t cts;
} run_sample;

typedef struct {
    unsigned track;
    /* the position of the first byte of the run in the whole stream */
    uint64_t data_start;
    int64_t decode_time;
    unsigned first, count;
} run;

typedef struct {
    uint32_t track_id, duration, size, flags;
    int present;
} trex_defaults;

struct yozora_mp4 {
    yozora_mp4_sink sink;
    uint8_t *buffer;
    size_t start, length, capacity;
    /* the position in the stream of buffer[start] */
    uint64_t position;
    yozora_mp4_track tracks[YOZORA_MP4_MAX_TRACKS];
    int64_t next_dts[YOZORA_MP4_MAX_TRACKS];
    unsigned track_count;
    trex_defaults trex[YOZORA_MP4_MAX_TRACKS];
    int have_init;
    run_sample *samples;
    size_t sample_count, sample_capacity;
    run *runs;
    size_t run_count, run_capacity;
    int moof_pending;
};

typedef struct {
    uint32_t type;
    const uint8_t *body;
    size_t body_size;
    size_t total;
} box;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint64_t rd64(const uint8_t *p) { return ((uint64_t)rd32(p) << 32) | rd32(p + 4); }

/* reads the header of the box at p, NEED_MORE while not all of it is in avail */
static yozora_mp4_status box_read(const uint8_t *p, size_t avail, box *out)
{
    if (avail < 8)
        return NEED_MORE;
    uint64_t size = rd32(p);
    size_t header = 8;
    out->type = rd32(p + 4);
    if (size == 1) {
        if (avail < 16)
            return NEED_MORE;
        size = rd64(p + 8);
        header = 16;
    } else if (size == 0) {
        /* a box that runs to the end of the stream has no length to wait for */
        return YOZORA_MP4_UNSUPPORTED;
    }
    if (size < header)
        return YOZORA_MP4_MALFORMED;
    if (size > YOZORA_MP4_MAX_BOX)
        return YOZORA_MP4_TOO_LARGE;
    if (avail < size)
        return NEED_MORE;
    out->body = p + header;
    out->body_size = (size_t)size - header;
    out->total = (size_t)size;
    return YOZORA_MP4_OK;
}

/* the next child of a box that is all in memory: a child that is cut off is malformed */
static yozora_mp4_status child_next(const uint8_t **cursor, const uint8_t *end, box *out)
{
    if (*cursor >= end)
        return NEED_MORE;
    yozora_mp4_status status = box_read(*cursor, (size_t)(end - *cursor), out);
    if (status == NEED_MORE)
        return YOZORA_MP4_MALFORMED;
    if (status == YOZORA_MP4_OK)
        *cursor += out->total;
    return status;
}

static yozora_mp4_status grow_samples(yozora_mp4 *parser, size_t extra)
{
    if (parser->sample_count + extra > YOZORA_MP4_MAX_RUN_SAMPLES)
        return YOZORA_MP4_TOO_LARGE;
    if (parser->sample_count + extra <= parser->sample_capacity)
        return YOZORA_MP4_OK;
    size_t capacity = parser->sample_capacity ? parser->sample_capacity : 256;
    while (capacity < parser->sample_count + extra)
        capacity *= 2;
    run_sample *grown = realloc(parser->samples, capacity * sizeof(run_sample));
    if (!grown)
        return YOZORA_MP4_NO_MEMORY;
    parser->samples = grown;
    parser->sample_capacity = capacity;
    return YOZORA_MP4_OK;
}

static yozora_mp4_status grow_runs(yozora_mp4 *parser)
{
    if (parser->run_count < parser->run_capacity)
        return YOZORA_MP4_OK;
    size_t capacity = parser->run_capacity ? parser->run_capacity * 2 : 8;
    if (capacity > 256)
        return YOZORA_MP4_TOO_LARGE;
    run *grown = realloc(parser->runs, capacity * sizeof(run));
    if (!grown)
        return YOZORA_MP4_NO_MEMORY;
    parser->runs = grown;
    parser->run_capacity = capacity;
    return YOZORA_MP4_OK;
}

/* a descriptor length: up to four bytes of seven bits */
static int read_descriptor_length(const uint8_t **p, const uint8_t *end, uint32_t *length)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        if (*p >= end)
            return 0;
        uint8_t byte = *(*p)++;
        value = (value << 7) | (byte & 0x7f);
        if (!(byte & 0x80)) {
            *length = value;
            return 1;
        }
    }
    return 0;
}

/* the AudioSpecificConfig inside an esds box. only AAC (object type 0x40) is taken */
static yozora_mp4_status parse_esds(const uint8_t *body, size_t size, yozora_mp4_track *track)
{
    if (size < 4)
        return YOZORA_MP4_MALFORMED;
    const uint8_t *p = body + 4;
    const uint8_t *end = body + size;
    uint32_t length;
    if (p >= end || *p++ != 0x03 || !read_descriptor_length(&p, end, &length))
        return YOZORA_MP4_MALFORMED;
    if (end - p < 3)
        return YOZORA_MP4_MALFORMED;
    uint8_t flags = p[2];
    p += 3;
    if (flags & 0x80)
        p += 2;
    if (flags & 0x40) {
        if (p >= end)
            return YOZORA_MP4_MALFORMED;
        p += 1 + *p;
    }
    if (flags & 0x20)
        p += 2;
    if (p >= end || *p++ != 0x04 || !read_descriptor_length(&p, end, &length))
        return YOZORA_MP4_MALFORMED;
    if (end - p < 13)
        return YOZORA_MP4_MALFORMED;
    if (p[0] != 0x40)
        return YOZORA_MP4_UNSUPPORTED;
    p += 13;
    if (p >= end || *p++ != 0x05 || !read_descriptor_length(&p, end, &length))
        return YOZORA_MP4_MALFORMED;
    if (length < 2 || length > YOZORA_MP4_MAX_CONFIG || (size_t)(end - p) < length)
        return YOZORA_MP4_MALFORMED;
    memcpy(track->config, p, length);
    track->config_size = length;
    return YOZORA_MP4_OK;
}

static yozora_mp4_status parse_stsd(const uint8_t *body, size_t size, yozora_mp4_track *track)
{
    if (size < 8)
        return YOZORA_MP4_MALFORMED;
    if (rd32(body + 4) < 1)
        return YOZORA_MP4_MALFORMED;
    const uint8_t *cursor = body + 8;
    const uint8_t *end = body + size;
    box entry;
    yozora_mp4_status status = child_next(&cursor, end, &entry);
    if (status == NEED_MORE)
        return YOZORA_MP4_MALFORMED;
    if (status != YOZORA_MP4_OK)
        return status;
    track->codec = entry.type;
    size_t fixed;
    if (track->kind == YOZORA_MP4_TRACK_VIDEO) {
        if (entry.type != YOZORA_MP4_CODEC_AVC1 && entry.type != YOZORA_MP4_CODEC_AVC3)
            return YOZORA_MP4_UNSUPPORTED;
        fixed = 78;
        if (entry.body_size < fixed)
            return YOZORA_MP4_MALFORMED;
        track->width = rd16(entry.body + 24);
        track->height = rd16(entry.body + 26);
    } else {
        if (entry.type != YOZORA_MP4_CODEC_MP4A)
            return YOZORA_MP4_UNSUPPORTED;
        fixed = 28;
        if (entry.body_size < fixed)
            return YOZORA_MP4_MALFORMED;
        uint16_t version = rd16(entry.body + 8);
        if (version == 1)
            fixed += 16;
        else if (version != 0)
            return YOZORA_MP4_UNSUPPORTED;
        if (entry.body_size < fixed)
            return YOZORA_MP4_MALFORMED;
        track->channels = rd16(entry.body + 16);
        track->sample_rate = rd32(entry.body + 24) >> 16;
    }
    const uint8_t *child = entry.body + fixed;
    const uint8_t *child_end = entry.body + entry.body_size;
    box inner;
    int found = 0;
    while ((status = child_next(&child, child_end, &inner)) == YOZORA_MP4_OK) {
        if (track->kind == YOZORA_MP4_TRACK_VIDEO && inner.type == F('a', 'v', 'c', 'C')) {
            if (inner.body_size < 7 || inner.body_size > YOZORA_MP4_MAX_CONFIG)
                return YOZORA_MP4_MALFORMED;
            memcpy(track->config, inner.body, inner.body_size);
            track->config_size = (uint32_t)inner.body_size;
            found = 1;
        } else if (track->kind == YOZORA_MP4_TRACK_AUDIO && inner.type == F('e', 's', 'd', 's')) {
            status = parse_esds(inner.body, inner.body_size, track);
            if (status != YOZORA_MP4_OK)
                return status;
            found = 1;
        }
    }
    if (status != NEED_MORE)
        return status;
    return found ? YOZORA_MP4_OK : YOZORA_MP4_MALFORMED;
}

static yozora_mp4_status parse_trak(const uint8_t *body, size_t size, yozora_mp4_track *track, int *usable)
{
    memset(track, 0, sizeof *track);
    *usable = 0;
    const uint8_t *cursor = body;
    const uint8_t *end = body + size;
    box child;
    yozora_mp4_status status;
    int have_id = 0, have_scale = 0;
    const uint8_t *stsd = NULL;
    size_t stsd_size = 0;
    while ((status = child_next(&cursor, end, &child)) == YOZORA_MP4_OK) {
        if (child.type == F('t', 'k', 'h', 'd')) {
            if (child.body_size < 4)
                return YOZORA_MP4_MALFORMED;
            size_t at = child.body[0] == 1 ? 20 : 12;
            if (child.body_size < at + 4)
                return YOZORA_MP4_MALFORMED;
            track->id = rd32(child.body + at);
            have_id = 1;
        } else if (child.type == F('m', 'd', 'i', 'a')) {
            const uint8_t *mc = child.body;
            const uint8_t *me = child.body + child.body_size;
            box inner;
            while ((status = child_next(&mc, me, &inner)) == YOZORA_MP4_OK) {
                if (inner.type == F('m', 'd', 'h', 'd')) {
                    size_t at = inner.body_size > 0 && inner.body[0] == 1 ? 20 : 12;
                    if (inner.body_size < at + 4)
                        return YOZORA_MP4_MALFORMED;
                    track->timescale = rd32(inner.body + at);
                    have_scale = 1;
                } else if (inner.type == F('h', 'd', 'l', 'r')) {
                    if (inner.body_size < 12)
                        return YOZORA_MP4_MALFORMED;
                    uint32_t handler = rd32(inner.body + 8);
                    track->kind = handler == F('v', 'i', 'd', 'e') ? YOZORA_MP4_TRACK_VIDEO : handler == F('s', 'o', 'u', 'n') ? YOZORA_MP4_TRACK_AUDIO : 0;
                } else if (inner.type == F('m', 'i', 'n', 'f')) {
                    const uint8_t *nc = inner.body;
                    const uint8_t *ne = inner.body + inner.body_size;
                    box deep;
                    while ((status = child_next(&nc, ne, &deep)) == YOZORA_MP4_OK) {
                        if (deep.type != F('s', 't', 'b', 'l'))
                            continue;
                        const uint8_t *sc = deep.body;
                        const uint8_t *se = deep.body + deep.body_size;
                        box table;
                        while ((status = child_next(&sc, se, &table)) == YOZORA_MP4_OK) {
                            if (table.type == F('s', 't', 's', 'd')) {
                                stsd = table.body;
                                stsd_size = table.body_size;
                            }
                        }
                        if (status != NEED_MORE)
                            return status;
                    }
                    if (status != NEED_MORE)
                        return status;
                }
            }
            if (status != NEED_MORE)
                return status;
        }
    }
    if (status != NEED_MORE)
        return status;
    if (!have_id || !have_scale || !track->timescale)
        return YOZORA_MP4_MALFORMED;
    /* a text or other track is not an error, the page just has nothing of it to play */
    if (!track->kind)
        return YOZORA_MP4_OK;
    if (!stsd)
        return YOZORA_MP4_MALFORMED;
    status = parse_stsd(stsd, stsd_size, track);
    if (status != YOZORA_MP4_OK)
        return status;
    *usable = 1;
    return YOZORA_MP4_OK;
}

static yozora_mp4_status parse_moov(yozora_mp4 *parser, const box *moov)
{
    yozora_mp4_track tracks[YOZORA_MP4_MAX_TRACKS];
    trex_defaults trex[YOZORA_MP4_MAX_TRACKS];
    unsigned count = 0, trex_count = 0;
    memset(trex, 0, sizeof trex);
    const uint8_t *cursor = moov->body;
    const uint8_t *end = moov->body + moov->body_size;
    box child;
    yozora_mp4_status status;
    while ((status = child_next(&cursor, end, &child)) == YOZORA_MP4_OK) {
        if (child.type == F('t', 'r', 'a', 'k')) {
            int usable;
            if (count == YOZORA_MP4_MAX_TRACKS)
                return YOZORA_MP4_TOO_LARGE;
            status = parse_trak(child.body, child.body_size, &tracks[count], &usable);
            if (status != YOZORA_MP4_OK)
                return status;
            if (usable)
                ++count;
        } else if (child.type == F('m', 'v', 'e', 'x')) {
            const uint8_t *mc = child.body;
            const uint8_t *me = child.body + child.body_size;
            box inner;
            while ((status = child_next(&mc, me, &inner)) == YOZORA_MP4_OK) {
                if (inner.type != F('t', 'r', 'e', 'x'))
                    continue;
                if (inner.body_size < 24)
                    return YOZORA_MP4_MALFORMED;
                if (trex_count == YOZORA_MP4_MAX_TRACKS)
                    return YOZORA_MP4_TOO_LARGE;
                trex[trex_count].track_id = rd32(inner.body + 4);
                trex[trex_count].duration = rd32(inner.body + 12);
                trex[trex_count].size = rd32(inner.body + 16);
                trex[trex_count].flags = rd32(inner.body + 20);
                trex[trex_count].present = 1;
                ++trex_count;
            }
            if (status != NEED_MORE)
                return status;
        }
    }
    if (status != NEED_MORE)
        return status;
    if (!count)
        return YOZORA_MP4_UNSUPPORTED;
    memcpy(parser->tracks, tracks, count * sizeof tracks[0]);
    parser->track_count = count;
    memcpy(parser->trex, trex, sizeof trex);
    for (unsigned i = 0; i < YOZORA_MP4_MAX_TRACKS; ++i)
        parser->next_dts[i] = 0;
    parser->have_init = 1;
    if (parser->sink.on_init && parser->sink.on_init(parser->sink.context, parser->tracks, parser->track_count))
        return YOZORA_MP4_ABORTED;
    return YOZORA_MP4_OK;
}

static int track_index(const yozora_mp4 *parser, uint32_t id)
{
    for (unsigned i = 0; i < parser->track_count; ++i) {
        if (parser->tracks[i].id == id)
            return (int)i;
    }
    return -1;
}

static const trex_defaults *trex_of(const yozora_mp4 *parser, uint32_t id)
{
    for (unsigned i = 0; i < YOZORA_MP4_MAX_TRACKS; ++i) {
        if (parser->trex[i].present && parser->trex[i].track_id == id)
            return &parser->trex[i];
    }
    return NULL;
}

typedef struct {
    int track;
    uint32_t default_duration, default_size, default_flags;
    int64_t decode_time;
    int have_decode_time;
    uint64_t next_data;
} traf_state;

static yozora_mp4_status parse_trun(yozora_mp4 *parser, const box *trun, traf_state *traf, uint64_t moof_position)
{
    if (trun->body_size < 8)
        return YOZORA_MP4_MALFORMED;
    uint8_t version = trun->body[0];
    uint32_t flags = ((uint32_t)trun->body[1] << 16) | ((uint32_t)trun->body[2] << 8) | trun->body[3];
    uint32_t count = rd32(trun->body + 4);
    const uint8_t *p = trun->body + 8;
    const uint8_t *end = trun->body + trun->body_size;
    int64_t data_offset = 0;
    int has_offset = 0;
    uint32_t first_flags = 0;
    int has_first_flags = 0;
    if (flags & 0x1) {
        if (end - p < 4)
            return YOZORA_MP4_MALFORMED;
        data_offset = (int32_t)rd32(p);
        p += 4;
        has_offset = 1;
    }
    if (flags & 0x4) {
        if (end - p < 4)
            return YOZORA_MP4_MALFORMED;
        first_flags = rd32(p);
        p += 4;
        has_first_flags = 1;
    }
    size_t per_sample = ((flags & 0x100) ? 4u : 0u) + ((flags & 0x200) ? 4u : 0u) + ((flags & 0x400) ? 4u : 0u) + ((flags & 0x800) ? 4u : 0u);
    if (count > YOZORA_MP4_MAX_RUN_SAMPLES)
        return YOZORA_MP4_TOO_LARGE;
    if (per_sample && (size_t)(end - p) / per_sample < count)
        return YOZORA_MP4_MALFORMED;
    yozora_mp4_status status = grow_samples(parser, count);
    if (status != YOZORA_MP4_OK)
        return status;
    status = grow_runs(parser);
    if (status != YOZORA_MP4_OK)
        return status;
    run *r = &parser->runs[parser->run_count];
    r->track = (unsigned)traf->track;
    r->first = (unsigned)parser->sample_count;
    r->count = count;
    r->decode_time = traf->have_decode_time ? traf->decode_time : parser->next_dts[traf->track];
    if (has_offset) {
        int64_t at = (int64_t)moof_position + data_offset;
        if (at < 0)
            return YOZORA_MP4_MALFORMED;
        r->data_start = (uint64_t)at;
    } else
        r->data_start = traf->next_data;
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        run_sample *s = &parser->samples[parser->sample_count + i];
        s->duration = (flags & 0x100) ? rd32(p) : traf->default_duration;
        if (flags & 0x100)
            p += 4;
        s->size = (flags & 0x200) ? rd32(p) : traf->default_size;
        if (flags & 0x200)
            p += 4;
        if (flags & 0x400) {
            s->flags = rd32(p);
            p += 4;
        } else
            s->flags = traf->default_flags;
        if (i == 0 && has_first_flags)
            s->flags = first_flags;
        s->cts = 0;
        if (flags & 0x800) {
            s->cts = (int32_t)rd32(p);
            /* version 0 holds an unsigned offset, a value past 2^31 is a hostile or a broken file */
            if (!version && s->cts < 0)
                return YOZORA_MP4_MALFORMED;
            p += 4;
        }
        total += s->size;
    }
    traf->next_data = r->data_start + total;
    traf->decode_time = r->decode_time;
    traf->have_decode_time = 0;
    parser->sample_count += count;
    ++parser->run_count;
    return YOZORA_MP4_OK;
}

static yozora_mp4_status parse_traf(yozora_mp4 *parser, const box *traf_box, uint64_t moof_position, uint64_t moof_end)
{
    traf_state traf;
    memset(&traf, 0, sizeof traf);
    traf.track = -1;
    traf.next_data = moof_end + 8;
    const uint8_t *cursor = traf_box->body;
    const uint8_t *end = traf_box->body + traf_box->body_size;
    box child;
    yozora_mp4_status status;
    while ((status = child_next(&cursor, end, &child)) == YOZORA_MP4_OK) {
        if (child.type == F('t', 'f', 'h', 'd')) {
            if (child.body_size < 8)
                return YOZORA_MP4_MALFORMED;
            uint32_t flags = ((uint32_t)child.body[1] << 16) | ((uint32_t)child.body[2] << 8) | child.body[3];
            uint32_t id = rd32(child.body + 4);
            const uint8_t *p = child.body + 8;
            const uint8_t *e = child.body + child.body_size;
            if (flags & 0x1)
                return YOZORA_MP4_UNSUPPORTED;
            if (flags & 0x2) {
                if (e - p < 4)
                    return YOZORA_MP4_MALFORMED;
                p += 4;
            }
            traf.track = track_index(parser, id);
            const trex_defaults *defaults = trex_of(parser, id);
            if (defaults) {
                traf.default_duration = defaults->duration;
                traf.default_size = defaults->size;
                traf.default_flags = defaults->flags;
            }
            if (flags & 0x8) {
                if (e - p < 4)
                    return YOZORA_MP4_MALFORMED;
                traf.default_duration = rd32(p);
                p += 4;
            }
            if (flags & 0x10) {
                if (e - p < 4)
                    return YOZORA_MP4_MALFORMED;
                traf.default_size = rd32(p);
                p += 4;
            }
            if (flags & 0x20) {
                if (e - p < 4)
                    return YOZORA_MP4_MALFORMED;
                traf.default_flags = rd32(p);
            }
        } else if (child.type == F('t', 'f', 'd', 't')) {
            if (child.body_size < 8)
                return YOZORA_MP4_MALFORMED;
            if (child.body[0] == 1) {
                if (child.body_size < 12)
                    return YOZORA_MP4_MALFORMED;
                uint64_t time = rd64(child.body + 4);
                if (time > (uint64_t)INT64_MAX)
                    return YOZORA_MP4_MALFORMED;
                traf.decode_time = (int64_t)time;
            } else
                traf.decode_time = rd32(child.body + 4);
            traf.have_decode_time = 1;
        } else if (child.type == F('t', 'r', 'u', 'n')) {
            if (traf.track < 0) {
                /* a track the initialization segment does not describe (a track of another kind): its samples are skipped */
                continue;
            }
            status = parse_trun(parser, &child, &traf, moof_position);
            if (status != YOZORA_MP4_OK)
                return status;
        }
    }
    return status == NEED_MORE ? YOZORA_MP4_OK : status;
}

static yozora_mp4_status parse_moof(yozora_mp4 *parser, const box *moof, uint64_t moof_position)
{
    parser->sample_count = 0;
    parser->run_count = 0;
    const uint8_t *cursor = moof->body;
    const uint8_t *end = moof->body + moof->body_size;
    box child;
    yozora_mp4_status status;
    while ((status = child_next(&cursor, end, &child)) == YOZORA_MP4_OK) {
        if (child.type == F('t', 'r', 'a', 'f')) {
            status = parse_traf(parser, &child, moof_position, moof_position + moof->total);
            if (status != YOZORA_MP4_OK)
                return status;
        }
    }
    if (status != NEED_MORE)
        return status;
    parser->moof_pending = 1;
    return YOZORA_MP4_OK;
}

static yozora_mp4_status emit_mdat(yozora_mp4 *parser, const box *mdat, uint64_t mdat_position)
{
    if (!parser->moof_pending)
        return YOZORA_MP4_OK;
    parser->moof_pending = 0;
    uint64_t body_start = mdat_position + (mdat->total - mdat->body_size);
    uint64_t body_end = mdat_position + mdat->total;
    for (size_t i = 0; i < parser->run_count; ++i) {
        const run *r = &parser->runs[i];
        uint64_t at = r->data_start;
        int64_t dts = r->decode_time;
        const yozora_mp4_track *track = &parser->tracks[r->track];
        for (unsigned n = 0; n < r->count; ++n) {
            const run_sample *s = &parser->samples[r->first + n];
            if (at < body_start || at > body_end || (uint64_t)s->size > body_end - at)
                return YOZORA_MP4_MALFORMED;
            yozora_mp4_sample out;
            out.dts = dts;
            out.pts = dts + s->cts;
            out.duration = s->duration;
            /* bit 16 of the sample flags is sample_is_non_sync_sample, audio samples are all sync */
            out.sync = track->kind == YOZORA_MP4_TRACK_AUDIO || !(s->flags & 0x10000);
            out.data = mdat->body + (size_t)(at - body_start);
            out.size = s->size;
            if (parser->sink.on_sample && parser->sink.on_sample(parser->sink.context, track, &out))
                return YOZORA_MP4_ABORTED;
            at += s->size;
            dts += s->duration;
        }
        parser->next_dts[r->track] = dts;
    }
    return YOZORA_MP4_OK;
}

yozora_mp4_status yozora_mp4_create(const yozora_mp4_sink *sink, yozora_mp4 **out)
{
    if (!sink || !out)
        return YOZORA_MP4_MALFORMED;
    *out = NULL;
    yozora_mp4 *parser = calloc(1, sizeof *parser);
    if (!parser)
        return YOZORA_MP4_NO_MEMORY;
    parser->sink = *sink;
    *out = parser;
    return YOZORA_MP4_OK;
}

void yozora_mp4_destroy(yozora_mp4 *parser)
{
    if (!parser)
        return;
    free(parser->buffer);
    free(parser->samples);
    free(parser->runs);
    free(parser);
}

void yozora_mp4_reset(yozora_mp4 *parser)
{
    if (!parser)
        return;
    parser->position += parser->length;
    parser->start = 0;
    parser->length = 0;
    parser->moof_pending = 0;
    parser->sample_count = 0;
    parser->run_count = 0;
}

size_t yozora_mp4_pending(const yozora_mp4 *parser)
{
    return parser ? parser->length : 0;
}

static yozora_mp4_status append(yozora_mp4 *parser, const uint8_t *data, size_t size)
{
    if (size > YOZORA_MP4_MAX_BOX + 16 || parser->length > YOZORA_MP4_MAX_BOX + 16 - size)
        return YOZORA_MP4_TOO_LARGE;
    if (parser->start && parser->start + parser->length + size > parser->capacity) {
        memmove(parser->buffer, parser->buffer + parser->start, parser->length);
        parser->start = 0;
    }
    if (parser->start + parser->length + size > parser->capacity) {
        size_t capacity = parser->capacity ? parser->capacity : 65536;
        while (capacity < parser->length + size)
            capacity *= 2;
        uint8_t *grown = realloc(parser->buffer, capacity);
        if (!grown)
            return YOZORA_MP4_NO_MEMORY;
        parser->buffer = grown;
        parser->capacity = capacity;
    }
    memcpy(parser->buffer + parser->start + parser->length, data, size);
    parser->length += size;
    return YOZORA_MP4_OK;
}

yozora_mp4_status yozora_mp4_feed(yozora_mp4 *parser, const uint8_t *data, size_t size)
{
    if (!parser || (!data && size))
        return YOZORA_MP4_MALFORMED;
    yozora_mp4_status status = append(parser, data, size);
    if (status != YOZORA_MP4_OK)
        return status;
    for (;;) {
        box current;
        status = box_read(parser->buffer + parser->start, parser->length, &current);
        if (status == NEED_MORE)
            return YOZORA_MP4_OK;
        if (status != YOZORA_MP4_OK)
            return status;
        if (current.type == F('m', 'o', 'o', 'v')) {
            status = parse_moov(parser, &current);
        } else if (current.type == F('m', 'o', 'o', 'f')) {
            if (!parser->have_init)
                return YOZORA_MP4_NO_INIT;
            status = parse_moof(parser, &current, parser->position);
        } else if (current.type == F('m', 'd', 'a', 't')) {
            status = emit_mdat(parser, &current, parser->position);
        }
        /* ftyp, styp, sidx, free, emsg and the rest carry nothing a decoder needs */
        if (status != YOZORA_MP4_OK)
            return status;
        parser->start += current.total;
        parser->length -= current.total;
        parser->position += current.total;
    }
}

int yozora_mp4_track_codec_string(const yozora_mp4_track *track, char *out, size_t capacity)
{
    if (!track || !out || !capacity)
        return 0;
    int written;
    if (track->kind == YOZORA_MP4_TRACK_VIDEO && track->config_size >= 4) {
        written = snprintf(out, capacity, "%c%c%c%c.%02x%02x%02x", (int)(track->codec >> 24), (int)((track->codec >> 16) & 255), (int)((track->codec >> 8) & 255),
            (int)(track->codec & 255), track->config[1], track->config[2], track->config[3]);
    } else if (track->kind == YOZORA_MP4_TRACK_AUDIO && track->config_size >= 2) {
        written = snprintf(out, capacity, "mp4a.40.%u", (unsigned)(track->config[0] >> 3));
    } else {
        out[0] = 0;
        return 0;
    }
    return written > 0 && (size_t)written < capacity;
}
