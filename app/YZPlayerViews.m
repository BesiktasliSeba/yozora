#import "YZPlayerViews.h"

#include <math.h>

#import "YZCompat.h"
#import "YZMotion.h"
#import "YZStrings.h"
#import "YZTheme.h"

static const CGFloat YZGlyphBox = 26;
static const CGFloat YZRowHeight = 44;
static const CGFloat YZMenuWidth = 124;

UIColor *YZPlayerInkOn(UIColor *color) {
    CGFloat c[4] = { 0, 0, 0, 1 };
    if (![color getRed:&c[0] green:&c[1] blue:&c[2] alpha:&c[3]])
        return [UIColor whiteColor];
    return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2] > 0.62f ? [UIColor colorWithWhite:0.1f alpha:1] : [UIColor whiteColor];
}

UIColor *YZPlayerAccent(void) {
    CGFloat c[4] = { 1, 1, 1, 1 };
    [[YZTheme accentColor] getRed:&c[0] green:&c[1] blue:&c[2] alpha:&c[3]];
    CGFloat lift = 0.38f;
    return [UIColor colorWithRed:c[0] + (1 - c[0]) * lift green:c[1] + (1 - c[1]) * lift blue:c[2] + (1 - c[2]) * lift alpha:1];
}

static CAMediaTimingFunction *YZPlayerSpring(void) {
    return [CAMediaTimingFunction functionWithControlPoints:0.34f:1.45f:0.64f:1.0f];
}

#pragma mark glyphs

static UIImage *YZDrawnImage(CGFloat side, void (^draw)(CGContextRef context)) {
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(side, side), NO, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGContextSetLineCap(context, kCGLineCapRound);
    CGContextSetLineJoin(context, kCGLineJoinRound);
    draw(context);
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return image;
}

UIImage *YZPlayerSkipImage(BOOL forward, CGFloat side, UIColor *color) {
    return YZDrawnImage(side, ^(CGContextRef context) {
        CGFloat radius = side * 0.34f, line = MAX((CGFloat)1.6f, side * 0.075f);
        CGPoint center = CGPointMake(side / 2, side / 2 + side * 0.03f);
        if (!forward) {
            // the backward arrow is the forward one turned over
            CGContextTranslateCTM(context, side, 0);
            CGContextScaleCTM(context, -1, 1);
        }
        CGContextSetStrokeColorWithColor(context, color.CGColor);
        CGContextSetFillColorWithColor(context, color.CGColor);
        CGContextSetLineWidth(context, line);
        // a clockwise circle with a gap at the top, 290 degrees, the arrow head in the gap
        const int steps = 48;
        for (int i = 0; i <= steps; ++i) {
            float angle = (float)((-55.0 + 290.0 * i / steps) * M_PI / 180.0);
            CGPoint point = CGPointMake(center.x + radius * cosf(angle), center.y + radius * sinf(angle));
            if (i == 0)
                CGContextMoveToPoint(context, point.x, point.y);
            else
                CGContextAddLineToPoint(context, point.x, point.y);
        }
        CGContextStrokePath(context);
        float end = (float)(235.0 * M_PI / 180.0);
        CGPoint tip = CGPointMake(center.x + radius * cosf(end), center.y + radius * sinf(end));
        CGPoint along = CGPointMake(-sinf(end), cosf(end));
        CGPoint across = CGPointMake(-along.y, along.x);
        CGFloat head = side * 0.17f;
        CGContextMoveToPoint(context, tip.x + along.x * head * 0.9f, tip.y + along.y * head * 0.9f);
        CGContextAddLineToPoint(context, tip.x - along.x * head * 0.35f + across.x * head, tip.y - along.y * head * 0.35f + across.y * head);
        CGContextAddLineToPoint(context, tip.x - along.x * head * 0.35f - across.x * head, tip.y - along.y * head * 0.35f - across.y * head);
        CGContextClosePath(context);
        CGContextFillPath(context);
        // the ten in the middle, set so it does not turn over with the arrow
        if (!forward) {
            CGContextScaleCTM(context, -1, 1);
            CGContextTranslateCTM(context, -side, 0);
        }
        UIFont *font = [YZTheme boldSystemFontOfSize:side * 0.3f];
        CGSize size = YZTextSize(@"10", font);
        YZDrawText(@"10", CGPointMake(center.x - size.width / 2, center.y - size.height / 2), font, color);
    });
}

