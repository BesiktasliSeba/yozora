#include "yozora_hls.h"
#include "yozora_mp4.h"
#include "yozora_ts.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEGMENT_BYTES (24u * 1024u * 1024u)
#define MAX_AUDIO_ONLY_SEGMENT (2 * 90000)
/* 1024 samples of AAC at 44100 Hz, the length of the last frame of an audio only stream */
#define YOZORA_HLS_AAC_FRAME 2090
#define CONFIG_CAPACITY 512

typedef struct {
    int64_t dts, pts;
    int sync;
    uint32_t size;
    uint8_t *data;
} queued;

typedef struct {
    queued *items;
    size_t head, count, capacity;
} queue;

typedef struct {
    uint8_t *data;
    size_t size;
    /* from the start of the playlist */
    int64_t start, end;
} segment;

struct yozora_hls {
    pthread_mutex_t lock;
    unsigned references;
    int want_video, want_audio;
    int have_video, have_audio;
    uint8_t avcc[CONFIG_CAPACITY];
    size_t avcc_size;
    uint8_t asc[CONFIG_CAPACITY];
    size_t asc_size;
    yozora_ts_mux mux;
    int mux_ready;
    queue video, audio;
    size_t pending_bytes;
    int origin_set;
    int64_t origin;
    int has_last_video, has_last_audio;
    int64_t last_video_dts, last_audio_pts;
    segment *segments;
    size_t segment_count, segment_capacity;
    size_t segment_bytes;
    int ended;
    /* the segment being written */
    uint8_t *build;
    size_t build_size, build_capacity;
    int build_failed;
};

static queued *queue_at(queue *q, size_t i) { return &q->items[q->head + i]; }

static int queue_push(queue *q, int64_t dts, int64_t pts, int sync, const uint8_t *data, size_t size)
{
    if (q->head + q->count == q->capacity) {
        if (q->head) {
            memmove(q->items, q->items + q->head, q->count * sizeof *q->items);
            q->head = 0;
        } else {
            size_t capacity = q->capacity ? q->capacity * 2 : 256;
            queued *grown = realloc(q->items, capacity * sizeof *grown);
            if (!grown)
                return 0;
            q->items = grown;
            q->capacity = capacity;
        }
    }
    queued *item = &q->items[q->head + q->count];
    item->data = malloc(size ? size : 1);
    if (!item->data)
        return 0;
    if (size)
        memcpy(item->data, data, size);
    item->dts = dts;
    item->pts = pts;
    item->sync = sync;
    item->size = (uint32_t)size;
    ++q->count;
    return 1;
}

/* drops the first n items, returning the bytes they held */
static size_t queue_drop(queue *q, size_t n)
{
    size_t bytes = 0;
    for (size_t i = 0; i < n; ++i) {
        bytes += q->items[q->head + i].size;
        free(q->items[q->head + i].data);
    }
    q->head += n;
    q->count -= n;
    if (!q->count)
        q->head = 0;
    return bytes;
}

static void queue_free(queue *q)
{
    queue_drop(q, q->count);
    free(q->items);
    memset(q, 0, sizeof *q);
}

yozora_hls_status yozora_hls_create(int want_video, int want_audio, yozora_hls **out)
{
    if (!out || (!want_video && !want_audio))
        return YOZORA_HLS_BAD_ARGUMENT;
    *out = NULL;
    yozora_hls *hls = calloc(1, sizeof *hls);
    if (!hls)
        return YOZORA_HLS_NO_MEMORY;
    if (pthread_mutex_init(&hls->lock, NULL)) {
        free(hls);
        return YOZORA_HLS_NO_MEMORY;
    }
    hls->references = 1;
    hls->want_video = want_video != 0;
    hls->want_audio = want_audio != 0;
    *out = hls;
    return YOZORA_HLS_OK;
}

void yozora_hls_retain(yozora_hls *hls)
{
    if (!hls)
        return;
    pthread_mutex_lock(&hls->lock);
    ++hls->references;
    pthread_mutex_unlock(&hls->lock);
}

