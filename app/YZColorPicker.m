#import "YZColorPicker.h"
#import "YZCompat.h"
#import "YZTheme.h"
#include "yozora_color.h"
#import <QuartzCore/QuartzCore.h>

static const CGFloat YZTrackHeight = 28;
static const CGFloat YZCardHeight = 76;
static const CGFloat YZColorMargin = 20;

static UIColor *YZColorFrom(float hue, float saturation, float value) {
    uint8_t rgb[3];
    yozora_color_hsv_to_rgb(hue, saturation, value, rgb);
    return [UIColor colorWithRed:rgb[0] / 255.0f green:rgb[1] / 255.0f blue:rgb[2] / 255.0f alpha:1];
}

// a pill with a gradient in it and a round handle that follows the finger
@interface YZColorTrack : UIControl {
    CAGradientLayer *_gradient;
    UIView *_handle;
    float _fraction;
}
@property(nonatomic, assign) float fraction;
- (void)setStops:(NSArray *)colors;
- (void)setHandleColor:(UIColor *)color;
@end

@implementation YZColorTrack
@synthesize fraction = _fraction;

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.backgroundColor = [UIColor clearColor];
    _gradient = [[CAGradientLayer layer] retain];
    _gradient.startPoint = CGPointMake(0, 0.5f);
    _gradient.endPoint = CGPointMake(1, 0.5f);
    _gradient.cornerRadius = YZTrackHeight / 2;
    [self.layer addSublayer:_gradient];
    _handle = [[UIView alloc] initWithFrame:CGRectMake(0, 0, YZTrackHeight + 4, YZTrackHeight + 4)];
    _handle.layer.cornerRadius = (YZTrackHeight + 4) / 2;
    _handle.layer.borderWidth = 3;
    _handle.layer.borderColor = [[UIColor whiteColor] CGColor];
    _handle.layer.shadowColor = [[UIColor blackColor] CGColor];
    _handle.layer.shadowOpacity = 0.35f;
    _handle.layer.shadowRadius = 2;
    _handle.layer.shadowOffset = CGSizeMake(0, 1);
    _handle.userInteractionEnabled = NO;
    [self addSubview:_handle];
    return self;
}

- (void)dealloc {
    [_gradient release];
    [_handle release];
    [super dealloc];
}

- (void)setStops:(NSArray *)colors {
    NSMutableArray *cg = [NSMutableArray array];
    for (UIColor *color in colors)
        [cg addObject:(id)[color CGColor]];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _gradient.colors = cg;
    [CATransaction commit];
}

- (void)setHandleColor:(UIColor *)color { _handle.backgroundColor = color; }

- (void)setFraction:(float)fraction {
    _fraction = fraction < 0 ? 0 : (fraction > 1 ? 1 : fraction);
    [self setNeedsLayout];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width, height = self.bounds.size.height;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _gradient.frame = CGRectMake(0, (height - YZTrackHeight) / 2, width, YZTrackHeight);
    [CATransaction commit];
    // the handle stays inside the pill at both ends
    CGFloat travel = MAX((CGFloat)0, width - YZTrackHeight);
    _handle.center = CGPointMake(YZTrackHeight / 2 + travel * _fraction, height / 2);
}

- (BOOL)moveTo:(UITouch *)touch {
    CGFloat travel = MAX((CGFloat)1, self.bounds.size.width - YZTrackHeight);
    CGFloat x = [touch locationInView:self].x - YZTrackHeight / 2;
    self.fraction = (float)(x / travel);
    [self sendActionsForControlEvents:UIControlEventValueChanged];
    return YES;
}

- (BOOL)beginTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event { (void)event; return [self moveTo:touch]; }
- (BOOL)continueTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event { (void)event; return [self moveTo:touch]; }
@end

@implementation YZColorPicker

