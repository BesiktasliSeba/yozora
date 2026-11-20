#include "yozora_motion.h"
#include <math.h>
#include <stdio.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_motion: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main(void)
{
    static const yozora_motion_curve curves[] = {
        YOZORA_MOTION_ENTER, YOZORA_MOTION_EXIT, YOZORA_MOTION_DISMISS, YOZORA_MOTION_POPUP_ENTER,
        YOZORA_MOTION_POPUP_EXIT, YOZORA_MOTION_LIST_MOVE, YOZORA_MOTION_SPRING, YOZORA_MOTION_WOBBLE
    };
    size_t i;
    int step;
    for (i = 0; i < sizeof curves / sizeof curves[0]; ++i) {
        yozora_motion_curve curve = curves[i];
        double duration = yozora_motion_duration(curve);
        CHECK(duration > 0.1 && duration < 1.0);
        /* the ends are exact and a time outside the range or not a number is held to them */
        CHECK(yozora_motion_progress(curve, 0.0) == 0.0);
        CHECK(yozora_motion_progress(curve, 1.0) == 1.0);
        CHECK(yozora_motion_progress(curve, -3.0) == 0.0);
        CHECK(yozora_motion_progress(curve, 7.0) == 1.0);
        CHECK(yozora_motion_progress(curve, NAN) == 0.0);
        CHECK(yozora_motion_progress(curve, INFINITY) == 1.0);
        for (step = 1; step < 100; ++step) {
            double value = yozora_motion_progress(curve, step / 100.0);
            CHECK(isfinite(value));
            CHECK(value > -0.05 && value < 1.4);
        }
    }
    /* a bezier curve is monotonic here, a spring is not */
    for (i = 0; i < 6; ++i) {
        double previous = 0.0;
        for (step = 1; step <= 100; ++step) {
            double value = yozora_motion_progress(curves[i], step / 100.0);
            CHECK(value >= previous - 1e-9);
            previous = value;
        }
    }
    /* the wobble passes its target by about a sixth and swings back past it again, the spring by about a tenth */
    {
        double wobble_peak = 0.0, spring_peak = 0.0;
        int crossings = 0, above = 0;
        for (step = 1; step < 1000; ++step) {
            double t = step / 1000.0;
            double w = yozora_motion_progress(YOZORA_MOTION_WOBBLE, t);
            double s = yozora_motion_progress(YOZORA_MOTION_SPRING, t);
            if (w > wobble_peak)
                wobble_peak = w;
            if (s > spring_peak)
                spring_peak = s;
            if ((w > 1.0) != above) {
                ++crossings;
                above = w > 1.0;
            }
        }
        CHECK(wobble_peak > 1.12 && wobble_peak < 1.25);
        CHECK(spring_peak > 1.04 && spring_peak < 1.15);
        CHECK(crossings >= 3);
    }
    CHECK(yozora_motion_duration((yozora_motion_curve)99) == 0.0);
    CHECK(yozora_motion_progress((yozora_motion_curve)99, 0.5) == 0.0);
    if (failures)
        return 1;
    puts("motion: curve ends, bounds, monotonic beziers, spring and wobble overshoot passed");
    return 0;
}
