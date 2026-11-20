#include "yozora_ts.h"

#include <stdlib.h>
#include <string.h>

static uint32_t crc32_mpeg(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= (uint32_t)data[i] << 24;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04c11db7u : crc << 1;
    }
    return crc;
}

int64_t yozora_ts_scale(int64_t value, uint32_t timescale)
{
    if (!timescale)
        return 0;
    /* split so the product stays in 64 bits: whole seconds, then the remainder */
    int64_t seconds = value / (int64_t)timescale;
    int64_t rest = value % (int64_t)timescale;
    return seconds * 90000 + (rest * 90000 + (rest >= 0 ? (int64_t)timescale / 2 : -(int64_t)timescale / 2)) / (int64_t)timescale;
}

yozora_ts_status yozora_ts_init(yozora_ts_mux *mux, const uint8_t *avcc, size_t avcc_size, const uint8_t *asc, size_t asc_size, yozora_ts_sink sink, void *context)
{
    if (!mux || !sink || (!avcc_size && !asc_size) || (avcc_size && !avcc) || (asc_size && !asc))
        return YOZORA_TS_BAD_ARGUMENT;
    memset(mux, 0, sizeof *mux);
    mux->sink = sink;
    mux->context = context;
    if (avcc_size) {
        if (avcc_size < 7 || avcc[0] != 1)
            return YOZORA_TS_BAD_CONFIG;
        mux->nal_length_size = (avcc[4] & 3u) + 1;
        size_t at = 5;
        unsigned sets = avcc[at++] & 0x1f;
        for (int pass = 0; pass < 2; ++pass) {
            for (unsigned i = 0; i < sets; ++i) {
                if (at + 2 > avcc_size)
                    return YOZORA_TS_BAD_CONFIG;
                size_t length = ((size_t)avcc[at] << 8) | avcc[at + 1];
                at += 2;
                if (!length || at + length > avcc_size || mux->parameter_sets_size + 4 + length > sizeof mux->parameter_sets)
                    return YOZORA_TS_BAD_CONFIG;
                static const uint8_t start_code[4] = { 0, 0, 0, 1 };
                memcpy(mux->parameter_sets + mux->parameter_sets_size, start_code, 4);
                memcpy(mux->parameter_sets + mux->parameter_sets_size + 4, avcc + at, length);
                mux->parameter_sets_size += 4 + length;
                at += length;
            }
            if (pass == 0) {
                if (at >= avcc_size)
                    return YOZORA_TS_BAD_CONFIG;
                sets = avcc[at++];
            }
        }
        if (!mux->parameter_sets_size)
            return YOZORA_TS_BAD_CONFIG;
        mux->has_video = 1;
    }
    if (asc_size) {
        if (asc_size < 2)
            return YOZORA_TS_BAD_CONFIG;
        unsigned object_type = asc[0] >> 3;
        unsigned frequency = ((asc[0] & 7u) << 1) | (asc[1] >> 7);
        unsigned channels = (asc[1] >> 3) & 15u;
        /* object types 1 to 4 are AAC main, LC, SSR and LTP, which ADTS names with two bits. 5 and 29 announce SBR and PS */
        if (object_type < 1 || object_type > 4 || frequency >= 13 || channels < 1 || channels > 7)
            return YOZORA_TS_BAD_CONFIG;
        mux->aac_profile = object_type - 1;
        mux->aac_frequency_index = frequency;
        mux->aac_channels = channels;
        mux->has_audio = 1;
    }
    return YOZORA_TS_OK;
}

static yozora_ts_status emit(yozora_ts_mux *mux, const uint8_t packet[YOZORA_TS_PACKET])
{
    return mux->sink(mux->context, packet) ? YOZORA_TS_ABORTED : YOZORA_TS_OK;
}

