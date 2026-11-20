#include "YozoraAudioDecode.h"

#include <cmath>
#include <cstring>

namespace Yozora {

namespace {

uint32_t read32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t read16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

enum : uint16_t { formatPCM = 1, formatFloat = 3, formatExtensible = 0xfffe };

}

DecodeStatus decodeWave(std::span<const uint8_t> data, DecodedAudio& out)
{
    out = { };
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) || std::memcmp(data.data() + 8, "WAVE", 4))
        return DecodeStatus::Unsupported;

    uint16_t format = 0, channels = 0, bits = 0, blockAlign = 0;
    uint32_t sampleRate = 0;
    bool haveFormat = false;
    const uint8_t* samples = nullptr;
    size_t sampleBytes = 0;
    size_t position = 12;
    // the chunks come one after another, a file with more than 64 of them is not an audio file this decoder wants
    for (unsigned chunks = 0; position + 8 <= data.size() && chunks < 64; ++chunks) {
        const uint8_t* header = data.data() + position;
        uint32_t size = read32(header + 4);
        size_t body = position + 8;
        size_t available = data.size() - body;
        if (!std::memcmp(header, "fmt ", 4)) {
            if (size < 16 || size > available)
                return DecodeStatus::Malformed;
            format = read16(data.data() + body);
            channels = read16(data.data() + body + 2);
            sampleRate = read32(data.data() + body + 4);
            blockAlign = read16(data.data() + body + 12);
            bits = read16(data.data() + body + 14);
            // the extensible header names the real format by the first two bytes of its sub format GUID
            if (format == formatExtensible) {
                if (size < 40)
                    return DecodeStatus::Malformed;
                format = read16(data.data() + body + 24);
            }
            haveFormat = true;
        } else if (!std::memcmp(header, "data", 4)) {
            samples = data.data() + body;
            // a streamed file can state a size larger than the file: the bytes that are there count
            sampleBytes = std::min<size_t>(size, available);
        }
        // chunks are padded to an even size
        size_t next = body + static_cast<size_t>(size) + (size & 1);
        if (next < body || next > data.size())
            break;
        position = next;
    }
    if (!haveFormat || !samples)
        return DecodeStatus::Malformed;
    if (format != formatPCM && format != formatFloat)
        return DecodeStatus::Unsupported;
    if (!channels || channels > 32 || sampleRate < 3000 || sampleRate > 768000)
        return DecodeStatus::Malformed;
    unsigned bytesPerSample = bits / 8;
    bool known = format == formatPCM ? (bits == 8 || bits == 16 || bits == 24 || bits == 32) : (bits == 32 || bits == 64);
    if (!known)
        return DecodeStatus::Unsupported;
    if (blockAlign != channels * bytesPerSample)
        return DecodeStatus::Malformed;

    size_t frames = sampleBytes / blockAlign;
    if (!frames)
        return DecodeStatus::Malformed;
    if (frames > maxDecodedSamples / channels)
        return DecodeStatus::TooLarge;

    out.sampleRate = static_cast<float>(sampleRate);
    out.channels.assign(channels, std::vector<float>(frames));
    for (size_t frame = 0; frame < frames; ++frame) {
        const uint8_t* at = samples + frame * blockAlign;
        for (unsigned channel = 0; channel < channels; ++channel, at += bytesPerSample) {
            float value;
            if (format == formatFloat) {
                if (bits == 32) {
                    uint32_t raw = read32(at);
                    std::memcpy(&value, &raw, 4);
                } else {
                    uint64_t raw = static_cast<uint64_t>(read32(at)) | (static_cast<uint64_t>(read32(at + 4)) << 32);
                    double wide;
                    std::memcpy(&wide, &raw, 8);
                    value = static_cast<float>(wide);
                }
                // a file can hold NaN or infinity, which would poison every node after it
                if (!std::isfinite(value))
                    value = 0;
            } else if (bits == 8)
                value = (static_cast<int>(at[0]) - 128) / 128.0f;
            else if (bits == 16)
                value = static_cast<int16_t>(read16(at)) / 32768.0f;
            else if (bits == 24) {
                int32_t raw = (at[0] << 8) | (at[1] << 16) | (static_cast<int32_t>(at[2]) << 24);
                value = raw / 2147483648.0f;
            } else
                value = static_cast<int32_t>(read32(at)) / 2147483648.0f;
            out.channels[channel][frame] = value;
        }
    }
    return DecodeStatus::Ok;
}

}