void yozora_hls_release(yozora_hls *hls)
{
    if (!hls)
        return;
    pthread_mutex_lock(&hls->lock);
    unsigned left = --hls->references;
    pthread_mutex_unlock(&hls->lock);
    if (left)
        return;
    queue_free(&hls->video);
    queue_free(&hls->audio);
    for (size_t i = 0; i < hls->segment_count; ++i)
        free(hls->segments[i].data);
    free(hls->segments);
    free(hls->build);
    pthread_mutex_destroy(&hls->lock);
    free(hls);
}

static int sink(void *context, const uint8_t packet[YOZORA_TS_PACKET])
{
    yozora_hls *hls = context;
    if (hls->build_failed)
        return 1;
    if (hls->build_size + YOZORA_TS_PACKET > MAX_SEGMENT_BYTES) {
        hls->build_failed = 1;
        return 1;
    }
    if (hls->build_size + YOZORA_TS_PACKET > hls->build_capacity) {
        size_t capacity = hls->build_capacity ? hls->build_capacity * 2 : 64 * 1024;
        uint8_t *grown = realloc(hls->build, capacity);
        if (!grown) {
            hls->build_failed = 1;
            return 1;
        }
        hls->build = grown;
        hls->build_capacity = capacity;
    }
    memcpy(hls->build + hls->build_size, packet, YOZORA_TS_PACKET);
    hls->build_size += YOZORA_TS_PACKET;
    return 0;
}

yozora_hls_status yozora_hls_set_video(yozora_hls *hls, const uint8_t *avcc, size_t size)
{
    if (!hls || !avcc || !size || size > CONFIG_CAPACITY)
        return YOZORA_HLS_BAD_ARGUMENT;
    pthread_mutex_lock(&hls->lock);
    yozora_hls_status status = YOZORA_HLS_OK;
    yozora_ts_mux probe;
    if (!hls->want_video || hls->have_video || yozora_ts_init(&probe, avcc, size, NULL, 0, sink, hls) != YOZORA_TS_OK)
        status = YOZORA_HLS_BAD_CONFIG;
    else {
        memcpy(hls->avcc, avcc, size);
        hls->avcc_size = size;
        hls->have_video = 1;
    }
    pthread_mutex_unlock(&hls->lock);
    return status;
}

yozora_hls_status yozora_hls_set_audio(yozora_hls *hls, const uint8_t *asc, size_t size)
{
    if (!hls || !asc || !size || size > CONFIG_CAPACITY)
        return YOZORA_HLS_BAD_ARGUMENT;
    pthread_mutex_lock(&hls->lock);
    yozora_hls_status status = YOZORA_HLS_OK;
    yozora_ts_mux probe;
    if (!hls->want_audio || hls->have_audio || yozora_ts_init(&probe, NULL, 0, asc, size, sink, hls) != YOZORA_TS_OK)
        status = YOZORA_HLS_BAD_CONFIG;
    else {
        memcpy(hls->asc, asc, size);
        hls->asc_size = size;
        hls->have_audio = 1;
    }
    pthread_mutex_unlock(&hls->lock);
    return status;
}

static int ready(const yozora_hls *hls)
{
    return (!hls->want_video || hls->have_video) && (!hls->want_audio || hls->have_audio);
}

static int64_t shifted(const yozora_hls *hls, int64_t media)
{
    return YOZORA_HLS_OFFSET + (media - hls->origin);
}

static yozora_hls_status segment_push(yozora_hls *hls, int64_t start, int64_t end)
{
    if (hls->segment_count >= YOZORA_HLS_MAX_SEGMENTS)
        return YOZORA_HLS_TOO_LARGE;
    if (hls->segment_count == hls->segment_capacity) {
        size_t capacity = hls->segment_capacity ? hls->segment_capacity * 2 : 32;
        segment *grown = realloc(hls->segments, capacity * sizeof *grown);
        if (!grown)
            return YOZORA_HLS_NO_MEMORY;
        hls->segments = grown;
        hls->segment_capacity = capacity;
    }
    segment *s = &hls->segments[hls->segment_count++];
    s->data = hls->build;
    s->size = hls->build_size;
    s->start = start;
    s->end = end;
    hls->segment_bytes += s->size;
    hls->build = NULL;
    hls->build_size = hls->build_capacity = 0;
    return YOZORA_HLS_OK;
}

