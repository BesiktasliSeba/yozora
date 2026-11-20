// the host has no sound device the build can count on: this output runs a thread at the real pace of a device
// and drops what it gets, all the graph tests need (a context clock follows the frames the thread asks for).
// the phone uses AudioOutputDarwin.cpp

#include "YozoraAudioOutput.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <pthread.h>
#include <vector>

namespace Yozora {

namespace {

class HostOutput final : public AudioOutput {
public:
    HostOutput(unsigned channels, float sampleRate, Fill fill, void* context)
        : m_channels(channels), m_sampleRate(sampleRate), m_fill(fill), m_context(context), m_buffer(static_cast<size_t>(channels) * audioPeriodFrames) { }

    ~HostOutput() override { stop(); }

    bool start() override
    {
        if (m_running.exchange(true))
            return true;
        if (pthread_create(&m_thread, nullptr, run, this)) {
            m_running = false;
            std::fprintf(stderr, "yozora: audio thread did not start\n");
            return false;
        }
        m_started = true;
        return true;
    }

    void stop() override
    {
        if (!m_started)
            return;
        m_running = false;
        pthread_join(m_thread, nullptr);
        m_started = false;
    }

private:
    static void* run(void* argument)
    {
        auto& output = *static_cast<HostOutput*>(argument);
        const long periodNanoseconds = static_cast<long>(1e9 * audioPeriodFrames / output.m_sampleRate);
        struct timespec next;
        clock_gettime(CLOCK_MONOTONIC, &next);
        while (output.m_running) {
            output.m_fill(output.m_context, output.m_buffer.data(), audioPeriodFrames);
            next.tv_nsec += periodNanoseconds;
            while (next.tv_nsec >= 1000000000L) {
                next.tv_nsec -= 1000000000L;
                ++next.tv_sec;
            }
            int error;
            while ((error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr)) == EINTR) { }
        }
        return nullptr;
    }

    unsigned m_channels;
    float m_sampleRate;
    Fill m_fill;
    void* m_context;
    std::vector<float> m_buffer;
    pthread_t m_thread { };
    std::atomic<bool> m_running { false };
    bool m_started { false };
};

}

std::unique_ptr<AudioOutput> AudioOutput::create(unsigned channels, float sampleRate, Fill fill, void* context)
{
    if (!channels || channels > 8 || !(sampleRate >= 8000 && sampleRate <= 192000) || !fill)
        return nullptr;
    return std::make_unique<HostOutput>(channels, sampleRate, fill, context);
}

float AudioOutput::hardwareSampleRate()
{
    return 44100;
}

}
