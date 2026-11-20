#include "config.h"
#include "yozora_darwin.h"

#include <cerrno>
#include <sys/resource.h>
#include <wtf/CPUTime.h>
#include <wtf/DataLog.h>

namespace WTF {

std::optional<CPUTime> CPUTime::get()
{
    struct rusage resource { };
    if (getrusage(RUSAGE_SELF, &resource)) {
        dataLogLn("yozora: getrusage(RUSAGE_SELF) failed: ", errno);
        return std::nullopt;
    }
    auto seconds = [](const struct timeval& value) {
        return Seconds(value.tv_sec) + Seconds::fromMicroseconds(value.tv_usec);
    };
    return CPUTime { MonotonicTime::now(), seconds(resource.ru_utime), seconds(resource.ru_stime) };
}

Seconds CPUTime::forCurrentThread()
{
    uint64_t value = 0;
    auto status = yozora_darwin_thread_cpu_us(&value);
    RELEASE_ASSERT_WITH_MESSAGE(status == YOZORA_CLOCK_OK, "yozora thread CPU clock failed: %d", status);
    return Seconds::fromMicroseconds(value);
}

}