/* writes the first video_count video samples and the audio samples before audio_end as one segment, in time order */
static yozora_hls_status cut(yozora_hls *hls, size_t video_count, int64_t audio_end, int64_t end_media)
{
    if (!hls->mux_ready) {
        const uint8_t *avcc = hls->want_video ? hls->avcc : NULL;
        const uint8_t *asc = hls->want_audio ? hls->asc : NULL;
        if (yozora_ts_init(&hls->mux, avcc, avcc ? hls->avcc_size : 0, asc, asc ? hls->asc_size : 0, sink, hls) != YOZORA_TS_OK)
            return YOZORA_HLS_BAD_CONFIG;
        hls->mux_ready = 1;
    }
    size_t audio_count = 0;
    while (audio_count < hls->audio.count && queue_at(&hls->audio, audio_count)->pts < audio_end)
        ++audio_count;
    int64_t start_media = hls->want_video ? queue_at(&hls->video, 0)->dts : queue_at(&hls->audio, 0)->pts;
    hls->build_size = 0;
    hls->build_failed = 0;
    yozora_ts_status status = yozora_ts_write_tables(&hls->mux);
    size_t vi = 0, ai = 0;
    while (status == YOZORA_TS_OK && (vi < video_count || ai < audio_count)) {
        int take_video = vi < video_count && (ai >= audio_count || queue_at(&hls->video, vi)->dts <= queue_at(&hls->audio, ai)->pts);
        if (take_video) {
            queued *v = queue_at(&hls->video, vi++);
            status = yozora_ts_write_video(&hls->mux, shifted(hls, v->dts), shifted(hls, v->pts), v->sync, v->data, v->size);
        } else {
            queued *a = queue_at(&hls->audio, ai++);
            status = yozora_ts_write_audio(&hls->mux, shifted(hls, a->pts), a->data, a->size);
        }
    }
    if (status != YOZORA_TS_OK || hls->build_failed) {
        hls->build_size = 0;
        return status == YOZORA_TS_NO_MEMORY || hls->build_failed ? YOZORA_HLS_NO_MEMORY : YOZORA_HLS_BAD_SAMPLE;
    }
    yozora_hls_status pushed = segment_push(hls, start_media - hls->origin, end_media - hls->origin);
    if (pushed != YOZORA_HLS_OK)
        return pushed;
    hls->pending_bytes -= queue_drop(&hls->video, video_count);
    hls->pending_bytes -= queue_drop(&hls->audio, audio_count);
    return YOZORA_HLS_OK;
}

