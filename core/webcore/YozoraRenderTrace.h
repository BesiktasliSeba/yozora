#pragma once

#include <cstdio>
#include <cstdlib>
#include <wtf/MonotonicTime.h>

inline bool yozora_render_trace_enabled()
{
    static bool enabled = [] {
        const char* value = std::getenv("YOZORA_TRACE_RENDERING");
        return value && value[0] == '1' && !value[1];
    }();
    return enabled;
}

// timestamps let the probe separate nested style and layout work from callbacks
class YozoraRenderTraceScope {
public:
    explicit YozoraRenderTraceScope(const char* name)
        : m_name(name)
        , m_start(yozora_render_trace_enabled() ? WTF::MonotonicTime::now() : WTF::MonotonicTime())
    { }
    ~YozoraRenderTraceScope()
    {
        if (!yozora_render_trace_enabled())
            return;
        static unsigned records = 0;
        if (records >= 100000)
            return;
        ++records;
        auto end = WTF::MonotonicTime::now();
        std::fprintf(stderr, "yozora-trace: phase=%s begin_us=%.0f end_us=%.0f\n", m_name,
                     m_start.secondsSinceEpoch().microseconds(), end.secondsSinceEpoch().microseconds());
    }
private:
    const char* m_name;
    WTF::MonotonicTime m_start;
};
