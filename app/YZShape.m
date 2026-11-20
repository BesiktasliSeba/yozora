#import "YZShape.h"
#import "YZSettings.h"
#import "YZTheme.h"

#define YZ_OUTLINE_POINTS 240
#define YZ_MORPH_HOLD 1.45
#define YZ_MORPH_MOVE 0.55
#define YZ_MORPH_TURN 14.0

static NSString *const YZMorphPathKey = @"yozora.morph";
static NSString *const YZMorphTurnKey = @"yozora.turn";

// the outline's distance from the middle at an angle, the lobes reach in from the unit circle and the radius
// is scaled so the farthest point lies on it
static CGFloat YZLobeRadius(YZShape shape, CGFloat angle) {
    switch (shape) {
    case YZShapeCookie: return (1 + 0.06f * cosf(9 * angle)) / 1.06f;
    case YZShapeSunny: return (1 + 0.12f * cosf(8 * angle)) / 1.12f;
    case YZShapeClover: return (1 + 0.24f * cosf(4 * angle)) / 1.24f;
    case YZShapeFlower: return (1 + 0.2f * cosf(6 * angle)) / 1.2f;
    // sharp tips with rounded valleys: ten of them
    case YZShapeBurst: return 1 - 0.18f * fabsf(sinf(5 * angle));
    default: return 1;
    }
}

static UIBezierPath *YZBuildShapePath(YZShape shape, CGFloat side);

// an outline is 240 points of trigonometry and a laid out ball made six each time, so outlines are kept by
// shape and size
UIBezierPath *YZShapePath(YZShape shape, CGFloat side) {
    static NSMutableDictionary *cache;
    if (!cache)
        cache = [[NSMutableDictionary alloc] init];
    NSString *key = [NSString stringWithFormat:@"%d:%.2f", (int)shape, side];
    UIBezierPath *path = [cache objectForKey:key];
    if (!path) {
        // a bound on the outlines held: the sizes in use are a handful
        if ([cache count] > 60)
            [cache removeAllObjects];
        path = YZBuildShapePath(shape, side);
        [cache setObject:path forKey:key];
    }
    return path;
}

static UIBezierPath *YZBuildShapePath(YZShape shape, CGFloat side) {
    UIBezierPath *path = [UIBezierPath bezierPath];
    CGFloat half = side / 2;
    for (int i = 0; i < YZ_OUTLINE_POINTS; ++i) {
        CGFloat angle = 2 * (CGFloat)M_PI * i / YZ_OUTLINE_POINTS - (CGFloat)M_PI / 2;
        CGFloat radius = YZLobeRadius(shape, angle);
        CGPoint point = CGPointMake(half + radius * cosf(angle) * half, half + radius * sinf(angle) * half);
        if (i == 0)
            [path moveToPoint:point];
        else
            [path addLineToPoint:point];
    }
    [path closePath];
    return path;
}

static BOOL YZMorphPaused;

void YZShapeSetMorphPaused(BOOL paused) { YZMorphPaused = paused; }

void YZShapeStopMorph(CAShapeLayer *layer) {
    [layer removeAnimationForKey:YZMorphPathKey];
    [layer removeAnimationForKey:YZMorphTurnKey];
}

void YZShapeStartMorph(CAShapeLayer *layer, CGFloat side) {
    layer.path = YZShapePath(YZShapeCookie, side).CGPath;
    if (YZMorphPaused || ![YZTheme animationsEnabled] || [YZSettings reduceMotion]) {
        YZShapeStopMorph(layer);
        return;
    }
    if ([layer animationForKey:YZMorphPathKey])
        return;
    // one hold and one spring into the next shape for each of the shapes, round to the first again
    CFTimeInterval period = YZ_MORPH_HOLD + YZ_MORPH_MOVE, total = period * YZShapeCount;
    NSMutableArray *values = [NSMutableArray array], *times = [NSMutableArray array], *easing = [NSMutableArray array];
    CAMediaTimingFunction *spring = [CAMediaTimingFunction functionWithControlPoints:0.34f:1.45f:0.64f:1.0f];
    CAMediaTimingFunction *linear = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionLinear];
    for (int i = 0; i < YZShapeCount; ++i) {
        CGPathRef outline = YZShapePath((YZShape)i, side).CGPath;
        [values addObject:(id)outline];
        [times addObject:[NSNumber numberWithDouble:period * i / total]];
        [values addObject:(id)outline];
        [times addObject:[NSNumber numberWithDouble:(period * i + YZ_MORPH_HOLD) / total]];
        [easing addObject:linear];
        [easing addObject:spring];
    }
    [values addObject:(id)YZShapePath(YZShapeCookie, side).CGPath];
    [times addObject:[NSNumber numberWithDouble:1]];
    CAKeyframeAnimation *morph = [CAKeyframeAnimation animationWithKeyPath:@"path"];
    morph.values = values;
    morph.keyTimes = times;
    morph.timingFunctions = easing;
    morph.duration = total;
    morph.repeatCount = HUGE_VALF;
    morph.timeOffset = fmod(CACurrentMediaTime(), total);
    [layer addAnimation:morph forKey:YZMorphPathKey];
    CABasicAnimation *turn = [CABasicAnimation animationWithKeyPath:@"transform.rotation.z"];
    turn.fromValue = [NSNumber numberWithDouble:0];
    turn.toValue = [NSNumber numberWithDouble:M_PI * 2];
    turn.duration = YZ_MORPH_TURN;
    turn.repeatCount = HUGE_VALF;
    turn.timingFunction = linear;
    turn.timeOffset = fmod(CACurrentMediaTime(), YZ_MORPH_TURN);
    [layer addAnimation:turn forKey:YZMorphTurnKey];
}

@implementation YZMorphView
@synthesize color = _color;

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.backgroundColor = [UIColor clearColor];
    self.userInteractionEnabled = NO;
    _shape = [[CAShapeLayer alloc] init];
    [self.layer addSublayer:_shape];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(sync) name:UIApplicationDidBecomeActiveNotification object:nil];
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_shape removeFromSuperlayer];
    [_shape release];
    [_color release];
    [super dealloc];
}

- (void)setColor:(UIColor *)color {
    [color retain];
    [_color release];
    _color = color;
    _shape.fillColor = color.CGColor;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat side = MIN(self.bounds.size.width, self.bounds.size.height);
    // the layer turns about its middle, so it is placed by its center and the animation keeps the ball where
    // it is. a layout that changes nothing leaves the animation running, a list lays out rows while it
    // scrolls
    CGPoint middle = CGPointMake(CGRectGetMidX(self.bounds), CGRectGetMidY(self.bounds));
    if (fabs(side - _side) < 0.01 && CGPointEqualToPoint(middle, _shape.position)) {
        [self sync];
        return;
    }
    _side = side;
    YZShapeStopMorph(_shape);
    _shape.bounds = CGRectMake(0, 0, side, side);
    _shape.position = middle;
    [self sync];
}

- (void)sync {
    CGFloat side = MIN(self.bounds.size.width, self.bounds.size.height);
    if (!self.window || self.hidden || side < 1) {
        YZShapeStopMorph(_shape);
        return;
    }
    YZShapeStartMorph(_shape, side);
}

// a hidden ball in a reused cell must not keep animating
- (void)setHidden:(BOOL)hidden {
    [super setHidden:hidden];
    [self sync];
}

- (void)didMoveToWindow {
    [super didMoveToWindow];
    [self sync];
}
@end
