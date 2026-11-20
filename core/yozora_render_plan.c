#include "yozora_render_plan.h"

#include <limits.h>
#include <stdint.h>

static int64_t rect_right(yozora_rect rect) { return (int64_t)rect.x + rect.width; }
static int64_t rect_bottom(yozora_rect rect) { return (int64_t)rect.y + rect.height; }

static int rect_intersects(yozora_rect a, yozora_rect b)
{
    return a.width > 0 && a.height > 0 && b.width > 0 && b.height > 0
        && a.x < rect_right(b) && b.x < rect_right(a)
        && a.y < rect_bottom(b) && b.y < rect_bottom(a);
}

static yozora_rect rect_union(yozora_rect a, yozora_rect b, int *full)
{
    int64_t right = rect_right(a) > rect_right(b) ? rect_right(a) : rect_right(b);
    int64_t bottom = rect_bottom(a) > rect_bottom(b) ? rect_bottom(a) : rect_bottom(b);
    yozora_rect result;
    result.x = a.x < b.x ? a.x : b.x;
    result.y = a.y < b.y ? a.y : b.y;
    if (right - result.x > INT_MAX || bottom - result.y > INT_MAX)
        *full = 1;
    result.width = right - result.x > INT_MAX ? INT_MAX : (int)(right - result.x);
    result.height = bottom - result.y > INT_MAX ? INT_MAX : (int)(bottom - result.y);
    return result;
}

void yozora_damage_add(yozora_damage *damage, yozora_rect rect)
{
    unsigned i;
    if (!damage || damage->full || rect.width <= 0 || rect.height <= 0)
        return;
    for (i = 0; i < damage->count;) {
        if (rect_intersects(rect, damage->rects[i])) {
            rect = rect_union(rect, damage->rects[i], &damage->full);
            damage->rects[i] = damage->rects[--damage->count];
            i = 0;
        } else
            ++i;
    }
    if (damage->count == YOZORA_DAMAGE_RECTS) {
        for (i = 0; i < damage->count; ++i)
            rect = rect_union(rect, damage->rects[i], &damage->full);
        damage->count = 0;
    }
    damage->rects[damage->count++] = rect;
}

int yozora_damage_intersects(const yozora_damage *damage, yozora_rect rect)
{
    unsigned i;
    if (!damage)
        return 0;
    if (damage->full)
        return rect.width > 0 && rect.height > 0;
    for (i = 0; i < damage->count; ++i) {
        if (rect_intersects(damage->rects[i], rect))
            return 1;
    }
    return 0;
}

int64_t yozora_frame_deadline(int64_t now, int64_t previous, int64_t interval)
{
    int64_t remainder;
    if (now < 0 || interval <= 0)
        return now;
    if (previous <= 0 || previous > now) {
        if (previous > now)
            return previous;
        return interval > INT64_MAX - now ? INT64_MAX : now + interval;
    }
    remainder = (now - previous) % interval;
    return interval - remainder > INT64_MAX - now ? INT64_MAX : now + interval - remainder;
}

static float smaller(float a, float b) { return a < b ? a : b; }
static float larger(float a, float b) { return a > b ? a : b; }

static void sticky_offset(const yozora_sticky *s, float dx, float dy, float *x, float *y)
{
    *x = 0;
    *y = 0;
    if (s->edges & 2)
        *x += larger(smaller(0, s->viewport[0] + dx + s->viewport[2] - s->offsets[1] - s->box[0] - s->box[2]), smaller(0, s->container[0] - s->box[0]));
    if (s->edges & 1)
        *x += smaller(larger(0, s->viewport[0] + dx + s->offsets[0] - s->box[0]), larger(0, s->container[0] + s->container[2] - s->box[0] - s->box[2]));
    if (s->edges & 8)
        *y += larger(smaller(0, s->viewport[1] + dy + s->viewport[3] - s->offsets[3] - s->box[1] - s->box[3]), smaller(0, s->container[1] - s->box[1]));
    if (s->edges & 4)
        *y += smaller(larger(0, s->viewport[1] + dy + s->offsets[2] - s->box[1]), larger(0, s->container[1] + s->container[3] - s->box[1] - s->box[3]));
}

void yozora_sticky_delta(const yozora_sticky *sticky, float dx, float dy, float *x, float *y)
{
    float before_x, before_y, after_x, after_y;
    if (!x || !y)
        return;
    *x = *y = 0;
    if (!sticky || !sticky->scroll_id)
        return;
    sticky_offset(sticky, 0, 0, &before_x, &before_y);
    sticky_offset(sticky, dx, dy, &after_x, &after_y);
    *x = after_x - before_x;
    *y = after_y - before_y;
}

int yozora_scroll_state_offset(const yozora_scroll_state *state, uint64_t id, int *x, int *y)
{
    unsigned i;
    if (x)
        *x = 0;
    if (y)
        *y = 0;
    if (!state || !id || !x || !y || state->count > YOZORA_MAX_SCROLL_AREAS)
        return 0;
    if (id == 1) {
        *y = state->root_y;
        return 1;
    }
    for (i = 0; i < state->count; ++i) {
        if (state->areas[i].id == id) {
            *x = state->areas[i].x;
            *y = state->areas[i].y;
            return 1;
        }
    }
    return 0;
}
