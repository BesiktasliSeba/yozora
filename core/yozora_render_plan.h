#ifndef YOZORA_RENDER_PLAN_H
#define YOZORA_RENDER_PLAN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YOZORA_DAMAGE_RECTS 32
typedef struct {
    int x, y, width, height;
} yozora_rect;
typedef struct {
    yozora_rect rects[YOZORA_DAMAGE_RECTS];
    unsigned count;
    int full;
} yozora_damage;

/* disjoint changes stay separate, the cap bounds input from pages that animate many elements */
void yozora_damage_add(yozora_damage *damage, yozora_rect rect);
int yozora_damage_intersects(const yozora_damage *damage, yozora_rect rect);
/* the next future frame deadline, missed frames are skipped without adding the work time to the interval */
int64_t yozora_frame_deadline(int64_t now, int64_t previous, int64_t interval);

#define YOZORA_MAX_SCROLL_AREAS 32
typedef struct {
    uint64_t id;
    int x, y;
} yozora_scroll_offset;
typedef struct {
    int root_y;
    unsigned count;
    yozora_scroll_offset areas[YOZORA_MAX_SCROLL_AREAS];
} yozora_scroll_state;
/* id 1 is the main frame, missing ids leave both outputs at zero */
int yozora_scroll_state_offset(const yozora_scroll_state *state, uint64_t id, int *x, int *y);

typedef struct {
    uint64_t scroll_id;
    unsigned edges;
    float viewport[4], container[4], box[4];
    /* left, right, top, bottom */
    float offsets[4];
    float scroll_x, scroll_y;
} yozora_sticky;
/* the change from the captured sticky offset, with the containing block as a hard boundary */
void yozora_sticky_delta(const yozora_sticky *sticky, float dx, float dy, float *x, float *y);

#ifdef __cplusplus
}
#endif
#endif
