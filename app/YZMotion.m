#import "YZMotion.h"
#import "YZTheme.h"
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>

/* a sample every 10 ms at the longest curve, which is smooth on a 60 Hz screen and cheap to build on an old phone */
#define YZ_MOTION_SAMPLES 61

static NSString *const YZMotionKey = @"yz.motion";
static NSString *const YZEnterKey = @"yz.enter";
static NSString *const YZBounceKey = @"yz.bounce";

@interface YZMotionCompletion : NSObject {
    void (^_completion)(BOOL);
}
- (id)initWithCompletion:(void (^)(BOOL))completion;
@end

@implementation YZMotionCompletion
- (id)initWithCompletion:(void (^)(BOOL))completion {
    self = [super init];
    if (self)
        _completion = [completion copy];
    return self;
}
- (void)dealloc {
    [_completion release];
    [super dealloc];
}
- (void)animationDidStop:(CAAnimation *)animation finished:(BOOL)finished {
    (void)animation;
    if (_completion)
        _completion(finished);
}
@end

static CGAffineTransform YZBlend(CGAffineTransform from, CGAffineTransform to, CGFloat progress) {
    return CGAffineTransformMake(from.a + (to.a - from.a) * progress, from.b + (to.b - from.b) * progress,
                                 from.c + (to.c - from.c) * progress, from.d + (to.d - from.d) * progress,
                                 from.tx + (to.tx - from.tx) * progress, from.ty + (to.ty - from.ty) * progress);
}

// the flat design moves things the way ios 7 does: one ease out, no overshoot and no spring, quick on the way out. every curve of the modern design
// is played as this one there
static double YZFlatProgress(double time) {
    double rest = 1 - time;
    return 1 - rest * rest * rest;
}

static NSTimeInterval YZFlatDuration(yozora_motion_curve curve) {
    return curve == YOZORA_MOTION_DISMISS || curve == YOZORA_MOTION_EXIT || curve == YOZORA_MOTION_POPUP_EXIT ? 0.22 : 0.34;
}

static CAAnimationGroup *YZMotionGroup(CGAffineTransform from, CGAffineTransform to, CGFloat fromAlpha, CGFloat toAlpha, yozora_motion_curve curve) {
    NSMutableArray *transforms = [NSMutableArray arrayWithCapacity:YZ_MOTION_SAMPLES];
    NSMutableArray *opacities = [NSMutableArray arrayWithCapacity:YZ_MOTION_SAMPLES];
    for (NSUInteger i = 0; i < YZ_MOTION_SAMPLES; ++i) {
        double time = (double)i / (YZ_MOTION_SAMPLES - 1);
        CGFloat progress = (CGFloat)([YZTheme isFlat] ? YZFlatProgress(time) : yozora_motion_progress(curve, time));
        [transforms addObject:[NSValue valueWithCATransform3D:CATransform3DMakeAffineTransform(YZBlend(from, to, progress))]];
        // a spring passes its target and the opacity is held to the range, a fade does not bounce
        [opacities addObject:[NSNumber numberWithDouble:fromAlpha + (toAlpha - fromAlpha) * MIN((CGFloat)1, MAX((CGFloat)0, progress))]];
    }
    CAKeyframeAnimation *movement = [CAKeyframeAnimation animationWithKeyPath:@"transform"];
    movement.values = transforms;
    CAKeyframeAnimation *fade = [CAKeyframeAnimation animationWithKeyPath:@"opacity"];
    fade.values = opacities;
    CAAnimationGroup *group = [CAAnimationGroup animation];
    group.duration = [YZTheme isFlat] ? YZFlatDuration(curve) : yozora_motion_duration(curve);
    movement.duration = fade.duration = group.duration;
    group.animations = [NSArray arrayWithObjects:movement, fade, nil];
    group.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionLinear];
    return group;
}