/* the segments that can be cut now. while not ended a segment needs the next key frame (its end) and audio that reaches past it */
static yozora_hls_status try_cut(yozora_hls *hls)
{
    if (!hls->origin_set || !ready(hls))
        return YOZORA_HLS_OK;
    for (;;) {
        if (hls->want_video) {
            if (!hls->video.count)
                break;
            size_t end_index = 0;
            for (size_t i = 1; i < hls->video.count; ++i) {
                queued *v = queue_at(&hls->video, i);
                if (v->sync && v->dts - queue_at(&hls->video, 0)->dts >= YOZORA_HLS_MIN_SEGMENT) {
                    end_index = i;
                    break;
                }
            }
            int64_t end_media;
            size_t count;
            if (end_index) {
                count = end_index;
                end_media = queue_at(&hls->video, end_index)->dts;
                if (hls->want_audio && (!hls->audio.count || queue_at(&hls->audio, hls->audio.count - 1)->pts < end_media) && !hls->ended)
                    break;
            } else if (hls->ended) {
                count = hls->video.count;
                queued *last = queue_at(&hls->video, count - 1);
                int64_t step = count > 1 ? last->dts - queue_at(&hls->video, count - 2)->dts : 3000;
                end_media = last->dts + (step > 0 ? step : 3000);
            } else
                break;
            yozora_hls_status status = cut(hls, count, hls->ended && !end_index ? INT64_MAX : end_media, end_media);
            if (status != YOZORA_HLS_OK)
                return status;
        } else {
            if (!hls->audio.count)
                break;
            int64_t start = queue_at(&hls->audio, 0)->pts;
            size_t count = 0;
            while (count < hls->audio.count && queue_at(&hls->audio, count)->pts < start + MAX_AUDIO_ONLY_SEGMENT)
                ++count;
            /* the segment ends where the next one begins, so the durations of the playlist add up to the time of the audio */
            int64_t end_media;
            if (count < hls->audio.count)
                end_media = queue_at(&hls->audio, count)->pts;
            else if (hls->ended)
                end_media = queue_at(&hls->audio, count - 1)->pts + YOZORA_HLS_AAC_FRAME;
            else
                break;
            yozora_hls_status status = cut(hls, 0, end_media, end_media);
            if (status != YOZORA_HLS_OK)
                return status;
        }
    }
    /* audio that ends the stream after the last video sample has no picture to go with, it is dropped */
    if (hls->ended && hls->want_video && !hls->video.count && hls->audio.count)
        hls->pending_bytes -= queue_drop(&hls->audio, hls->audio.count);
    return YOZORA_HLS_OK;
}

yozora_hls_status yozora_hls_add_video(yozora_hls *hls, int64_t dts, int64_t pts, int sync, const uint8_t *data, size_t size)
{
    if (!hls || (!data && size) || size > YOZORA_TS_MAX_VIDEO_SAMPLE)
        return YOZORA_HLS_BAD_ARGUMENT;
    pthread_mutex_lock(&hls->lock);
    yozora_hls_status status = YOZORA_HLS_OK;
    if (hls->ended)
        status = YOZORA_HLS_ENDED;
    else if (!hls->want_video)
        status = YOZORA_HLS_BAD_CONFIG;
    else if (pts < dts || (hls->has_last_video && dts < hls->last_video_dts))
        status = YOZORA_HLS_BAD_SAMPLE;
    else if (!hls->origin_set && !sync) {
        /* the epoch begins at a key frame: what comes before the first one cannot be shown */
    } else if (hls->pending_bytes + size > YOZORA_HLS_MAX_PENDING_BYTES)
        status = YOZORA_HLS_TOO_LARGE;
    else {
        if (!hls->origin_set) {
            hls->origin_set = 1;
            hls->origin = dts;
            /* audio that arrived before the first key frame and starts before it */
            size_t early = 0;
            while (early < hls->audio.count && queue_at(&hls->audio, early)->pts < dts)
                ++early;
            hls->pending_bytes -= queue_drop(&hls->audio, early);
        }
        if (!queue_push(&hls->video, dts, pts, sync, data, size))
            status = YOZORA_HLS_NO_MEMORY;
        else {
            hls->pending_bytes += size;
            hls->has_last_video = 1;
            hls->last_video_dts = dts;
            status = try_cut(hls);
        }
    }
    pthread_mutex_unlock(&hls->lock);
    return status;
}

