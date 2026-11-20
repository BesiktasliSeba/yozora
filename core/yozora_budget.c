#include "yozora_budget.h"

unsigned yozora_raster_budget_mb(unsigned memory_mb)
{
    unsigned budget;
    if (memory_mb < 400)
        return 16;
    if (memory_mb <= 512)
        return 24;
    /* the hundreds are divided first so the largest memory value cannot overflow */
    budget = memory_mb / 100 * 6 + memory_mb % 100 * 6 / 100;
    if (budget < 48)
        return 48;
    if (budget > YOZORA_RASTER_BUDGET_MAX_MB)
        return YOZORA_RASTER_BUDGET_MAX_MB;
    return budget;
}