static yozora_ts_status write_section(yozora_ts_mux *mux, unsigned pid, uint8_t *counter, const uint8_t *section, size_t length)
{
    uint8_t packet[YOZORA_TS_PACKET];
    memset(packet, 0xff, sizeof packet);
    packet[0] = 0x47;
    packet[1] = (uint8_t)(0x40 | (pid >> 8));
    packet[2] = (uint8_t)pid;
    packet[3] = (uint8_t)(0x10 | (*counter & 15u));
    *counter = (uint8_t)((*counter + 1) & 15u);
    packet[4] = 0;
    memcpy(packet + 5, section, length);
    return emit(mux, packet);
}

yozora_ts_status yozora_ts_write_tables(yozora_ts_mux *mux)
{
    if (!mux || !mux->sink)
        return YOZORA_TS_BAD_ARGUMENT;
    uint8_t section[64];
    size_t n = 0;
    section[n++] = 0x00;
    section[n++] = 0xb0;
    section[n++] = 13;
    section[n++] = 0x00;
    section[n++] = 0x01;
    section[n++] = 0xc1;
    section[n++] = 0x00;
    section[n++] = 0x00;
    section[n++] = 0x00;
    section[n++] = 0x01;
    section[n++] = (uint8_t)(0xe0 | (YOZORA_TS_PID_PMT >> 8));
    section[n++] = (uint8_t)YOZORA_TS_PID_PMT;
    uint32_t crc = crc32_mpeg(section, n);
    section[n++] = (uint8_t)(crc >> 24);
    section[n++] = (uint8_t)(crc >> 16);
    section[n++] = (uint8_t)(crc >> 8);
    section[n++] = (uint8_t)crc;
    yozora_ts_status status = write_section(mux, 0, &mux->cc_pat, section, n);
    if (status != YOZORA_TS_OK)
        return status;

    unsigned pcr_pid = mux->has_video ? YOZORA_TS_PID_VIDEO : YOZORA_TS_PID_AUDIO;
    unsigned streams = (mux->has_video ? 1u : 0u) + (mux->has_audio ? 1u : 0u);
    n = 0;
    section[n++] = 0x02;
    section[n++] = 0xb0;
    section[n++] = (uint8_t)(13 + 5 * streams);
    section[n++] = 0x00;
    section[n++] = 0x01;
    section[n++] = 0xc1;
    section[n++] = 0x00;
    section[n++] = 0x00;
    section[n++] = (uint8_t)(0xe0 | (pcr_pid >> 8));
    section[n++] = (uint8_t)pcr_pid;
    section[n++] = 0xf0;
    section[n++] = 0x00;
    if (mux->has_video) {
        section[n++] = 0x1b;
        section[n++] = (uint8_t)(0xe0 | (YOZORA_TS_PID_VIDEO >> 8));
        section[n++] = (uint8_t)YOZORA_TS_PID_VIDEO;
        section[n++] = 0xf0;
        section[n++] = 0x00;
    }
    if (mux->has_audio) {
        section[n++] = 0x0f;
        section[n++] = (uint8_t)(0xe0 | (YOZORA_TS_PID_AUDIO >> 8));
        section[n++] = (uint8_t)YOZORA_TS_PID_AUDIO;
        section[n++] = 0xf0;
        section[n++] = 0x00;
    }
    crc = crc32_mpeg(section, n);
    section[n++] = (uint8_t)(crc >> 24);
    section[n++] = (uint8_t)(crc >> 16);
    section[n++] = (uint8_t)(crc >> 8);
    section[n++] = (uint8_t)crc;
    return write_section(mux, YOZORA_TS_PID_PMT, &mux->cc_pmt, section, n);
}

static void put_timestamp(uint8_t *out, unsigned marker, int64_t value)
{
    uint64_t v = (uint64_t)value & 0x1ffffffffull;
    out[0] = (uint8_t)((marker << 4) | (((v >> 30) & 7u) << 1) | 1u);
    out[1] = (uint8_t)(v >> 22);
    out[2] = (uint8_t)((((v >> 15) & 0x7fu) << 1) | 1u);
    out[3] = (uint8_t)(v >> 7);
    out[4] = (uint8_t)(((v & 0x7fu) << 1) | 1u);
}