yozora_hls_status yozora_hls_add_audio(yozora_hls *hls, int64_t pts, const uint8_t *data, size_t size)
{
    if (!hls || !data || !size || size > 8000)
        return YOZORA_HLS_BAD_ARGUMENT;
    pthread_mutex_lock(&hls->lock);
    yozora_hls_status status = YOZORA_HLS_OK;
    if (hls->ended)
        status = YOZORA_HLS_ENDED;
    else if (!hls->want_audio)
        status = YOZORA_HLS_BAD_CONFIG;
    else if (hls->has_last_audio && pts < hls->last_audio_pts)
        status = YOZORA_HLS_BAD_SAMPLE;
    else if (hls->origin_set && pts < hls->origin) {
        /* before the start of the epoch */
    } else if (hls->pending_bytes + size > YOZORA_HLS_MAX_PENDING_BYTES)
        status = YOZORA_HLS_TOO_LARGE;
    else {
        if (!hls->origin_set && !hls->want_video) {
            hls->origin_set = 1;
            hls->origin = pts;
        }
        if (!queue_push(&hls->audio, pts, pts, 1, data, size))
            status = YOZORA_HLS_NO_MEMORY;
        else {
            hls->pending_bytes += size;
            hls->has_last_audio = 1;
            hls->last_audio_pts = pts;
            status = try_cut(hls);
        }
    }
    pthread_mutex_unlock(&hls->lock);
    return status;
}

yozora_hls_status yozora_hls_end(yozora_hls *hls)
{
    if (!hls)
        return YOZORA_HLS_BAD_ARGUMENT;
    pthread_mutex_lock(&hls->lock);
    yozora_hls_status status = YOZORA_HLS_OK;
    if (!hls->ended) {
        hls->ended = 1;
        status = try_cut(hls);
    }
    pthread_mutex_unlock(&hls->lock);
    return status;
}

int64_t yozora_hls_origin(yozora_hls *hls)
{
    if (!hls)
        return 0;
    pthread_mutex_lock(&hls->lock);
    int64_t origin = hls->origin_set ? hls->origin : 0;
    pthread_mutex_unlock(&hls->lock);
    return origin;
}

int64_t yozora_hls_duration(yozora_hls *hls)
{
    if (!hls)
        return 0;
    pthread_mutex_lock(&hls->lock);
    int64_t duration = hls->segment_count ? hls->segments[hls->segment_count - 1].end : 0;
    pthread_mutex_unlock(&hls->lock);
    return duration;
}

unsigned yozora_hls_segment_count(yozora_hls *hls)
{
    if (!hls)
        return 0;
    pthread_mutex_lock(&hls->lock);
    unsigned count = (unsigned)hls->segment_count;
    pthread_mutex_unlock(&hls->lock);
    return count;
}

int yozora_hls_ended(yozora_hls *hls)
{
    if (!hls)
        return 0;
    pthread_mutex_lock(&hls->lock);
    int ended = hls->ended;
    pthread_mutex_unlock(&hls->lock);
    return ended;
}

void yozora_hls_trim(yozora_hls *hls, int64_t before)
{
    if (!hls)
        return;
    pthread_mutex_lock(&hls->lock);
    /* the last segment stays whatever the time says, the player may still be asking for it */
    for (size_t i = 0; i + 1 < hls->segment_count; ++i) {
        segment *s = &hls->segments[i];
        if (s->end > before)
            break;
        if (s->data) {
            hls->segment_bytes -= s->size;
            free(s->data);
            s->data = NULL;
        }
    }
    pthread_mutex_unlock(&hls->lock);
}

size_t yozora_hls_memory(yozora_hls *hls)
{
    if (!hls)
        return 0;
    pthread_mutex_lock(&hls->lock);
    size_t bytes = hls->segment_bytes + hls->pending_bytes;
    pthread_mutex_unlock(&hls->lock);
    return bytes;
}