void YZMotionAnimateView(UIView *view, CGAffineTransform transform, CGFloat alpha, yozora_motion_curve curve, NSTimeInterval delay,
                         void (^completion)(BOOL finished)) {
    CALayer *layer = view.layer;
    // the classic design uses the animation of the system and none of the sampled curves: ios 5 to 7 move a view
    // with one ease in out
    if ([YZTheme isClassic]) {
        [layer removeAnimationForKey:YZMotionKey];
        [layer removeAnimationForKey:YZEnterKey];
        if (!view.window || ![YZTheme animationsEnabled]) {
            view.transform = transform;
            view.alpha = alpha;
            if (completion)
                completion(YES);
            return;
        }
        [UIView animateWithDuration:0.3 delay:delay options:UIViewAnimationOptionBeginFromCurrentState | UIViewAnimationOptionCurveEaseInOut animations:^{
            view.transform = transform;
            view.alpha = alpha;
        } completion:^(BOOL finished) {
            if (completion)
                completion(finished);
        }];
        return;
    }
    // a view that leaves while its way in plays starts from where it is seen, the model value is already the
    // end of the way in, so starting from it made a sheet closed early jump to its place first. the way in is
    // dropped so it cannot play under the new motion
    BOOL moving = [layer animationForKey:YZMotionKey] || [layer animationForKey:YZEnterKey];
    CALayer *visible = moving ? [layer presentationLayer] : nil;
    CGAffineTransform from = visible ? CATransform3DGetAffineTransform(visible.transform) : view.transform;
    CGFloat fromAlpha = visible ? visible.opacity : view.alpha;
    [layer removeAnimationForKey:YZMotionKey];
    [layer removeAnimationForKey:YZEnterKey];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    view.transform = transform;
    view.alpha = alpha;
    [CATransaction commit];
    if (!view.window || ![YZTheme animationsEnabled]) {
        if (completion)
            completion(YES);
        return;
    }
    CAAnimationGroup *group = YZMotionGroup(from, transform, fromAlpha, alpha, curve);
    if (delay > 0) {
        group.beginTime = [layer convertTime:CACurrentMediaTime() fromLayer:nil] + delay;
        group.fillMode = kCAFillModeBackwards;
    }
    if (completion)
        group.delegate = (id)[[[YZMotionCompletion alloc] initWithCompletion:completion] autorelease];
    [layer addAnimation:group forKey:YZMotionKey];
}

void YZMotionAnimateEnter(UIView *view, CGAffineTransform from, CGFloat fromAlpha, yozora_motion_curve curve, NSTimeInterval delay) {
    CALayer *layer = view.layer;
    [layer removeAnimationForKey:YZEnterKey];
    // the classic design has no sampled way in, the view just stands where the caller put it
    if ([YZTheme isClassic] || !(view.window || view.superview) || ![YZTheme animationsEnabled])
        return;
    CAAnimationGroup *group = YZMotionGroup(from, view.transform, fromAlpha, view.alpha, curve);
    if (delay > 0) {
        group.beginTime = [layer convertTime:CACurrentMediaTime() fromLayer:nil] + delay;
        group.fillMode = kCAFillModeBackwards;
    }
    [layer addAnimation:group forKey:YZEnterKey];
}

UIView *YZMotionBeginReveal(UIView *host) {
    // the circle that opens from the switch is not an animation of ios 5 to 7, the flat design fades the old look away instead
    if ([YZTheme isClassic] || !host.window || ![YZTheme animationsEnabled] || host.bounds.size.width < 1 || host.bounds.size.height < 1)
        return nil;
    UIGraphicsBeginImageContextWithOptions(host.bounds.size, YES, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();
    if (!context) {
        UIGraphicsEndImageContext();
        return nil;
    }
    [host.layer renderInContext:context];
    UIImage *picture = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    UIImageView *cover = [[[UIImageView alloc] initWithImage:picture] autorelease];
    cover.frame = host.bounds;
    cover.userInteractionEnabled = NO;
    [host addSubview:cover];
    return cover;
}

// the mask is the screen plus the circle filled by the even odd rule, so the circle is a hole and the picture
// keeps what lies outside it
static CGPathRef YZRevealPath(CGRect bounds, CGPoint origin, CGFloat radius) {
    UIBezierPath *path = [UIBezierPath bezierPathWithRect:bounds];
    [path appendPath:[UIBezierPath bezierPathWithArcCenter:origin radius:MAX((CGFloat)0.01, radius) startAngle:0 endAngle:(CGFloat)(2 * M_PI) clockwise:YES]];
    return path.CGPath;
}

void YZMotionFinishReveal(UIView *cover, CGPoint origin) {
    if (!cover)
        return;
    if ([YZTheme isFlat]) {
        [UIView animateWithDuration:0.28 delay:0 options:UIViewAnimationOptionCurveEaseOut animations:^{ cover.alpha = 0; } completion:^(BOOL finished) {
            (void)finished;
            [cover removeFromSuperview];
        }];
        return;
    }
    CGRect bounds = cover.bounds;
    // the circle has to pass the corner that lies farthest from where it opens
    CGFloat dx = MAX(origin.x, bounds.size.width - origin.x), dy = MAX(origin.y, bounds.size.height - origin.y);
    CGFloat reach = (CGFloat)sqrt(dx * dx + dy * dy) + 2;
    CAShapeLayer *mask = [CAShapeLayer layer];
    mask.frame = bounds;
    mask.fillRule = kCAFillRuleEvenOdd;
    mask.path = YZRevealPath(bounds, origin, reach);
    cover.layer.mask = mask;
    // emphasized curve of material 3 over half a second, long enough to follow from the switch to the corner
    const NSTimeInterval duration = 0.5;
    NSMutableArray *paths = [NSMutableArray arrayWithCapacity:YZ_MOTION_SAMPLES];
    for (NSUInteger i = 0; i < YZ_MOTION_SAMPLES; ++i) {
        CGFloat progress = MIN((CGFloat)1, (CGFloat)yozora_motion_progress(YOZORA_MOTION_POPUP_ENTER, (double)i / (YZ_MOTION_SAMPLES - 1)));
        [paths addObject:(id)YZRevealPath(bounds, origin, reach * progress)];
    }
    CAKeyframeAnimation *open = [CAKeyframeAnimation animationWithKeyPath:@"path"];
    open.values = paths;
    open.duration = duration;
    open.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionLinear];
    open.delegate = (id)[[[YZMotionCompletion alloc] initWithCompletion:^(BOOL finished) {
        (void)finished;
        [cover removeFromSuperview];
    }] autorelease];
    [mask addAnimation:open forKey:@"yz.reveal"];
}