/* a PES packet over as many transport packets as it takes. the first carries the program clock when it is the
   clock's, the last is padded with an adaptation field of stuffing bytes to fill 188 bytes */
static yozora_ts_status write_pes(yozora_ts_mux *mux, unsigned pid, uint8_t *counter, unsigned stream_id, int64_t pts, int64_t dts, int with_dts, int random_access,
    int with_pcr, const uint8_t *payload, size_t payload_size, int bounded)
{
    uint8_t header[19];
    size_t header_size = 0;
    size_t optional = with_dts ? 10 : 5;
    size_t pes_length = 3 + optional + payload_size;
    header[header_size++] = 0;
    header[header_size++] = 0;
    header[header_size++] = 1;
    header[header_size++] = (uint8_t)stream_id;
    if (bounded && pes_length > 0xffff)
        return YOZORA_TS_BAD_SAMPLE;
    header[header_size++] = bounded ? (uint8_t)(pes_length >> 8) : 0;
    header[header_size++] = bounded ? (uint8_t)pes_length : 0;
    header[header_size++] = 0x80;
    header[header_size++] = with_dts ? 0xc0 : 0x80;
    header[header_size++] = (uint8_t)optional;
    put_timestamp(header + header_size, with_dts ? 3 : 2, pts);
    header_size += 5;
    if (with_dts) {
        put_timestamp(header + header_size, 1, dts);
        header_size += 5;
    }

    size_t header_at = 0, payload_at = 0;
    int first = 1;
    while (header_at < header_size || payload_at < payload_size) {
        uint8_t packet[YOZORA_TS_PACKET];
        size_t available = YOZORA_TS_PACKET - 4;
        size_t adaptation = 0;
        int need_flags = first && (random_access || with_pcr);
        if (need_flags)
            adaptation = 1 + 1 + (with_pcr ? 6 : 0);
        size_t remaining = (header_size - header_at) + (payload_size - payload_at);
        size_t room = available - adaptation;
        if (remaining < room) {
            /* the stuffing bytes go into the adaptation field, which is at least its length byte */
            adaptation += room - remaining;
            room = available - adaptation;
        }
        packet[0] = 0x47;
        packet[1] = (uint8_t)((first ? 0x40 : 0) | (pid >> 8));
        packet[2] = (uint8_t)pid;
        packet[3] = (uint8_t)((adaptation ? 0x30 : 0x10) | (*counter & 15u));
        *counter = (uint8_t)((*counter + 1) & 15u);
        size_t at = 4;
        if (adaptation) {
            packet[at++] = (uint8_t)(adaptation - 1);
            if (adaptation > 1) {
                uint8_t flags = 0;
                if (first && random_access)
                    flags |= 0x40;
                if (first && with_pcr)
                    flags |= 0x10;
                packet[at++] = flags;
                if (first && with_pcr) {
                    /* the clock runs a tenth of a second behind the decode time, which is what a decoder wants to have buffered */
                    int64_t clock = dts - 9000;
                    uint64_t base = clock > 0 ? (uint64_t)clock & 0x1ffffffffull : 0;
                    packet[at++] = (uint8_t)(base >> 25);
                    packet[at++] = (uint8_t)(base >> 17);
                    packet[at++] = (uint8_t)(base >> 9);
                    packet[at++] = (uint8_t)(base >> 1);
                    packet[at++] = (uint8_t)(((base & 1u) << 7) | 0x7eu);
                    packet[at++] = 0;
                }
                while (at < 4 + adaptation)
                    packet[at++] = 0xff;
            }
        }
        size_t take_header = header_size - header_at < room ? header_size - header_at : room;
        memcpy(packet + at, header + header_at, take_header);
        at += take_header;
        header_at += take_header;
        room -= take_header;
        size_t take = payload_size - payload_at < room ? payload_size - payload_at : room;
        if (take)
            memcpy(packet + at, payload + payload_at, take);
        at += take;
        payload_at += take;
        if (at != YOZORA_TS_PACKET)
            return YOZORA_TS_BAD_SAMPLE;
        yozora_ts_status status = emit(mux, packet);
        if (status != YOZORA_TS_OK)
            return status;
        first = 0;
    }
    return YOZORA_TS_OK;
}

