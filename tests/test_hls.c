#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yozora_hls.h"
#include "yozora_ts.h"

static int failures;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_hls: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

/* an avcC with one SPS and one PPS, and an AAC-LC 44100 Hz stereo config */
static const uint8_t avcc[] = { 1, 0x64, 0, 0x1f, 0xff, 0xe1, 0, 4, 0x67, 0x64, 0, 0x1f, 1, 0, 2, 0x68, 0xeb };
static const uint8_t asc[] = { 0x12, 0x10 };
/* one NAL unit of four bytes with its length prefix */
static const uint8_t nal[] = { 0, 0, 0, 4, 0x65, 1, 2, 3 };
static const uint8_t frame[] = { 0x21, 0x10, 0x04, 0x60 };

#define SECOND 90000

static yozora_hls *make(int video, int audio)
{
    yozora_hls *hls = NULL;
    CHECK(yozora_hls_create(video, audio, &hls) == YOZORA_HLS_OK);
    if (video)
        CHECK(yozora_hls_set_video(hls, avcc, sizeof avcc) == YOZORA_HLS_OK);
    if (audio)
        CHECK(yozora_hls_set_audio(hls, asc, sizeof asc) == YOZORA_HLS_OK);
    return hls;
}

static char *read_all(yozora_hls *hls, const char *name, uint64_t *total, yozora_hls_read_status *status)
{
    static uint8_t buffer[1 << 20];
    size_t got = 0;
    const char *type = NULL;
    *status = yozora_hls_read(hls, name, 0, buffer, sizeof buffer - 1, &got, total, &type);
    buffer[got] = 0;
    return (char *)buffer;
}

/* video: a key frame every second, 25 frames a second; audio: a frame every 1024 samples at 44100 Hz */
static void feed(yozora_hls *hls, int video, int audio, int64_t from, int64_t to, int sync_every_frames)
{
    int64_t frame_step = SECOND / 25;
    int64_t index = 0;
    for (int64_t t = from; t < to; t += frame_step, ++index) {
        if (video)
            CHECK(yozora_hls_add_video(hls, t, t + 2 * frame_step, sync_every_frames ? index % sync_every_frames == 0 : 0, nal, sizeof nal) == YOZORA_HLS_OK);
    }
    if (audio) {
        for (int64_t t = from; t < to; t += 2090)
            CHECK(yozora_hls_add_audio(hls, t, frame, sizeof frame) == YOZORA_HLS_OK);
    }
}

static int count_packets_with_pid(const uint8_t *data, size_t size, unsigned pid)
{
    int count = 0;
    for (size_t i = 0; i + 188 <= size; i += 188) {
        if (data[i] != 0x47)
            return -1;
        if ((unsigned)(((data[i + 1] & 0x1f) << 8) | data[i + 2]) == pid)
            ++count;
    }
    return count;
}

