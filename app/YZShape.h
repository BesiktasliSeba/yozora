#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

/* the material expressive shapes a color ball passes through, ported from the avatar in the About screen of
   exteria. every outline is 240 points built the same way, because Core Animation only interpolates two paths
   of the same structure */
typedef enum {
    YZShapeCookie,
    YZShapeFlower,
    YZShapeClover,
    YZShapeSunny,
    YZShapeBurst,
    YZShapeCount
} YZShape;

/* the closed outline of a shape in a square of side points */
UIBezierPath *YZShapePath(YZShape shape, CGFloat side);

/* a shape layer that holds a form, springs into the next, rounds back to the first and turns slowly. the
   phase follows the clock so a ball made again in the next cell carries on. nothing runs with less motion.
   call again after the layer returns on screen, Core Animation drops running animations while the app is
   hidden */
void YZShapeStartMorph(CAShapeLayer *layer, CGFloat side);
void YZShapeStopMorph(CAShapeLayer *layer);
/* while a list is dragged or coasting its balls hold still: each running ball is two animations the render
   server draws every frame and the list already asks it for every row that scrolls in. while paused
   YZShapeStartMorph only places the outline */
void YZShapeSetMorphPaused(BOOL paused);

/* a ball of one color that morphs by itself */
@interface YZMorphView : UIView {
    CAShapeLayer *_shape;
    UIColor *_color;
    CGFloat _side;
}
@property(nonatomic, retain) UIColor *color;
/* starts or stops the morph to match the state of the ball (on a window, shown, not paused) */
- (void)sync;
@end