/* the playlist: integer arithmetic only, so no locale puts a comma in a duration */
static char *playlist(const yozora_hls *hls, size_t *size)
{
    size_t capacity = 256 + hls->segment_count * 48;
    char *text = malloc(capacity);
    if (!text)
        return NULL;
    int64_t longest = 0;
    for (size_t i = 0; i < hls->segment_count; ++i) {
        int64_t d = hls->segments[i].end - hls->segments[i].start;
        if (d > longest)
            longest = d;
    }
    int64_t target = (longest + 89999) / 90000;
    if (target < 3)
        target = 3;
    size_t n = 0;
    n += (size_t)snprintf(text + n, capacity - n, "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-PLAYLIST-TYPE:EVENT\n#EXT-X-TARGETDURATION:%lld\n#EXT-X-MEDIA-SEQUENCE:0\n", (long long)target);
    for (size_t i = 0; i < hls->segment_count; ++i) {
        int64_t d = hls->segments[i].end - hls->segments[i].start;
        int64_t ms = d * 1000 / 90000;
        if (ms < 1)
            ms = 1;
        n += (size_t)snprintf(text + n, capacity - n, "#EXTINF:%lld.%03lld,\nseg%zu.ts\n", (long long)(ms / 1000), (long long)(ms % 1000), i);
    }
    if (hls->ended)
        n += (size_t)snprintf(text + n, capacity - n, "#EXT-X-ENDLIST\n");
    *size = n;
    return text;
}

/* "seg" digits ".ts" with nothing else: a name with a sign, a space or a leading zero is not one of ours */
static int parse_segment_name(const char *name, size_t *index)
{
    if (strncmp(name, "seg", 3))
        return 0;
    const char *p = name + 3;
    if (!*p || (*p == '0' && p[1] != '.'))
        return 0;
    size_t value = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
        if (value > (size_t)(YOZORA_HLS_MAX_SEGMENTS))
            return 0;
        value = value * 10 + (size_t)(*p - '0');
        ++p;
        ++digits;
    }
    if (!digits || strcmp(p, ".ts"))
        return 0;
    *index = value;
    return 1;
}

yozora_hls_read_status yozora_hls_read(yozora_hls *hls, const char *name, uint64_t offset, uint8_t *out, size_t capacity, size_t *got, uint64_t *total,
                                       const char **content_type)
{
    if (got)
        *got = 0;
    if (total)
        *total = 0;
    if (!hls || !name || (!out && capacity) || !got || !total || !content_type)
        return YOZORA_HLS_READ_NOT_FOUND;
    pthread_mutex_lock(&hls->lock);
    const uint8_t *source = NULL;
    size_t length = 0;
    char *text = NULL;
    yozora_hls_read_status status = YOZORA_HLS_READ_OK;
    if (!strcmp(name, YOZORA_HLS_NAME_PLAYLIST)) {
        text = playlist(hls, &length);
        source = (const uint8_t *)text;
        *content_type = "application/vnd.apple.mpegurl";
        if (!text)
            status = YOZORA_HLS_READ_NOT_FOUND;
    } else {
        size_t index;
        if (!parse_segment_name(name, &index) || index >= hls->segment_count || !hls->segments[index].data)
            status = YOZORA_HLS_READ_NOT_FOUND;
        else {
            source = hls->segments[index].data;
            length = hls->segments[index].size;
            *content_type = "video/mp2t";
        }
    }
    if (status == YOZORA_HLS_READ_OK) {
        *total = length;
        if (offset >= length && length)
            status = YOZORA_HLS_READ_RANGE;
        else {
            size_t take = length - (size_t)offset;
            if (take > capacity)
                take = capacity;
            if (take)
                memcpy(out, source + offset, take);
            *got = take;
        }
    }
    pthread_mutex_unlock(&hls->lock);
    free(text);
    return status;
}


#define FMP4_MAX_OUTPUT (64u * 1024u * 1024u)
#define FMP4_MAX_SAMPLES 262144u

typedef struct {
    int video;
    int64_t dts, pts;
    int sync;
    size_t offset;
    uint32_t size;
} fmp4_sample;

typedef struct {
    yozora_mp4_track video, audio;
    int has_video, has_audio;
    uint8_t *arena;
    size_t arena_size, arena_capacity;
    fmp4_sample *samples;
    size_t count, capacity;
    yozora_fmp4_status failure;
} fmp4_collect;

typedef struct {
    uint8_t *data;
    size_t size, capacity;
    int failed;
} fmp4_output;

