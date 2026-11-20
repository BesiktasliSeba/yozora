#include "yozora_memory.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    static const struct {
        uint64_t size;
        uint64_t page;
        yozora_memory_status status;
        uint64_t expected;
    } cases[] = {
        { 0, 4096, YOZORA_MEMORY_INVALID, 0 },
        { 1, 0, YOZORA_MEMORY_INVALID, 0 },
        { 1, 3, YOZORA_MEMORY_INVALID, 0 },
        { 1, 131072, YOZORA_MEMORY_INVALID, 0 },
        { 1, 4096, YOZORA_MEMORY_OK, 4096 },
        { 4096, 4096, YOZORA_MEMORY_OK, 4096 },
        { 4097, 4096, YOZORA_MEMORY_OK, 8192 },
        { 1, 16384, YOZORA_MEMORY_OK, 16384 },
        { YOZORA_MEMORY_MAX_SIZE, 4096, YOZORA_MEMORY_OK, YOZORA_MEMORY_MAX_SIZE },
        { YOZORA_MEMORY_MAX_SIZE - 1, 65536, YOZORA_MEMORY_OK, YOZORA_MEMORY_MAX_SIZE },
        { YOZORA_MEMORY_MAX_SIZE + 1, 4096, YOZORA_MEMORY_LIMIT, 0 },
        { UINT64_MAX, 4096, YOZORA_MEMORY_LIMIT, 0 },
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint64_t rounded = UINT64_MAX;
        assert(yozora_memory_round_size(cases[i].size, cases[i].page, &rounded) == cases[i].status);
        assert(rounded == cases[i].expected);
    }
    assert(yozora_memory_round_size(1, 4096, NULL) == YOZORA_MEMORY_INVALID);
    puts("memory bounds: zero, page sizes, rounding, maximum and overflow passed");
    return 0;
}
