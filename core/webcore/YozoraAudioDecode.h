#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Yozora {

// the most samples (all channels together) a decoded clip may hold: 64 MB of floats, what a 512 MB phone can
// keep next to a page
constexpr size_t maxDecodedSamples = 16 * 1024 * 1024;

struct DecodedAudio {
    float sampleRate { 0 };
    // one vector of samples for each channel, all of the same length
    std::vector<std::vector<float>> channels;
};

enum class DecodeStatus { Ok, Malformed, Unsupported, TooLarge };

// RIFF WAVE with integer PCM of 8, 16, 24 and 32 bits and float of 32 and 64 bits, also the extensible
// header. every size read from the file is checked against the bytes there are before use
DecodeStatus decodeWave(std::span<const uint8_t> data, DecodedAudio& out);

#if defined(__APPLE__)
// mp3, aac, m4a, caf and aiff through AudioToolbox, which has them on every ios the app runs on
DecodeStatus decodeWithAudioToolbox(std::span<const uint8_t> data, DecodedAudio& out);
#endif

}
