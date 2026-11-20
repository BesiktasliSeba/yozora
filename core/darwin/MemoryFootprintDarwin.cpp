#include "config.h"
#include "yozora_darwin.h"

#include <wtf/Assertions.h>
#include <wtf/MemoryFootprint.h>

namespace WTF {

size_t memoryFootprint()
{
    size_t value = 0;
    auto status = yozora_darwin_memory_bytes(&value);
    RELEASE_ASSERT_WITH_MESSAGE(status == YOZORA_CLOCK_OK, "yozora task memory query failed: %d", status);
    return value;
}

}
