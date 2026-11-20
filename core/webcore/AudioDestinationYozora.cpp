// the Web Audio destination: the graph renders into the FIFO of AudioDestinationResampler and the sound
// device thread pulls from it. the device runs at the context rate, so the base class resampler is not used

#include "config.h"

#if ENABLE(WEB_AUDIO)

#include "AudioBus.h"
#include "AudioDestinationResampler.h"
#include "AudioUtilities.h"
#include "YozoraAudioOutput.h"
#include <algorithm>
#include <wtf/MonotonicTime.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/Vector.h>

namespace WebCore {

namespace {

constexpr unsigned long maxOutputChannels = 2;

// the render thread holds a reference while it works, and the sound device is stopped on the main thread that made it
class AudioDestinationYozora final : public AudioDestinationResampler, public ThreadSafeRefCounted<AudioDestinationYozora, WTF::DestructionThread::Main> {
public:
    void ref() const final { ThreadSafeRefCounted::ref(); }
    void deref() const final { ThreadSafeRefCounted::deref(); }

    explicit AudioDestinationYozora(const AudioDestination::CreationOptions& options)
        : AudioDestinationResampler(options, options.sampleRate)
        , m_channels(options.numberOfOutputChannels)
        , m_sampleRate(options.sampleRate)
    {
        // the output bus has no memory of its own: the device thread points it at a quantum of planar samples before each pull
        m_planar.resize(static_cast<size_t>(m_channels) * AudioUtilities::renderQuantumSize);
    }

    ~AudioDestinationYozora() override
    {
        if (m_output)
            m_output->stop();
    }

private:
    void startRendering(CompletionHandler<void(bool)>&& completion) final
    {
        if (!m_output)
            m_output = Yozora::AudioOutput::create(m_channels, m_sampleRate, &AudioDestinationYozora::fillFromDevice, this);
        bool started = m_output && m_output->start();
        setIsPlaying(started);
        completion(started);
    }

    void stopRendering(CompletionHandler<void(bool)>&& completion) final
    {
        if (m_output)
            m_output->stop();
        setIsPlaying(false);
        completion(true);
    }

    static void fillFromDevice(void* context, float* interleaved, unsigned frames)
    {
        static_cast<AudioDestinationYozora*>(context)->fill(interleaved, frames);
    }

    // on the device thread: the graph renders as many quanta as the device asks for, then each quantum is
    // pulled from the FIFO and interleaved. a period the graph could not finish in time is the silence
    // pullRendered adds
    void fill(float* interleaved, unsigned frames)
    {
        ASSERT(!(frames % AudioUtilities::renderQuantumSize));
        render(m_framesPlayed / m_sampleRate, MonotonicTime::now(), frames);
        for (unsigned offset = 0; offset < frames; offset += AudioUtilities::renderQuantumSize) {
            for (unsigned channel = 0; channel < m_channels; ++channel)
                m_outputBus->setChannelMemory(channel, std::span<float> { m_planar.mutableSpan().subspan(channel * AudioUtilities::renderQuantumSize, AudioUtilities::renderQuantumSize) });
            pullRendered(AudioUtilities::renderQuantumSize);
            for (unsigned frame = 0; frame < AudioUtilities::renderQuantumSize; ++frame) {
                for (unsigned channel = 0; channel < m_channels; ++channel)
                    interleaved[(offset + frame) * m_channels + channel] = m_planar[channel * AudioUtilities::renderQuantumSize + frame];
            }
        }
        m_framesPlayed += frames;
    }

    unsigned m_channels;
    float m_sampleRate;
    Vector<float> m_planar;
    std::unique_ptr<Yozora::AudioOutput> m_output;
    double m_framesPlayed { 0 };
};

} // namespace

Ref<AudioDestination> AudioDestination::create(const CreationOptions& options)
{
    return adoptRef(*new AudioDestinationYozora(options));
}

float AudioDestination::hardwareSampleRate()
{
    return Yozora::AudioOutput::hardwareSampleRate();
}

unsigned long AudioDestination::maxChannelCount()
{
    return maxOutputChannels;
}

} // namespace WebCore

#endif // ENABLE(WEB_AUDIO)
