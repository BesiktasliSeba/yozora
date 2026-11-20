#pragma once

#include <memory>

namespace Yozora {

// the sound device under Web Audio: it asks for buffers from its own thread and plays what it gets. a period
// is 512 frames, four render quanta, so the queue between graph and device never gains or loses a partial
// quantum
constexpr unsigned audioPeriodFrames = 512;

class AudioOutput {
public:
    // fills `frames` interleaved frames of `channels` channels with 32 bit float samples, called on the thread of the device
    using Fill = void (*)(void* context, float* interleaved, unsigned frames);

    // null when the device cannot be opened for this format
    static std::unique_ptr<AudioOutput> create(unsigned channels, float sampleRate, Fill, void* context);
    // the rate the device runs at: the contexts are made at this rate unless the page asks for another one
    static float hardwareSampleRate();

    virtual ~AudioOutput() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
};

}