UIImage *YZPlayerSpeakerImage(BOOL muted, CGFloat side, UIColor *color) {
    return YZDrawnImage(side, ^(CGContextRef context) {
        CGFloat u = side / 24;
        CGContextSetStrokeColorWithColor(context, color.CGColor);
        CGContextSetFillColorWithColor(context, color.CGColor);
        CGContextSetLineWidth(context, 2 * u);
        // the speaker body and its cone
        CGContextMoveToPoint(context, 3.5f * u, 9.5f * u);
        CGContextAddLineToPoint(context, 7.5f * u, 9.5f * u);
        CGContextAddLineToPoint(context, 12 * u, 5 * u);
        CGContextAddLineToPoint(context, 12 * u, 19 * u);
        CGContextAddLineToPoint(context, 7.5f * u, 14.5f * u);
        CGContextAddLineToPoint(context, 3.5f * u, 14.5f * u);
        CGContextClosePath(context);
        CGContextFillPath(context);
        if (muted) {
            CGContextMoveToPoint(context, 16 * u, 9.5f * u);
            CGContextAddLineToPoint(context, 21 * u, 14.5f * u);
            CGContextMoveToPoint(context, 21 * u, 9.5f * u);
            CGContextAddLineToPoint(context, 16 * u, 14.5f * u);
            CGContextStrokePath(context);
            return;
        }
        // two waves on the right of the cone, arcs of 90 degrees centered on the horizontal
        for (NSNumber *wave in [NSArray arrayWithObjects:[NSNumber numberWithDouble:5], [NSNumber numberWithDouble:9], nil]) {
            UIBezierPath *arc = [UIBezierPath bezierPathWithArcCenter:CGPointMake(12 * u, 12 * u) radius:[wave doubleValue] * u
                                                           startAngle:-(CGFloat)(M_PI / 4) endAngle:(CGFloat)(M_PI / 4) clockwise:YES];
            arc.lineWidth = 2 * u;
            arc.lineCapStyle = kCGLineCapRound;
            [color setStroke];
            [arc stroke];
        }
    });
}

UIImage *YZPlayerAspectImage(BOOL filled, CGFloat side, UIColor *color) {
    return YZDrawnImage(side, ^(CGContextRef context) {
        CGFloat u = side / 24;
        CGContextSetStrokeColorWithColor(context, color.CGColor);
        CGContextSetLineWidth(context, 2.2f * u);
        // filled: corners at the edge of the picture and open outward; fit: corners pulled into the frame
        CGFloat edge = filled ? 3 * u : 6 * u, arm = filled ? 6 * u : 4 * u, far = side - edge;
        CGContextMoveToPoint(context, edge, edge + arm);
        CGContextAddLineToPoint(context, edge, edge);
        CGContextAddLineToPoint(context, edge + arm, edge);
        CGContextMoveToPoint(context, far - arm, edge);
        CGContextAddLineToPoint(context, far, edge);
        CGContextAddLineToPoint(context, far, edge + arm);
        CGContextMoveToPoint(context, far, far - arm);
        CGContextAddLineToPoint(context, far, far);
        CGContextAddLineToPoint(context, far - arm, far);
        CGContextMoveToPoint(context, edge + arm, far);
        CGContextAddLineToPoint(context, edge, far);
        CGContextAddLineToPoint(context, edge, far - arm);
        CGContextStrokePath(context);
    });
}

#pragma mark scrim

@implementation YZScrimView
+ (Class)layerClass { return [CAGradientLayer class]; }

- (id)initWithFrame:(CGRect)frame fromTop:(BOOL)fromTop {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.userInteractionEnabled = NO;
    self.backgroundColor = [UIColor clearColor];
    CAGradientLayer *gradient = (CAGradientLayer *)self.layer;
    UIColor *dark = [UIColor colorWithWhite:0 alpha:0.62f];
    UIColor *clear = [UIColor colorWithWhite:0 alpha:0];
    gradient.colors = fromTop ? [NSArray arrayWithObjects:(id)dark.CGColor, (id)clear.CGColor, nil]
                              : [NSArray arrayWithObjects:(id)clear.CGColor, (id)dark.CGColor, nil];
    return self;
}
@end

#pragma mark play button

@implementation YZPlayButton
@synthesize playing = _playing;
@synthesize busy = _busy;