static void segmenting(void)
{
    yozora_hls *hls = make(1, 1);
    /* nothing is cut before the epoch holds a segment and the audio reaches past its end: one second of video
       with a key frame at its start */
    feed(hls, 1, 1, 10 * SECOND, 11 * SECOND, 25);
    CHECK(yozora_hls_segment_count(hls) == 0);
    CHECK(yozora_hls_origin(hls) == 10 * SECOND);
    /* the key frame at 11 s ends the first segment, but the audio stops at 11 s minus a frame: still waiting */
    CHECK(yozora_hls_add_video(hls, 11 * SECOND, 11 * SECOND + 7200, 1, nal, sizeof nal) == YOZORA_HLS_OK);
    CHECK(yozora_hls_segment_count(hls) == 0);
    CHECK(yozora_hls_add_audio(hls, 11 * SECOND + 100, frame, sizeof frame) == YOZORA_HLS_OK);
    CHECK(yozora_hls_segment_count(hls) == 1);
    CHECK(yozora_hls_duration(hls) == SECOND);

    uint64_t total;
    yozora_hls_read_status status;
    char *text = read_all(hls, "index.m3u8", &total, &status);
    CHECK(status == YOZORA_HLS_READ_OK && total == strlen(text));
    CHECK(strstr(text, "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-PLAYLIST-TYPE:EVENT\n#EXT-X-TARGETDURATION:3\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:1.000,\nseg0.ts\n"));
    CHECK(!strstr(text, "ENDLIST"));

    static uint8_t segment[1 << 16];
    size_t got = 0;
    const char *type = NULL;
    CHECK(yozora_hls_read(hls, "seg0.ts", 0, segment, sizeof segment, &got, &total, &type) == YOZORA_HLS_READ_OK);
    CHECK(total == got && got % 188 == 0 && !strcmp(type, "video/mp2t"));
    /* the tables, 25 video frames (one packet each, they are tiny) and the audio frames of that second */
    CHECK(count_packets_with_pid(segment, got, 0) == 1 && count_packets_with_pid(segment, got, YOZORA_TS_PID_PMT) == 1);
    CHECK(count_packets_with_pid(segment, got, YOZORA_TS_PID_VIDEO) == 25);
    int audio = count_packets_with_pid(segment, got, YOZORA_TS_PID_AUDIO);
    CHECK(audio >= 42 && audio <= 44);
    /* the first packet after the tables is video: its time is the offset, 0 from the origin */
    CHECK(segment[2 * 188] == 0x47 && (((segment[2 * 188 + 1] & 0x1f) << 8) | segment[2 * 188 + 2]) == YOZORA_TS_PID_VIDEO);

    /* a range inside a segment, and past its end */
    CHECK(yozora_hls_read(hls, "seg0.ts", 188, segment, 188, &got, &total, &type) == YOZORA_HLS_READ_OK && got == 188 && segment[0] == 0x47);
    CHECK(yozora_hls_read(hls, "seg0.ts", total, segment, 188, &got, &total, &type) == YOZORA_HLS_READ_RANGE);
    /* names that are not segments of the stream */
    CHECK(yozora_hls_read(hls, "seg1.ts", 0, segment, 188, &got, &total, &type) == YOZORA_HLS_READ_NOT_FOUND);
    CHECK(yozora_hls_read(hls, "seg00.ts", 0, segment, 188, &got, &total, &type) == YOZORA_HLS_READ_NOT_FOUND);
    CHECK(yozora_hls_read(hls, "seg-1.ts", 0, segment, 188, &got, &total, &type) == YOZORA_HLS_READ_NOT_FOUND);
    CHECK(yozora_hls_read(hls, "seg0.ts/../x", 0, segment, 188, &got, &total, &type) == YOZORA_HLS_READ_NOT_FOUND);
    CHECK(yozora_hls_read(hls, "seg99999999999999999999.ts", 0, segment, 188, &got, &total, &type) == YOZORA_HLS_READ_NOT_FOUND);
    CHECK(yozora_hls_read(hls, "", 0, segment, 188, &got, &total, &type) == YOZORA_HLS_READ_NOT_FOUND);

    /* more seconds, then the end of the stream: the samples that were waiting are cut with an estimated end, the playlist closes */
    feed(hls, 1, 1, 11 * SECOND + 3600, 14 * SECOND, 25);
    unsigned before = yozora_hls_segment_count(hls);
    CHECK(before >= 2);
    CHECK(yozora_hls_end(hls) == YOZORA_HLS_OK);
    CHECK(yozora_hls_ended(hls) && yozora_hls_segment_count(hls) > before);
    text = read_all(hls, "index.m3u8", &total, &status);
    CHECK(strstr(text, "#EXT-X-ENDLIST\n"));
    CHECK(yozora_hls_add_video(hls, 20 * SECOND, 20 * SECOND, 1, nal, sizeof nal) == YOZORA_HLS_ENDED);
    CHECK(yozora_hls_end(hls) == YOZORA_HLS_OK);
    yozora_hls_release(hls);
}

