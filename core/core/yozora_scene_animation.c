#include "yozora_scene.h"
#include <math.h>

static double cubic(double t, double first, double second)
{
    double inverse = 1 - t;
    return 3 * inverse * inverse * t * first + 3 * inverse * t * t * second + t * t * t;
}

static double timing_progress(double progress, const float controls[4])
{
    double low = 0, high = 1, t = progress;
    unsigned i;
    if (controls[0] == controls[1] && controls[2] == controls[3])
        return progress;
    /* bisection also handles cubic curves with a zero derivative at an endpoint */
    for (i = 0; i < 32; ++i) {
        if (cubic(t, controls[0], controls[2]) < progress)
            low = t;
        else
            high = t;
        t = (low + high) / 2;
    }
    return cubic(t, controls[1], controls[3]);
}

int yozora_scene_animation_values(const yozora_scene_animation *animation, double elapsed, double values[16])
{
    double iterations, position, iteration, progress;
    unsigned i, j;
    const yozora_scene_keyframe *left, *right;
    if (!animation || !values || !animation->frames || animation->count < 2 || animation->count > YOZORA_ANIMATION_MAX_FRAMES
        || !(animation->duration > 0) || !isfinite(animation->duration) || !isfinite(elapsed))
        return 0;
    iterations = animation->iterations;
    if (elapsed < 0) {
        if (!(animation->fill & 2))
            return 0;
        elapsed = 0;
    }
    position = elapsed / animation->duration;
    if (!isfinite(position) || !isfinite(iterations))
        return 0;
    if (iterations >= 0 && position >= iterations) {
        if (!(animation->fill & 1))
            return 0;
        position = iterations;
    }
    iteration = floor(position);
    progress = position - iteration;
    if (progress == 0 && position > 0 && iterations >= 0 && position >= iterations) {
        iteration -= 1;
        progress = 1;
    }
    if (animation->alternate && fmod(iteration, 2) >= 1)
        progress = 1 - progress;
    left = &animation->frames[0];
    right = &animation->frames[animation->count - 1];
    for (i = 1; i < animation->count; ++i) {
        if (progress <= animation->frames[i].time) {
            left = &animation->frames[i - 1];
            right = &animation->frames[i];
            break;
        }
    }
    progress = right->time > left->time ? (progress - left->time) / (right->time - left->time) : 1;
    progress = progress < 0 ? 0 : (progress > 1 ? 1 : progress);
    progress = timing_progress(progress, left->bezier);
    for (j = 0; j < 16; ++j)
        values[j] = left->values[j] + (right->values[j] - left->values[j]) * progress;
    return 1;
}