static int fmp4_on_init(void *context, const yozora_mp4_track *tracks, unsigned count)
{
    fmp4_collect *c = context;
    for (unsigned i = 0; i < count; ++i) {
        if (tracks[i].kind == YOZORA_MP4_TRACK_VIDEO && !c->has_video) {
            c->video = tracks[i];
            c->has_video = 1;
        } else if (tracks[i].kind == YOZORA_MP4_TRACK_AUDIO && !c->has_audio) {
            c->audio = tracks[i];
            c->has_audio = 1;
        }
    }
    return 0;
}

static int fmp4_on_sample(void *context, const yozora_mp4_track *track, const yozora_mp4_sample *sample)
{
    fmp4_collect *c = context;
    int video;
    if (c->has_video && track->id == c->video.id && track->kind == YOZORA_MP4_TRACK_VIDEO)
        video = 1;
    else if (c->has_audio && track->id == c->audio.id && track->kind == YOZORA_MP4_TRACK_AUDIO)
        video = 0;
    else
        return 0;
    if (c->count >= FMP4_MAX_SAMPLES || sample->size > MAX_SEGMENT_BYTES || c->arena_size + sample->size > FMP4_MAX_OUTPUT) {
        c->failure = YOZORA_FMP4_TOO_LARGE;
        return 1;
    }
    if (c->count == c->capacity) {
        size_t grown = c->capacity ? c->capacity * 2 : 256;
        fmp4_sample *bigger = realloc(c->samples, grown * sizeof *bigger);
        if (!bigger) {
            c->failure = YOZORA_FMP4_NO_MEMORY;
            return 1;
        }
        c->samples = bigger;
        c->capacity = grown;
    }
    if (c->arena_size + sample->size > c->arena_capacity) {
        size_t grown = (c->arena_size + sample->size) * 2;
        uint8_t *bigger = realloc(c->arena, grown);
        if (!bigger) {
            c->failure = YOZORA_FMP4_NO_MEMORY;
            return 1;
        }
        c->arena = bigger;
        c->arena_capacity = grown;
    }
    memcpy(c->arena + c->arena_size, sample->data, sample->size);
    uint32_t scale = track->timescale;
    fmp4_sample *at = &c->samples[c->count++];
    at->video = video;
    at->dts = yozora_ts_scale(sample->dts, scale) + YOZORA_HLS_OFFSET;
    at->pts = yozora_ts_scale(sample->pts, scale) + YOZORA_HLS_OFFSET;
    if (at->pts < at->dts)
        at->pts = at->dts;
    at->sync = sample->sync;
    at->offset = c->arena_size;
    at->size = sample->size;
    c->arena_size += sample->size;
    return 0;
}

static int fmp4_sink(void *context, const uint8_t packet[YOZORA_TS_PACKET])
{
    fmp4_output *o = context;
    if (o->size + YOZORA_TS_PACKET > FMP4_MAX_OUTPUT) {
        o->failed = 1;
        return 1;
    }
    if (o->size + YOZORA_TS_PACKET > o->capacity) {
        size_t grown = o->capacity ? o->capacity * 2 : 64 * 1024;
        uint8_t *bigger = realloc(o->data, grown);
        if (!bigger) {
            o->failed = 1;
            return 1;
        }
        o->data = bigger;
        o->capacity = grown;
    }
    memcpy(o->data + o->size, packet, YOZORA_TS_PACKET);
    o->size += YOZORA_TS_PACKET;
    return 0;
}

static yozora_fmp4_status fmp4_from_mp4(yozora_mp4_status status)
{
    switch (status) {
    case YOZORA_MP4_OK: return YOZORA_FMP4_OK;
    case YOZORA_MP4_UNSUPPORTED: return YOZORA_FMP4_UNSUPPORTED;
    case YOZORA_MP4_TOO_LARGE: return YOZORA_FMP4_TOO_LARGE;
    case YOZORA_MP4_NO_MEMORY: return YOZORA_FMP4_NO_MEMORY;
    default: return YOZORA_FMP4_MALFORMED;
    }
}