+ (CGFloat)preferredHeight { return YZColorMargin + YZCardHeight + 3 * (YZTrackHeight + 20) + 8; }

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    _card = [[UIView alloc] initWithFrame:CGRectZero];
    _card.layer.cornerRadius = [YZTheme isClassic] ? 10 : 24;
    [self addSubview:_card];
    _hex = [[UILabel alloc] initWithFrame:CGRectZero];
    _hex.font = [YZTheme boldSystemFontOfSize:22];
    _hex.textAlignment = YZTextAlignmentCenter;
    _hex.backgroundColor = [UIColor clearColor];
    [_card addSubview:_hex];
    NSMutableArray *tracks = [NSMutableArray array];
    for (int i = 0; i < 3; ++i) {
        YZColorTrack *track = [[[YZColorTrack alloc] initWithFrame:CGRectZero] autorelease];
        [track addTarget:self action:@selector(trackMoved:) forControlEvents:UIControlEventValueChanged];
        [self addSubview:track];
        [tracks addObject:track];
    }
    _tracks = [tracks retain];
    _saturation = 1;
    _value = 1;
    [self refresh];
    return self;
}

- (void)dealloc {
    [_card release];
    [_hex release];
    [_tracks release];
    [super dealloc];
}

- (NSString *)hex {
    uint8_t rgb[3];
    char text[8];
    yozora_color_hsv_to_rgb(_hue, _saturation, _value, rgb);
    yozora_color_format_hex(rgb, text);
    return [NSString stringWithUTF8String:text];
}

- (void)setHex:(NSString *)hex {
    uint8_t rgb[3];
    if (!yozora_color_parse_hex([hex UTF8String], [hex length], rgb))
        return;
    yozora_color_rgb_to_hsv(rgb, &_hue, &_saturation, &_value);
    [self refresh];
}

- (void)trackMoved:(YZColorTrack *)track {
    NSUInteger index = [_tracks indexOfObject:track];
    if (index == 0)
        _hue = track.fraction * 360;
    else if (index == 1)
        _saturation = track.fraction;
    else
        _value = track.fraction;
    [self refresh];
    [self sendActionsForControlEvents:UIControlEventValueChanged];
}

// the card, the digits and the tracks show the color. a track draws what its value would give with the other two as they are
- (void)refresh {
    UIColor *color = YZColorFrom(_hue, _saturation, _value);
    _card.backgroundColor = color;
    _hex.text = [self hex];
    // dark digits on a light color and light digits on a dark one
    uint8_t rgb[3];
    yozora_color_hsv_to_rgb(_hue, _saturation, _value, rgb);
    BOOL light = (0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2]) > 150;
    _hex.textColor = light ? [UIColor colorWithWhite:0.1f alpha:1] : [UIColor whiteColor];
    NSMutableArray *rainbow = [NSMutableArray array];
    for (int i = 0; i <= 6; ++i)
        [rainbow addObject:YZColorFrom(i * 60.0f, 1, 1)];
    YZColorTrack *hue = [_tracks objectAtIndex:0], *saturation = [_tracks objectAtIndex:1], *value = [_tracks objectAtIndex:2];
    [hue setStops:rainbow];
    [saturation setStops:[NSArray arrayWithObjects:YZColorFrom(_hue, 0, _value), YZColorFrom(_hue, 1, _value), nil]];
    [value setStops:[NSArray arrayWithObjects:YZColorFrom(_hue, _saturation, 0), YZColorFrom(_hue, _saturation, 1), nil]];
    hue.fraction = _hue / 360;
    saturation.fraction = _saturation;
    value.fraction = _value;
    [hue setHandleColor:YZColorFrom(_hue, 1, 1)];
    [saturation setHandleColor:color];
    [value setHandleColor:color];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width;
    _card.frame = CGRectMake(YZColorMargin, 4, width - 2 * YZColorMargin, YZCardHeight);
    _hex.frame = _card.bounds;
    CGFloat y = 4 + YZCardHeight + 16;
    for (YZColorTrack *track in _tracks) {
        track.frame = CGRectMake(YZColorMargin - 2, y, width - 2 * YZColorMargin + 4, YZTrackHeight + 20);
        y += YZTrackHeight + 20;
    }
}
@end