yozora_ts_status yozora_ts_write_video(yozora_ts_mux *mux, int64_t dts, int64_t pts, int sync, const uint8_t *sample, size_t size)
{
    if (!mux || !mux->sink || !mux->has_video || (!sample && size) || size > YOZORA_TS_MAX_VIDEO_SAMPLE)
        return YOZORA_TS_BAD_ARGUMENT;
    /* first pass: the NAL units must add up to the sample, and the output size is known */
    static const uint8_t access_unit_delimiter[6] = { 0, 0, 0, 1, 0x09, 0xf0 };
    size_t output = sizeof access_unit_delimiter + (sync ? mux->parameter_sets_size : 0);
    size_t at = 0;
    while (at < size) {
        if (size - at < mux->nal_length_size)
            return YOZORA_TS_BAD_SAMPLE;
        size_t length = 0;
        for (unsigned i = 0; i < mux->nal_length_size; ++i)
            length = (length << 8) | sample[at + i];
        at += mux->nal_length_size;
        if (!length || length > size - at)
            return YOZORA_TS_BAD_SAMPLE;
        output += 4 + length;
        at += length;
    }
    uint8_t *annexb = malloc(output);
    if (!annexb)
        return YOZORA_TS_NO_MEMORY;
    size_t n = 0;
    memcpy(annexb, access_unit_delimiter, sizeof access_unit_delimiter);
    n += sizeof access_unit_delimiter;
    if (sync) {
        memcpy(annexb + n, mux->parameter_sets, mux->parameter_sets_size);
        n += mux->parameter_sets_size;
    }
    at = 0;
    while (at < size) {
        size_t length = 0;
        for (unsigned i = 0; i < mux->nal_length_size; ++i)
            length = (length << 8) | sample[at + i];
        at += mux->nal_length_size;
        annexb[n++] = 0;
        annexb[n++] = 0;
        annexb[n++] = 0;
        annexb[n++] = 1;
        memcpy(annexb + n, sample + at, length);
        n += length;
        at += length;
    }
    yozora_ts_status status = write_pes(mux, YOZORA_TS_PID_VIDEO, &mux->cc_video, 0xe0, pts, dts, pts != dts, sync, 1, annexb, n, 0);
    free(annexb);
    return status;
}

yozora_ts_status yozora_ts_write_audio(yozora_ts_mux *mux, int64_t pts, const uint8_t *frame, size_t size)
{
    if (!mux || !mux->sink || !mux->has_audio || !frame || !size)
        return YOZORA_TS_BAD_ARGUMENT;
    size_t length = size + 7;
    if (length > 0x1fff)
        return YOZORA_TS_BAD_SAMPLE;
    uint8_t *adts = malloc(length);
    if (!adts)
        return YOZORA_TS_NO_MEMORY;
    adts[0] = 0xff;
    adts[1] = 0xf1;
    adts[2] = (uint8_t)((mux->aac_profile << 6) | (mux->aac_frequency_index << 2) | (mux->aac_channels >> 2));
    adts[3] = (uint8_t)(((mux->aac_channels & 3u) << 6) | (length >> 11));
    adts[4] = (uint8_t)(length >> 3);
    adts[5] = (uint8_t)(((length & 7u) << 5) | 0x1f);
    adts[6] = 0xfc;
    memcpy(adts + 7, frame, size);
    /* the clock is carried by the video when there is video */
    yozora_ts_status status = write_pes(mux, YOZORA_TS_PID_AUDIO, &mux->cc_audio, 0xc0, pts, pts, 0, 0, !mux->has_video, adts, length, 1);
    free(adts);
    return status;
}