static size_t fmp4_next(const fmp4_collect *c, size_t index, int video)
{
    while (index < c->count && c->samples[index].video != video)
        ++index;
    return index;
}

yozora_fmp4_status yozora_fmp4_to_ts(const uint8_t *init, size_t init_size, const uint8_t *segment, size_t segment_size, uint8_t **out, size_t *out_size)
{
    if (out)
        *out = NULL;
    if (out_size)
        *out_size = 0;
    if (!init || !init_size || !segment || !segment_size || !out || !out_size)
        return YOZORA_FMP4_BAD_ARGUMENT;
    if (init_size > YOZORA_FMP4_MAX_INPUT || segment_size > YOZORA_FMP4_MAX_INPUT)
        return YOZORA_FMP4_TOO_LARGE;
    fmp4_collect collect;
    memset(&collect, 0, sizeof collect);
    fmp4_output output;
    memset(&output, 0, sizeof output);
    yozora_fmp4_status result = YOZORA_FMP4_OK;
    yozora_mp4 *parser = NULL;
    yozora_mp4_sink sink = { fmp4_on_init, fmp4_on_sample, &collect };
    yozora_mp4_status parsed = yozora_mp4_create(&sink, &parser);
    if (parsed != YOZORA_MP4_OK)
        return fmp4_from_mp4(parsed);
    parsed = yozora_mp4_feed(parser, init, init_size);
    if (parsed == YOZORA_MP4_OK && !collect.has_video && !collect.has_audio)
        parsed = YOZORA_MP4_MALFORMED;
    if (parsed == YOZORA_MP4_OK)
        parsed = yozora_mp4_feed(parser, segment, segment_size);
    /* a cut download ends inside a box */
    if (parsed == YOZORA_MP4_OK && yozora_mp4_pending(parser))
        parsed = YOZORA_MP4_MALFORMED;
    yozora_mp4_destroy(parser);
    if (parsed != YOZORA_MP4_OK)
        result = parsed == YOZORA_MP4_ABORTED && collect.failure ? collect.failure : fmp4_from_mp4(parsed);
    else if (!collect.count)
        result = YOZORA_FMP4_MALFORMED;
    if (result == YOZORA_FMP4_OK) {
        yozora_ts_mux mux;
        yozora_ts_status ts = yozora_ts_init(&mux, collect.has_video ? collect.video.config : NULL, collect.has_video ? collect.video.config_size : 0,
                                             collect.has_audio ? collect.audio.config : NULL, collect.has_audio ? collect.audio.config_size : 0, fmp4_sink, &output);
        if (ts == YOZORA_TS_OK)
            ts = yozora_ts_write_tables(&mux);
        size_t v = fmp4_next(&collect, 0, 1), a = fmp4_next(&collect, 0, 0);
        while (ts == YOZORA_TS_OK && (v < collect.count || a < collect.count)) {
            /* interleaved by time */
            if (a >= collect.count || (v < collect.count && collect.samples[v].dts <= collect.samples[a].pts)) {
                const fmp4_sample *s = &collect.samples[v];
                ts = yozora_ts_write_video(&mux, s->dts, s->pts, s->sync, collect.arena + s->offset, s->size);
                v = fmp4_next(&collect, v + 1, 1);
            } else {
                const fmp4_sample *s = &collect.samples[a];
                ts = yozora_ts_write_audio(&mux, s->pts, collect.arena + s->offset, s->size);
                a = fmp4_next(&collect, a + 1, 0);
            }
        }
        if (ts != YOZORA_TS_OK)
            result = output.failed ? YOZORA_FMP4_TOO_LARGE : ts == YOZORA_TS_NO_MEMORY ? YOZORA_FMP4_NO_MEMORY : YOZORA_FMP4_UNSUPPORTED;
    }
    free(collect.arena);
    free(collect.samples);
    if (result != YOZORA_FMP4_OK) {
        free(output.data);
        return result;
    }
    *out = output.data;
    *out_size = output.size;
    return YOZORA_FMP4_OK;
}