static void ordering(void)
{
    yozora_hls *hls = make(1, 1);
    /* samples before the first key frame are dropped, audio before it too */
    CHECK(yozora_hls_add_audio(hls, 500, frame, sizeof frame) == YOZORA_HLS_OK);
    CHECK(yozora_hls_add_video(hls, 1000, 1000, 0, nal, sizeof nal) == YOZORA_HLS_OK);
    CHECK(yozora_hls_origin(hls) == 0);
    CHECK(yozora_hls_add_video(hls, 3000, 3000, 1, nal, sizeof nal) == YOZORA_HLS_OK);
    CHECK(yozora_hls_origin(hls) == 3000);
    CHECK(yozora_hls_add_audio(hls, 3500, frame, sizeof frame) == YOZORA_HLS_OK);
    /* decode order cannot go back, a presentation time cannot be before its decode time, audio time cannot go back */
    CHECK(yozora_hls_add_video(hls, 2000, 2000, 0, nal, sizeof nal) == YOZORA_HLS_BAD_SAMPLE);
    CHECK(yozora_hls_add_video(hls, 4000, 3900, 0, nal, sizeof nal) == YOZORA_HLS_BAD_SAMPLE);
    CHECK(yozora_hls_add_audio(hls, 3000, frame, sizeof frame) == YOZORA_HLS_BAD_SAMPLE);
    /* an audio sample before the start of the epoch is ignored without an error */
    CHECK(yozora_hls_add_audio(hls, 3500, frame, sizeof frame) == YOZORA_HLS_OK);
    yozora_hls_release(hls);

    /* audio first, then the key frame that cuts its early part */
    hls = make(1, 1);
    for (int i = 0; i < 4; ++i)
        CHECK(yozora_hls_add_audio(hls, 1000 + i * 2090, frame, sizeof frame) == YOZORA_HLS_OK);
    CHECK(yozora_hls_add_video(hls, 5000, 5000, 1, nal, sizeof nal) == YOZORA_HLS_OK);
    CHECK(yozora_hls_origin(hls) == 5000);
    yozora_hls_release(hls);
}

static void configuring(void)
{
    yozora_hls *hls = NULL;
    CHECK(yozora_hls_create(0, 0, &hls) == YOZORA_HLS_BAD_ARGUMENT);
    CHECK(yozora_hls_create(1, 1, NULL) == YOZORA_HLS_BAD_ARGUMENT);
    CHECK(yozora_hls_create(1, 0, &hls) == YOZORA_HLS_OK);
    /* no segment before the config of the video, a config for a track that is not wanted, a config twice, a broken config */
    CHECK(yozora_hls_add_audio(hls, 0, frame, sizeof frame) == YOZORA_HLS_BAD_CONFIG);
    CHECK(yozora_hls_set_audio(hls, asc, sizeof asc) == YOZORA_HLS_BAD_CONFIG);
    CHECK(yozora_hls_set_video(hls, avcc, 3) == YOZORA_HLS_BAD_CONFIG);
    CHECK(yozora_hls_add_video(hls, 0, 0, 1, nal, sizeof nal) == YOZORA_HLS_OK);
    CHECK(yozora_hls_add_video(hls, 3600, 3600, 1, nal, sizeof nal) == YOZORA_HLS_OK);
    CHECK(yozora_hls_segment_count(hls) == 0);
    CHECK(yozora_hls_set_video(hls, avcc, sizeof avcc) == YOZORA_HLS_OK);
    CHECK(yozora_hls_set_video(hls, avcc, sizeof avcc) == YOZORA_HLS_BAD_CONFIG);
    CHECK(yozora_hls_add_video(hls, 3601, 3601, 1, nal, sizeof nal) == YOZORA_HLS_OK);
    /* a key frame every 3600 units is closer than the minimum segment: nothing is cut yet */
    CHECK(yozora_hls_segment_count(hls) == 0);
    for (int i = 0; i < 20; ++i)
        CHECK(yozora_hls_add_video(hls, 7200 + i * 3600, 7200 + i * 3600, 1, nal, sizeof nal) == YOZORA_HLS_OK);
    CHECK(yozora_hls_segment_count(hls) >= 1);
    /* a NAL unit that does not add up is refused by the muxer when the segment is cut */
    static const uint8_t broken[] = { 0, 0, 0, 9, 0x65 };
    CHECK(yozora_hls_add_video(hls, 900000, 900000, 1, broken, sizeof broken) == YOZORA_HLS_OK);
    CHECK(yozora_hls_add_video(hls, 900000 + 5 * SECOND, 900000 + 5 * SECOND, 1, nal, sizeof nal) == YOZORA_HLS_BAD_SAMPLE);
    CHECK(yozora_hls_add_video(NULL, 0, 0, 1, nal, sizeof nal) == YOZORA_HLS_BAD_ARGUMENT);
    CHECK(yozora_hls_add_audio(hls, 0, frame, 0) == YOZORA_HLS_BAD_ARGUMENT);
    yozora_hls_release(hls);
}

