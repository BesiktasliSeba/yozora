#ifndef YOZORA_MOTION_H
#define YOZORA_MOTION_H

#ifdef __cplusplus
extern "C" {
#endif

/* the curves of the interface. time and progress are fractions of the duration of the curve, the progress of a spring passes 1 */
typedef enum {
    /* a view that arrives: a fast start and a long settle (ease out quint) */
    YOZORA_MOTION_ENTER,
    /* a view that leaves slowly at first and then quickly */
    YOZORA_MOTION_EXIT,
    /* a card or a menu that goes away: it starts to leave at once and settles */
    YOZORA_MOTION_DISMISS,
    /* sheets and menus of Material 3: the emphasized decelerate curve in and the emphasized accelerate curve out */
    YOZORA_MOTION_POPUP_ENTER,
    YOZORA_MOTION_POPUP_EXIT,
    /* rows and cards that change place */
    YOZORA_MOTION_LIST_MOVE,
    /* a spring that overshoots by a tenth and settles in one swing */
    YOZORA_MOTION_SPRING,
    /* the loose spring that overshoots by about a sixth and swings back twice */
    YOZORA_MOTION_WOBBLE
} yozora_motion_curve;

/* seconds, zero for a curve that does not exist */
double yozora_motion_duration(yozora_motion_curve curve);
/* progress at a time from 0 to 1, exactly 0 at 0 and exactly 1 at 1, a time out of range or not a number is
   held to its end */
double yozora_motion_progress(yozora_motion_curve curve, double time);

#ifdef __cplusplus
}
#endif
#endif
