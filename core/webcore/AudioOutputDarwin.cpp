// the sound of a page on the phone: an AudioQueue of 16 bit samples, three buffers of one period. AudioQueue
// is in AudioToolbox since ios 2 and plays at the context rate whatever the hardware runs at, the system
// converter resamples

#include "YozoraAudioOutput.h"

#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace Yozora {

namespace {

constexpr unsigned bufferCount = 3;

class QueueOutput final : public AudioOutput {
public:
    QueueOutput(unsigned channels, float sampleRate, Fill fill, void* context)
        : m_channels(channels), m_sampleRate(sampleRate), m_fill(fill), m_context(context), m_scratch(static_cast<size_t>(channels) * audioPeriodFrames) { }

    ~QueueOutput() override
    {
        stop();
        if (m_queue)
            AudioQueueDispose(m_queue, true);
    }

    bool open()
    {
        AudioStreamBasicDescription format = { };
        format.mSampleRate = m_sampleRate;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        format.mChannelsPerFrame = m_channels;
        format.mBitsPerChannel = 16;
        format.mFramesPerPacket = 1;
        format.mBytesPerFrame = 2 * m_channels;
        format.mBytesPerPacket = format.mBytesPerFrame;
        // a null run loop makes the queue call back on a thread of its own
        OSStatus status = AudioQueueNewOutput(&format, callback, this, nullptr, nullptr, 0, &m_queue);
        if (status != noErr) {
            std::fprintf(stderr, "yozora: AudioQueueNewOutput failed: %d\n", static_cast<int>(status));
            m_queue = nullptr;
            return false;
        }
        for (unsigned i = 0; i < bufferCount; ++i) {
            status = AudioQueueAllocateBuffer(m_queue, format.mBytesPerFrame * audioPeriodFrames, &m_buffers[i]);
            if (status != noErr) {
                std::fprintf(stderr, "yozora: AudioQueueAllocateBuffer failed: %d\n", static_cast<int>(status));
                return false;
            }
        }
        return true;
    }

    bool start() override
    {
        if (m_running)
            return true;
        // the queue starts with every buffer full, an empty one would play a gap
        for (unsigned i = 0; i < bufferCount; ++i)
            fillAndEnqueue(m_buffers[i]);
        OSStatus status = AudioQueueStart(m_queue, nullptr);
        if (status != noErr) {
            std::fprintf(stderr, "yozora: AudioQueueStart failed: %d\n", static_cast<int>(status));
            AudioQueueStop(m_queue, true);
            return false;
        }
        m_running = true;
        return true;
    }

    void stop() override
    {
        if (!m_running)
            return;
        m_running = false;
        // immediate: the callbacks are over when this returns, so the destination can go away after it
        AudioQueueStop(m_queue, true);
    }

private:
    static void callback(void* context, AudioQueueRef, AudioQueueBufferRef buffer)
    {
        auto& output = *static_cast<QueueOutput*>(context);
        if (!output.m_running)
            return;
        output.fillAndEnqueue(buffer);
    }

    void fillAndEnqueue(AudioQueueBufferRef buffer)
    {
        m_fill(m_context, m_scratch.data(), audioPeriodFrames);
        auto* samples = static_cast<int16_t*>(buffer->mAudioData);
        size_t count = m_scratch.size();
        for (size_t i = 0; i < count; ++i) {
            float value = std::clamp(m_scratch[i], -1.0f, 1.0f);
            samples[i] = static_cast<int16_t>(value * 32767.0f);
        }
        buffer->mAudioDataByteSize = static_cast<UInt32>(count * sizeof(int16_t));
        OSStatus status = AudioQueueEnqueueBuffer(m_queue, buffer, 0, nullptr);
        if (status != noErr)
            std::fprintf(stderr, "yozora: AudioQueueEnqueueBuffer failed: %d\n", static_cast<int>(status));
    }

    unsigned m_channels;
    float m_sampleRate;
    Fill m_fill;
    void* m_context;
    std::vector<float> m_scratch;
    AudioQueueRef m_queue { nullptr };
    AudioQueueBufferRef m_buffers[bufferCount] { };
    volatile bool m_running { false };
};

}

std::unique_ptr<AudioOutput> AudioOutput::create(unsigned channels, float sampleRate, Fill fill, void* context)
{
    if (!channels || channels > 2 || !(sampleRate >= 8000 && sampleRate <= 192000) || !fill)
        return nullptr;
    auto output = std::make_unique<QueueOutput>(channels, sampleRate, fill, context);
    if (!output->open())
        return nullptr;
    return output;
}

float AudioOutput::hardwareSampleRate()
{
    return 44100;
}

}