// each piece is four points: the play triangle cut in two, and the two bars. a piece keeps its points across
// the shapes so the morph is a plain interpolation
static CGPathRef YZPiecePath(BOOL left, BOOL playing) {
    CGFloat w = YZGlyphBox - 4, h = YZGlyphBox - 4, ox = 2 + (playing ? 0 : 2), oy = 2;
    CGPoint p[4];
    if (left && !playing) { p[0] = CGPointMake(0, 0); p[1] = CGPointMake(0.5f * w, 0.25f * h); p[2] = CGPointMake(0.5f * w, 0.75f * h); p[3] = CGPointMake(0, h); }
    else if (left) { p[0] = CGPointMake(0.04f * w, 0); p[1] = CGPointMake(0.36f * w, 0); p[2] = CGPointMake(0.36f * w, h); p[3] = CGPointMake(0.04f * w, h); }
    else if (!playing) { p[0] = CGPointMake(0.5f * w, 0.25f * h); p[1] = CGPointMake(w, 0.5f * h); p[2] = CGPointMake(w, 0.5f * h); p[3] = CGPointMake(0.5f * w, 0.75f * h); }
    else { p[0] = CGPointMake(0.64f * w, 0); p[1] = CGPointMake(0.96f * w, 0); p[2] = CGPointMake(0.96f * w, h); p[3] = CGPointMake(0.64f * w, h); }
    CGMutablePathRef path = CGPathCreateMutable();
    CGPathMoveToPoint(path, NULL, ox + p[0].x, oy + p[0].y);
    for (int i = 1; i < 4; ++i)
        CGPathAddLineToPoint(path, NULL, ox + p[i].x, oy + p[i].y);
    CGPathCloseSubpath(path);
    return (CGPathRef)[(id)path autorelease];
}

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.backgroundColor = [UIColor clearColor];
    self.isAccessibilityElement = YES;
    self.accessibilityTraits = UIAccessibilityTraitButton;
    _body = [[CALayer alloc] init];
    [self.layer addSublayer:_body];
    _left = [[CAShapeLayer alloc] init];
    _right = [[CAShapeLayer alloc] init];
    for (CAShapeLayer *piece in [NSArray arrayWithObjects:_left, _right, nil]) {
        piece.bounds = CGRectMake(0, 0, YZGlyphBox, YZGlyphBox);
        // a stroke in the fill color rounds the corners of the glyph
        piece.lineWidth = 4;
        piece.lineJoin = kCALineJoinRound;
        [self.layer addSublayer:piece];
    }
    _ring = [[CAShapeLayer alloc] init];
    _ring.fillColor = nil;
    _ring.lineWidth = 3;
    _ring.lineCap = kCALineCapRound;
    _ring.strokeEnd = 0.28f;
    _ring.opacity = 0;
    [self.layer addSublayer:_ring];
    _left.path = YZPiecePath(YES, NO);
    _right.path = YZPiecePath(NO, NO);
    [self applyTheme];
    return self;
}

- (void)dealloc {
    [_body release];
    [_left release];
    [_right release];
    [_ring release];
    [super dealloc];
}

- (void)applyTheme {
    UIColor *fill = [YZTheme accentColor];
    UIColor *ink = YZPlayerInkOn(fill);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _body.backgroundColor = fill.CGColor;
    for (CAShapeLayer *piece in [NSArray arrayWithObjects:_left, _right, nil]) {
        piece.fillColor = ink.CGColor;
        piece.strokeColor = ink.CGColor;
    }
    _ring.strokeColor = ink.CGColor;
    [CATransaction commit];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect bounds = self.bounds;
    CGFloat side = MIN(bounds.size.width, bounds.size.height);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _body.bounds = CGRectMake(0, 0, side, side);
    _body.position = CGPointMake(CGRectGetMidX(bounds), CGRectGetMidY(bounds));
    _body.cornerRadius = _playing ? side / 2 : side * 0.32f;
    for (CAShapeLayer *piece in [NSArray arrayWithObjects:_left, _right, nil])
        piece.position = _body.position;
    CGFloat ringSide = side - 14;
    _ring.bounds = CGRectMake(0, 0, ringSide, ringSide);
    _ring.position = _body.position;
    _ring.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(0, 0, ringSide, ringSide)].CGPath;
    [CATransaction commit];
}

- (void)setPlaying:(BOOL)playing { [self setPlaying:playing animated:NO]; }

// the glyph turns on the spring while the body rounds into a circle (and back to a rounded square when it pauses)
- (void)setPlaying:(BOOL)playing animated:(BOOL)animated {
    if (playing == _playing)
        return;
    _playing = playing;
    self.accessibilityLabel = playing ? YZT(@"Пауза") : YZT(@"Воспроизвести");
    CGFloat side = MIN(self.bounds.size.width, self.bounds.size.height);
    CGFloat radius = playing ? side / 2 : side * 0.32f;
    BOOL moving = animated && self.window && [YZTheme animationsEnabled];
    NSArray *pieces = [NSArray arrayWithObjects:_left, _right, nil];
    CGPathRef targets[2] = { YZPiecePath(YES, playing), YZPiecePath(NO, playing) };
    for (NSUInteger i = 0; i < 2; ++i) {
        CAShapeLayer *piece = [pieces objectAtIndex:i];
        CAShapeLayer *shown = (CAShapeLayer *)[piece presentationLayer];
        CGPathRef from = moving && shown && shown.path ? shown.path : piece.path;
        CGPathRetain(from);
        [piece removeAnimationForKey:@"yz.glyph"];
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        piece.path = targets[i];
        _body.cornerRadius = radius;
        [CATransaction commit];
        if (moving && from) {
            CABasicAnimation *morph = [CABasicAnimation animationWithKeyPath:@"path"];
            morph.fromValue = (id)from;
            morph.toValue = (id)targets[i];
            morph.duration = 0.34;
            morph.timingFunction = YZPlayerSpring();
            [piece addAnimation:morph forKey:@"yz.glyph"];
        }
        CGPathRelease(from);
    }
    if (moving) {
        CABasicAnimation *round = [CABasicAnimation animationWithKeyPath:@"cornerRadius"];
        round.fromValue = [NSNumber numberWithDouble:(double)(playing ? side * 0.32f : side / 2)];
        round.toValue = [NSNumber numberWithDouble:(double)radius];
        round.duration = 0.34;
        round.timingFunction = YZPlayerSpring();
        [_body addAnimation:round forKey:@"yz.round"];
    }
}

