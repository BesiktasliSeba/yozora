// decodeAudioData and the panner HRTF database: a clip is decoded to planar float at the file rate, then
// WebKit's converter brings it to the context rate. a format no decoder takes ends in a null bus, which the
// page sees as an EncodingError

#include "config.h"

#if ENABLE(WEB_AUDIO)

#include "AudioBus.h"
#include "AudioChannel.h"
#include "AudioFileReader.h"
#include "YozoraAudioDecode.h"
#include <cstdio>
#include <cstring>
#include <wtf/NeverDestroyed.h>

// made at configure time from platform/audio/resources/Composite.wav of the WebCore sources (PlatformYozora.cmake)
static const unsigned char hrtfComposite[] = {
#include "YozoraHRTFComposite.inc"
};

namespace WebCore {

RefPtr<AudioBus> createBusFromInMemoryAudioFile(std::span<const uint8_t> data, bool mixToMono, float sampleRate)
{
    Yozora::DecodedAudio decoded;
    auto status = Yozora::decodeWave(data, decoded);
#if defined(__APPLE__)
    // a file that is not a wave goes to the system decoders
    if (status == Yozora::DecodeStatus::Unsupported)
        status = Yozora::decodeWithAudioToolbox(data, decoded);
#endif
    if (status != Yozora::DecodeStatus::Ok) {
        if (status != Yozora::DecodeStatus::Unsupported)
            std::fprintf(stderr, "yozora: audio file refused (%s)\n", status == Yozora::DecodeStatus::TooLarge ? "too large" : "malformed");
        return nullptr;
    }

    size_t length = decoded.channels[0].size();
    auto bus = AudioBus::create(decoded.channels.size(), length);
    bus->setSampleRate(decoded.sampleRate);
    for (unsigned channel = 0; channel < decoded.channels.size(); ++channel)
        std::memcpy(bus->channel(channel)->mutableSpan().data(), decoded.channels[channel].data(), length * sizeof(float));
    // the decoded copy is gone before the converter makes its own
    decoded = { };

    if (sampleRate && sampleRate != bus->sampleRate())
        return AudioBus::createBySampleRateConverting(bus.get(), mixToMono, sampleRate);
    if (mixToMono && bus->numberOfChannels() > 1)
        return AudioBus::createByMixingToMono(bus.get());
    return RefPtr<AudioBus> { WTF::move(bus) };
}

// the panner asks for one impulse response at a time by a name such as IRC_Composite_C_R0195_T015_P000
// (azimuth 15, elevation 0), the Composite.wav of the WebCore sources holds all 240, 256 frames each, by
// increasing azimuth and per azimuth in the order of the elevation table
RefPtr<AudioBus> AudioBus::loadPlatformResource(const char* name, float sampleRate)
{
    constexpr unsigned responseFrames = 256;
    constexpr unsigned azimuthSpacing = 15;
    constexpr unsigned responses = 240;
    constexpr int elevations[] = { 0, 15, 30, 45, 60, 75, 90, 315, 330, 345 };
    constexpr unsigned elevationCount = sizeof(elevations) / sizeof(elevations[0]);

    int azimuth = 0, elevation = 0;
    char tail = 0;
    if (std::sscanf(name, "IRC_Composite_C_R0195_T%3d_P%3d%c", &azimuth, &elevation, &tail) != 2 || azimuth < 0 || azimuth > 345 || azimuth % azimuthSpacing) {
        std::fprintf(stderr, "yozora: no audio resource named %s\n", name);
        return nullptr;
    }
    unsigned elevationIndex = elevationCount;
    for (unsigned i = 0; i < elevationCount; ++i) {
        if (elevations[i] == elevation)
            elevationIndex = i;
    }
    unsigned index = (azimuth / azimuthSpacing) * elevationCount + elevationIndex;
    if (elevationIndex == elevationCount || index >= responses) {
        std::fprintf(stderr, "yozora: no audio resource named %s\n", name);
        return nullptr;
    }

    // the whole wave is decoded once at its recorded rate (the bus is only read after this, so threads that
    // ask for a response share it without touching its count)
    static NeverDestroyed<RefPtr<AudioBus>> composite { createBusFromInMemoryAudioFile(std::span<const uint8_t> { hrtfComposite, sizeof(hrtfComposite) }, false, 0) };
    if (!composite.get() || composite.get()->length() != static_cast<size_t>(responses) * responseFrames || composite.get()->numberOfChannels() != 2) {
        std::fprintf(stderr, "yozora: the HRTF database is not what the panner expects\n");
        return nullptr;
    }
    auto response = AudioBus::createBufferFromRange(*composite.get(), index * responseFrames, (index + 1) * responseFrames);
    if (!response)
        return nullptr;
    if (sampleRate && sampleRate != response->sampleRate())
        return AudioBus::createBySampleRateConverting(*response, false, sampleRate);
    return response;
}

} // namespace WebCore

#endif // ENABLE(WEB_AUDIO)