static const NSTimeInterval YZBouncePress = 0.06;
static const NSTimeInterval YZBounceRelease = 0.35;
static const double YZBounceOvershoot = 5.0;

@interface YZBounceTarget : NSObject
+ (YZBounceTarget *)shared;
- (void)press:(UIControl *)control;
- (void)lift:(UIControl *)control;
@end

// the scale the layer shows now, which is where a new bounce starts from
static CGFloat YZBounceShownScale(CALayer *layer) {
    CALayer *shown = [layer animationForKey:YZBounceKey] ? [layer presentationLayer] : nil;
    NSNumber *scale = shown ? [shown valueForKeyPath:@"transform.scale"] : nil;
    return scale ? (CGFloat)[scale doubleValue] : 1;
}

@implementation YZBounceTarget
+ (YZBounceTarget *)shared {
    static YZBounceTarget *target;
    if (!target)
        target = [[YZBounceTarget alloc] init];
    return target;
}

- (BOOL)animates:(UIControl *)control {
    // a system button only lights while pressed, it does not shrink or spring, and the flat design's buttons are the same
    return control.window && [YZTheme animationsEnabled] && ![YZTheme isClassic] && ![YZTheme isFlat];
}

- (void)press:(UIControl *)control {
    if (![self animates:control])
        return;
    CALayer *layer = control.layer;
    CGFloat depth = (CGFloat)[objc_getAssociatedObject(control, &YZBounceKey) doubleValue];
    CABasicAnimation *shrink = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
    shrink.fromValue = [NSNumber numberWithDouble:YZBounceShownScale(layer)];
    shrink.toValue = [NSNumber numberWithDouble:1 - depth];
    shrink.duration = YZBouncePress;
    shrink.fillMode = kCAFillModeForwards;
    shrink.removedOnCompletion = NO;
    shrink.timingFunction = [CAMediaTimingFunction functionWithControlPoints:0.25f:0.1f:0.25f:1];
    [layer addAnimation:shrink forKey:YZBounceKey];
}

- (void)lift:(UIControl *)control {
    CALayer *layer = control.layer;
    if (![layer animationForKey:YZBounceKey])
        return;
    CGFloat from = YZBounceShownScale(layer);
    if (![self animates:control]) {
        [layer removeAnimationForKey:YZBounceKey];
        return;
    }
    NSMutableArray *values = [NSMutableArray arrayWithCapacity:YZ_MOTION_SAMPLES];
    for (NSUInteger i = 0; i < YZ_MOTION_SAMPLES; ++i) {
        double t = (double)i / (YZ_MOTION_SAMPLES - 1) - 1;
        double overshoot = t * t * ((YZBounceOvershoot + 1) * t + YZBounceOvershoot) + 1;
        [values addObject:[NSNumber numberWithDouble:from + (1 - from) * overshoot]];
    }
    CAKeyframeAnimation *spring = [CAKeyframeAnimation animationWithKeyPath:@"transform.scale"];
    spring.values = values;
    spring.duration = YZBounceRelease;
    spring.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionLinear];
    [layer addAnimation:spring forKey:YZBounceKey];
}
@end

void YZMotionAttachBounce(UIControl *control, CGFloat depth) {
    YZBounceTarget *target = [YZBounceTarget shared];
    objc_setAssociatedObject(control, &YZBounceKey, [NSNumber numberWithDouble:depth], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [control addTarget:target action:@selector(press:) forControlEvents:UIControlEventTouchDown | UIControlEventTouchDragEnter];
    [control addTarget:target action:@selector(lift:)
      forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel | UIControlEventTouchDragExit];
}