static void audio_only(void)
{
    yozora_hls *hls = make(0, 1);
    /* four seconds of audio: segments of two seconds are cut as the audio reaches past them */
    for (int64_t t = 0; t < 5 * SECOND; t += 2090)
        CHECK(yozora_hls_add_audio(hls, t, frame, sizeof frame) == YOZORA_HLS_OK);
    CHECK(yozora_hls_segment_count(hls) == 2);
    /* the cuts fall on audio frames: the first frame past 2 s is number 87 (181830), past 4 s from there number 174 (363660) */
    CHECK(yozora_hls_origin(hls) == 0 && yozora_hls_duration(hls) == 174 * 2090);
    CHECK(yozora_hls_end(hls) == YOZORA_HLS_OK);
    CHECK(yozora_hls_segment_count(hls) == 3);
    uint64_t total;
    yozora_hls_read_status status;
    char *text = read_all(hls, "index.m3u8", &total, &status);
    CHECK(strstr(text, "#EXTINF:2.020,\nseg0.ts\n#EXTINF:2.020,\nseg1.ts\n#EXTINF:"));
    yozora_hls_release(hls);
}

static void trimming(void)
{
    yozora_hls *hls = make(1, 0);
    feed(hls, 1, 0, 0, 6 * SECOND, 25);
    CHECK(yozora_hls_end(hls) == YOZORA_HLS_OK);
    unsigned count = yozora_hls_segment_count(hls);
    CHECK(count == 6);
    size_t before = yozora_hls_memory(hls);
    CHECK(before > 0);
    yozora_hls_trim(hls, 3 * SECOND);
    CHECK(yozora_hls_memory(hls) < before);
    static uint8_t buffer[1 << 16];
    size_t got;
    uint64_t total;
    const char *type;
    /* the segments before 3 s are gone, the playlist still names them */
    CHECK(yozora_hls_read(hls, "seg0.ts", 0, buffer, sizeof buffer, &got, &total, &type) == YOZORA_HLS_READ_NOT_FOUND);
    CHECK(yozora_hls_read(hls, "seg2.ts", 0, buffer, sizeof buffer, &got, &total, &type) == YOZORA_HLS_READ_NOT_FOUND);
    CHECK(yozora_hls_read(hls, "seg3.ts", 0, buffer, sizeof buffer, &got, &total, &type) == YOZORA_HLS_READ_OK);
    yozora_hls_read_status status;
    char *text = read_all(hls, "index.m3u8", &total, &status);
    CHECK(strstr(text, "seg0.ts") && strstr(text, "seg5.ts"));
    /* the last segment is kept whatever the time */
    yozora_hls_trim(hls, 1000 * SECOND);
    CHECK(yozora_hls_read(hls, "seg5.ts", 0, buffer, sizeof buffer, &got, &total, &type) == YOZORA_HLS_READ_OK);
    yozora_hls_release(hls);
}

static void references(void)
{
    yozora_hls *hls = make(1, 0);
    yozora_hls_retain(hls);
    yozora_hls_release(hls);
    CHECK(yozora_hls_segment_count(hls) == 0);
    yozora_hls_release(hls);
    yozora_hls_release(NULL);
    yozora_hls_retain(NULL);
}

int main(void)
{
    segmenting();
    ordering();
    configuring();
    audio_only();
    trimming();
    references();
    if (failures) {
        fprintf(stderr, "test_hls: %d failures\n", failures);
        return 1;
    }
    puts("test_hls: ok");
    return 0;
}
