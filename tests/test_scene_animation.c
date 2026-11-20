#include "core/yozora_scene.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void expect(yozora_scene_animation *animation, double elapsed, int active, double expected)
{
    double values[16] = { 0 };
    assert(yozora_scene_animation_values(animation, elapsed, values) == active);
    if (active)
        assert(fabs(values[0] - expected) < 0.00001);
}

int main(void)
{
    yozora_scene_keyframe frames[2] = { 0 };
    yozora_scene_animation animation = { 0 };
    frames[0].bezier[2] = frames[0].bezier[3] = 1;
    frames[1].time = 1;
    frames[1].values[0] = 1;
    animation.frames = frames;
    animation.count = 2;
    animation.duration = 1;
    animation.iterations = 2;
    expect(&animation, -0.5, 0, 0);
    expect(&animation, 0, 1, 0);
    expect(&animation, 0.25, 1, 0.25);
    expect(&animation, 1.25, 1, 0.25);
    expect(&animation, 2, 0, 0);
    animation.fill = 3;
    expect(&animation, -0.5, 1, 0);
    expect(&animation, 2, 1, 1);
    animation.alternate = 1;
    expect(&animation, 1.25, 1, 0.75);
    expect(&animation, 2, 1, 0);
    animation.iterations = 1.5;
    expect(&animation, 2, 1, 0.5);
    animation.iterations = -1;
    expect(&animation, 1001.25, 1, 0.75);
    frames[0].bezier[0] = 1.0f / 3;
    frames[0].bezier[1] = 0;
    frames[0].bezier[2] = 2.0f / 3;
    frames[0].bezier[3] = 1;
    expect(&animation, 0.25, 1, 0.15625);
    animation.duration = 0;
    expect(&animation, 0, 0, 0);
    animation.duration = NAN;
    expect(&animation, 0, 0, 0);
    animation.duration = 1;
    expect(&animation, INFINITY, 0, 0);
    animation.count = YOZORA_ANIMATION_MAX_FRAMES + 1;
    expect(&animation, 0, 0, 0);
    animation.count = 1;
    expect(&animation, 0, 0, 0);
    assert(!yozora_scene_animation_values(NULL, 0, NULL));
    /* a 360 degree turn is sampled into many keyframes (a matrix at its beginning and end is the same one, so
       two keyframes would not move) and the compositor goes straight from sample to sample: it follows the
       rotation closely and the full count is accepted */
    {
        enum { samples = 65 };
        static yozora_scene_keyframe turn[samples];
        yozora_scene_animation rotation = { 0 };
        double values[16];
        unsigned i;
        for (i = 0; i < samples; ++i) {
            double angle = 6.283185307179586 * i / (samples - 1);
            turn[i].time = (double)i / (samples - 1);
            turn[i].values[0] = turn[i].values[5] = cos(angle);
            turn[i].values[1] = sin(angle);
            turn[i].values[4] = -sin(angle);
            turn[i].values[10] = turn[i].values[15] = 1;
            turn[i].bezier[2] = turn[i].bezier[3] = 1;
        }
        rotation.frames = turn;
        rotation.count = samples;
        rotation.duration = 1;
        rotation.iterations = 1;
        /* on a sample the matrix is exact */
        assert(yozora_scene_animation_values(&rotation, 0.25, values));
        assert(fabs(values[0]) < 1e-9 && fabs(values[1] - 1) < 1e-9);
        /* between two samples the chord of 5.6 degrees keeps the scale within 0.2 percent */
        assert(yozora_scene_animation_values(&rotation, 0.3 + 1.0 / (2 * (samples - 1)), values));
        assert(hypot(values[0], values[1]) > 0.998 && hypot(values[0], values[1]) <= 1.0000001);
        /* the largest count there is, and one more */
        {
            static yozora_scene_keyframe many[YOZORA_ANIMATION_MAX_FRAMES + 1];
            unsigned count;
            for (count = 0; count < YOZORA_ANIMATION_MAX_FRAMES + 1; ++count) {
                many[count].time = (double)count / YOZORA_ANIMATION_MAX_FRAMES;
                many[count].bezier[2] = many[count].bezier[3] = 1;
            }
            rotation.frames = many;
            rotation.count = YOZORA_ANIMATION_MAX_FRAMES;
            many[YOZORA_ANIMATION_MAX_FRAMES - 1].time = 1;
            assert(yozora_scene_animation_values(&rotation, 0.5, values));
            rotation.count = YOZORA_ANIMATION_MAX_FRAMES + 1;
            assert(!yozora_scene_animation_values(&rotation, 0.5, values));
        }
    }
    puts("scene animation: timing, fills, alternate iterations and finite bounds passed");
    return 0;
}
