#import <UIKit/UIKit.h>
#include "yozora_motion.h"

/* a view takes its final state at once and plays the way there as a sampled curve, so the spring is the same
   on ios 5 where CASpringAnimation does not exist. a running motion is retargeted from what is on screen.
   with reduced motion or off screen only the final state is taken */
void YZMotionAnimateView(UIView *view, CGAffineTransform transform, CGFloat alpha, yozora_motion_curve curve, NSTimeInterval delay,
                         void (^completion)(BOOL finished));
/* the view is already in its final state, this plays the way in from the given transform and alpha without touching it */
void YZMotionAnimateEnter(UIView *view, CGAffineTransform from, CGFloat fromAlpha, yozora_motion_curve curve, NSTimeInterval delay);
/* a screen look change as a circle that opens from a point: begin covers the screen with its picture, finish
   lets it leave through a growing circle. begin returns nil and finish does nothing with reduced motion or
   off screen. the cover takes no touches */
UIView *YZMotionBeginReveal(UIView *host);
void YZMotionFinishReveal(UIView *cover, CGPoint origin);
/* the control shrinks by depth (0.05 is five percent) while a finger is on it and springs back past its size when the finger leaves */
void YZMotionAttachBounce(UIControl *control, CGFloat depth);
