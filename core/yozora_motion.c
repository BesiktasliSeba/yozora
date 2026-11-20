#include "yozora_motion.h"
#include <math.h>

static double cubic(double t, double first, double second)
{
    double inverse = 1.0 - t;
    return 3.0 * inverse * inverse * t * first + 3.0 * inverse * t * t * second + t * t * t;
}

/* the cubic bezier with the end points (0, 0) and (1, 1), x is solved by bisection because it has no closed form */
static double bezier(double time, double x1, double y1, double x2, double y2)
{
    double lower = 0.0, upper = 1.0;
    int i;
    for (i = 0; i < 32; ++i) {
        double middle = (lower + upper) * 0.5;
        if (cubic(middle, x1, x2) < time)
            lower = middle;
        else
            upper = middle;
    }
    return cubic((lower + upper) * 0.5, y1, y2);
}

/* a damped spring of unit mass released at rest, the damping ratio is below 1 so the swing is oscillating */
static double spring(double seconds, double stiffness, double damping)
{
    double frequency = sqrt(stiffness);
    double decay = frequency * damping;
    double oscillation = frequency * sqrt(1.0 - damping * damping);
    return 1.0 - exp(-decay * seconds) * (cos(oscillation * seconds) + decay / oscillation * sin(oscillation * seconds));
}

double yozora_motion_duration(yozora_motion_curve curve)
{
    switch (curve) {
    case YOZORA_MOTION_ENTER: return 0.32;
    case YOZORA_MOTION_EXIT: return 0.18;
    case YOZORA_MOTION_DISMISS: return 0.2;
    case YOZORA_MOTION_POPUP_ENTER: return 0.34;
    case YOZORA_MOTION_POPUP_EXIT: return 0.2;
    case YOZORA_MOTION_LIST_MOVE: return 0.25;
    case YOZORA_MOTION_SPRING: return 0.48;
    case YOZORA_MOTION_WOBBLE: return 0.6;
    }
    return 0.0;
}

double yozora_motion_progress(yozora_motion_curve curve, double time)
{
    double duration = yozora_motion_duration(curve);
    if (duration == 0.0)
        return 0.0;
    if (!(time > 0.0))
        return 0.0;
    if (time >= 1.0)
        return 1.0;
    switch (curve) {
    case YOZORA_MOTION_ENTER: return bezier(time, 0.23, 1.0, 0.32, 1.0);
    case YOZORA_MOTION_EXIT: return bezier(time, 0.42, 0.0, 1.0, 1.0);
    case YOZORA_MOTION_DISMISS: {
        double remaining = 1.0 - time;
        return 1.0 - remaining * remaining;
    }
    case YOZORA_MOTION_POPUP_ENTER: return bezier(time, 0.05, 0.7, 0.1, 1.0);
    case YOZORA_MOTION_POPUP_EXIT: return bezier(time, 0.3, 0.0, 0.8, 0.15);
    case YOZORA_MOTION_LIST_MOVE: return bezier(time, 0.2, 0.01, 0.28, 0.91);
    /* the spring is normalized by its value at the end of the duration, so the last sample is exactly 1 */
    case YOZORA_MOTION_SPRING: return spring(time * duration, 650.0, 0.6) / spring(duration, 650.0, 0.6);
    case YOZORA_MOTION_WOBBLE: return spring(time * duration, 420.0, 0.5) / spring(duration, 420.0, 0.5);
    }
    return 0.0;
}
