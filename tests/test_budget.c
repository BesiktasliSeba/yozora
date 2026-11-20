#include "yozora_budget.h"
#include <limits.h>
#include <stdio.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_budget: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main(void)
{
    static const struct { unsigned memory; unsigned budget; } cases[] = {
        { 0, 16 }, { 128, 16 }, { 256, 16 }, { 399, 16 },
        { 400, 24 }, { 512, 24 },
        { 513, 48 }, { 768, 48 }, { 900, 54 }, { 1024, 61 },
        { 2048, 122 }, { 3072, 184 }, { 4096, 245 }, { 6144, 368 },
        { 8192, 491 }, { 9000, 512 }, { 16384, 512 }, { UINT_MAX, 512 }
    };
    size_t i;
    unsigned previous = 0;
    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        unsigned budget = yozora_raster_budget_mb(cases[i].memory);
        CHECK(budget == cases[i].budget);
        /* more memory never gives a smaller budget */
        CHECK(budget >= previous);
        previous = budget;
        /* five budgets stay within a third of the memory from 800 MiB, where the floor of 48 MiB no longer dominates */
        if (cases[i].memory >= 800 && cases[i].memory < UINT_MAX)
            CHECK((unsigned long long)budget * 15 <= cases[i].memory);
    }
    if (failures)
        return 1;
    puts("budget: tiers, the percent rule, the ceiling and overflow passed");
    return 0;
}
