#include "yozora_render_plan.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static void test_scroll_state(void)
{
    yozora_scroll_state state = {0};
    int x = 9, y = 9;
    unsigned i;
    state.root_y = -120;
    assert(yozora_scroll_state_offset(&state, 1, &x, &y) && x == 0 && y == -120);
    assert(!yozora_scroll_state_offset(&state, 2, &x, &y) && x == 0 && y == 0);
    state.count = YOZORA_MAX_SCROLL_AREAS;
    for (i = 0; i < state.count; ++i) {
        state.areas[i].id = UINT64_MAX - i;
        state.areas[i].x = -(int)i;
        state.areas[i].y = (int)i * 3;
    }
    for (i = 0; i < state.count; ++i) {
        assert(yozora_scroll_state_offset(&state, UINT64_MAX - i, &x, &y));
        assert(x == -(int)i && y == (int)i * 3);
    }
    assert(!yozora_scroll_state_offset(&state, 0, &x, &y) && x == 0 && y == 0);
    assert(!yozora_scroll_state_offset(NULL, 1, &x, &y) && x == 0 && y == 0);
    assert(!yozora_scroll_state_offset(&state, 1, NULL, &y) && y == 0);
    assert(!yozora_scroll_state_offset(&state, 1, &x, NULL) && x == 0);
    state.count++;
    assert(!yozora_scroll_state_offset(&state, UINT64_MAX, &x, &y) && x == 0 && y == 0);
}

int main(void)
{
    yozora_damage damage = {0};
    test_scroll_state();
    unsigned i;
    yozora_damage_add(&damage, (yozora_rect){10, 10, 20, 20});
    yozora_damage_add(&damage, (yozora_rect){10, 400, 20, 20});
    assert(damage.count == 2);
    assert(!yozora_damage_intersects(&damage, (yozora_rect){0, 128, 320, 128}));
    assert(yozora_damage_intersects(&damage, (yozora_rect){0, 0, 320, 128}));
    assert(!yozora_damage_intersects(&damage, (yozora_rect){30, 10, 1, 1}));
    yozora_damage_add(&damage, (yozora_rect){20, 20, 20, 20});
    assert(damage.count == 2);
    assert(yozora_damage_intersects(&damage, (yozora_rect){35, 35, 1, 1}));
    yozora_damage_add(&damage, (yozora_rect){0, 0, 0, 0});
    assert(damage.count == 2);
    damage.count = 0;
    for (i = 0; i < 100; ++i)
        yozora_damage_add(&damage, (yozora_rect){0, (int)i * 10, 5, 5});
    assert(damage.count <= YOZORA_DAMAGE_RECTS);
    for (i = 0; i < 100; ++i)
        assert(yozora_damage_intersects(&damage, (yozora_rect){0, (int)i * 10, 5, 5}));
    damage.count = 0;
    yozora_damage_add(&damage, (yozora_rect){INT_MAX - 10, INT_MAX - 10, 20, 20});
    assert(yozora_damage_intersects(&damage, (yozora_rect){INT_MAX, INT_MAX, 1, 1}));
    assert(yozora_frame_deadline(100, 0, 16) == 116);
    assert(yozora_frame_deadline(110, 116, 16) == 116);
    assert(yozora_frame_deadline(120, 116, 16) == 132);
    assert(yozora_frame_deadline(200, 116, 16) == 212);
    assert(yozora_frame_deadline(INT64_MAX - 2, 0, 16) == INT64_MAX);
    assert(yozora_frame_deadline(1, 0, 0) == 1);
    {
        yozora_sticky sticky = {0};
        float x, y;
        sticky.scroll_id = 1;
        sticky.edges = 4;
        sticky.viewport[2] = 320;
        sticky.viewport[3] = 400;
        sticky.container[2] = 320;
        sticky.container[3] = 1000;
        sticky.box[1] = 100;
        sticky.box[2] = 320;
        sticky.box[3] = 40;
        yozora_sticky_delta(&sticky, 0, 50, &x, &y);
        assert(x == 0 && y == 0);
        yozora_sticky_delta(&sticky, 0, 150, &x, &y);
        assert(y == 50);
        yozora_sticky_delta(&sticky, 0, 2000, &x, &y);
        assert(y == 860);
        sticky.viewport[1] = 150;
        yozora_sticky_delta(&sticky, 0, -150, &x, &y);
        assert(y == -50);
    }
    puts("render plan: disjoint damage, overlap, bounded overflow, frame deadlines and scroll snapshots passed");
    return 0;
}