- (void)setBusy:(BOOL)busy {
    if (busy == _busy)
        return;
    _busy = busy;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _ring.opacity = busy ? 1 : 0;
    [CATransaction commit];
    [_ring removeAnimationForKey:@"yz.turn"];
    if (busy && self.window && [YZTheme animationsEnabled]) {
        CABasicAnimation *turn = [CABasicAnimation animationWithKeyPath:@"transform.rotation.z"];
        turn.fromValue = [NSNumber numberWithDouble:0];
        turn.toValue = [NSNumber numberWithDouble:M_PI * 2];
        turn.duration = 1.1;
        turn.repeatCount = HUGE_VALF;
        turn.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionLinear];
        [_ring addAnimation:turn forKey:@"yz.turn"];
    }
}

// a view that leaves the window loses its animations, the ring starts again when it returns
- (void)didMoveToWindow {
    [super didMoveToWindow];
    if (self.window && _busy) {
        _busy = NO;
        self.busy = YES;
    }
}
@end

#pragma mark speed menu

@interface YZSpeedMenu ()
- (void)rowTapped:(UIButton *)row;
@end

@implementation YZSpeedMenu

+ (NSArray *)rates {
    return [NSArray arrayWithObjects:[NSNumber numberWithDouble:0.5], [NSNumber numberWithDouble:0.75], [NSNumber numberWithDouble:1],
            [NSNumber numberWithDouble:1.25], [NSNumber numberWithDouble:1.5], [NSNumber numberWithDouble:2], nil];
}

+ (CGSize)menuSize { return CGSizeMake(YZMenuWidth, YZRowHeight * [[self rates] count] + 16); }

static NSString *YZRateTitle(double rate) {
    NSString *text = [NSString stringWithFormat:@"%g", rate];
    return [text stringByAppendingString:@"×"];
}

- (id)initWithSelectedRate:(double)rate choose:(void (^)(double))choose {
    self = [super initWithFrame:CGRectMake(0, 0, [YZSpeedMenu menuSize].width, [YZSpeedMenu menuSize].height)];
    if (!self)
        return nil;
    _selected = rate;
    _choose = [choose copy];
    self.backgroundColor = [UIColor colorWithWhite:0.1f alpha:0.94f];
    self.layer.cornerRadius = 22;
    _rows = [[NSMutableArray alloc] init];
    NSArray *rates = [YZSpeedMenu rates];
    for (NSUInteger i = 0; i < [rates count]; ++i) {
        double value = [[rates objectAtIndex:i] doubleValue];
        BOOL chosen = fabs(value - rate) < 0.01;
        UIButton *row = [UIButton buttonWithType:UIButtonTypeCustom];
        row.tag = (NSInteger)i;
        row.frame = CGRectMake(8, 8 + i * YZRowHeight, YZMenuWidth - 16, YZRowHeight);
        row.layer.cornerRadius = 16;
        row.backgroundColor = chosen ? [YZPlayerAccent() colorWithAlphaComponent:0.28f] : [UIColor clearColor];
        row.titleLabel.font = [YZTheme boldSystemFontOfSize:16];
        [row setTitle:YZRateTitle(value) forState:UIControlStateNormal];
        [row setTitleColor:chosen ? YZPlayerAccent() : [UIColor whiteColor] forState:UIControlStateNormal];
        row.accessibilityLabel = YZRateTitle(value);
        [row addTarget:self action:@selector(rowTapped:) forControlEvents:UIControlEventTouchUpInside];
        YZMotionAttachBounce(row, 0.05f);
        [self addSubview:row];
        [_rows addObject:row];
    }
    return self;
}

- (void)dealloc {
    [_rows release];
    [_choose release];
    [super dealloc];
}

- (void)rowTapped:(UIButton *)row {
    if (_choose)
        _choose([[[YZSpeedMenu rates] objectAtIndex:(NSUInteger)row.tag] doubleValue]);
}
@end
