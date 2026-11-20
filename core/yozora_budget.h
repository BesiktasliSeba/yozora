#ifndef YOZORA_BUDGET_H
#define YOZORA_BUDGET_H

#ifdef __cplusplus
extern "C" {
#endif

#define YOZORA_RASTER_BUDGET_MAX_MB 512

/* the pixel budget of one page core in MiB. the raster cache and up to four held scenes can each reach it, so
   the worst case is about five times it, kept near 30 percent of memory: 16 below 400 MiB, 24 up to 512, 6
   percent above, from 48 to YOZORA_RASTER_BUDGET_MAX_MB */
unsigned yozora_raster_budget_mb(unsigned memory_mb);

#ifdef __cplusplus
}
#endif
#endif
