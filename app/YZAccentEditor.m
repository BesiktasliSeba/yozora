#import "YZAccentEditor.h"
#import "YZShape.h"
#import "YZControls.h"

#import <QuartzCore/QuartzCore.h>
#include <math.h>

#import "YZStrings.h"
#import "YZTheme.h"
#include "yozora_accent.h"

#define EDITOR_HEIGHT 444.0
#define MARGIN 16.0
#define CARD_HEIGHT 112.0
#define CARD_RADIUS ([YZTheme isClassic] ? 10.0 : 30.0)
#define TRACK_HEIGHT 28.0
#define THUMB_SIDE 38.0
#define SLIDER_ROW 40.0
#define CHIP_SIDE 44.0
#define CHIP_GAP 12.0
#define LOBES 10

// the ready colors: the purple of the palette first, a grey last (a grey has no hue, the saturation 0 says it)
static const struct { float hue, saturation; } YZAccentPresets[] = {
    {286, 0.60f}, {337, 0.80f}, {2, 0.75f}, {24, 0.82f}, {42, 0.85f}, {135, 0.58f}, {174, 0.70f}, {212, 0.76f}, {252, 0.62f}, {0, 0}
};
#define PRESET_COUNT (sizeof(YZAccentPresets) / sizeof(YZAccentPresets[0]))

// the color toward white (t > 0) or toward black (t < 0)
static UIColor *YZShade(UIColor *color, CGFloat t) {
    CGFloat c[4] = { 0, 0, 0, 1 };
    [color getRed:&c[0] green:&c[1] blue:&c[2] alpha:&c[3]];
    CGFloat target = t > 0 ? 1 : 0, amount = fabs(t);
    return [UIColor colorWithRed:c[0] + (target - c[0]) * amount green:c[1] + (target - c[1]) * amount blue:c[2] + (target - c[2]) * amount alpha:1];
}

static UIColor *YZAccentColor(float hue, float saturation, float brightness) {
    yozora_accent accent = { hue, saturation, brightness };
    float r, g, b;
    yozora_accent_rgb(&accent, &r, &g, &b);
    return [UIColor colorWithRed:r green:g blue:b alpha:1];
}

// the colors of a track: at full value, which is the color of the accent the chips show
static UIColor *YZAccentShown(float hue, float saturation) {
    return YZAccentColor(hue, saturation, 1);
}

static NSString *YZAccentHex(float hue, float saturation, float brightness) {
    yozora_accent accent = { hue, saturation, brightness };
    char hex[7];
    yozora_accent_hex(&accent, hex);
    return [NSString stringWithUTF8String:hex];
}

static BOOL YZAccentHexAllowed(NSString *text) {
    if (text.length > 6)
        return NO;
    NSCharacterSet *bad = [[NSCharacterSet characterSetWithCharactersInString:@"0123456789ABCDEFabcdef"] invertedSet];
    return [text rangeOfCharacterFromSet:bad].location == NSNotFound;
}

static UIImage *YZAccentCheckImage(void) {
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(22, 22), NO, 0);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGContextSetStrokeColorWithColor(ctx, [UIColor whiteColor].CGColor);
    CGContextSetLineWidth(ctx, 2.5f);
    CGContextSetLineCap(ctx, kCGLineCapRound);
    CGContextSetLineJoin(ctx, kCGLineJoinRound);
    CGContextMoveToPoint(ctx, 5, 11);
    CGContextAddLineToPoint(ctx, 9, 15);
    CGContextAddLineToPoint(ctx, 17, 7);
    CGContextStrokePath(ctx);
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return image;
}

// a spring that passes its target and settles, like the tab switch, still when motion is reduced
static void YZAccentSpring(void (^changes)(void)) {
    [CATransaction begin];
    if (![YZTheme animationsEnabled]) {
        [CATransaction setDisableActions:YES];
    } else {
        [CATransaction setAnimationDuration:0.3];
        [CATransaction setAnimationTimingFunction:[CAMediaTimingFunction functionWithControlPoints:0.34f:1.45f:0.64f:1.0f]];
    }
    changes();
    [CATransaction commit];
}

@implementation YZAccentTrack
@synthesize fraction = _fraction;
@synthesize spokenName = _spokenName;

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.backgroundColor = [UIColor clearColor];
    self.isAccessibilityElement = YES;
    self.accessibilityTraits = UIAccessibilityTraitAdjustable;
    _track = [[CAGradientLayer alloc] init];
    _track.startPoint = CGPointMake(0, 0.5f);
    _track.endPoint = CGPointMake(1, 0.5f);
    _track.cornerRadius = TRACK_HEIGHT / 2;
    [self.layer addSublayer:_track];
    _handle = [[CALayer alloc] init];
    _handle.bounds = CGRectMake(0, 0, THUMB_SIDE, THUMB_SIDE);
    _handle.cornerRadius = THUMB_SIDE / 2;
    _handle.borderWidth = 4;
    _handle.borderColor = [UIColor whiteColor].CGColor;
    _handle.shadowColor = [UIColor blackColor].CGColor;
    _handle.shadowOpacity = 0.3f;
    _handle.shadowRadius = 4;
    _handle.shadowOffset = CGSizeMake(0, 2);
    [self.layer addSublayer:_handle];
    return self;
}

- (void)dealloc {
    [_track release];
    [_handle release];
    [_spokenName release];
    [super dealloc];
}

- (CGFloat)travel { return MAX((CGFloat)1, self.bounds.size.width - THUMB_SIDE); }

- (void)place {
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    CGSize size = self.bounds.size;
    _track.frame = CGRectMake(0, (size.height - TRACK_HEIGHT) / 2, size.width, TRACK_HEIGHT);
    _handle.position = CGPointMake(THUMB_SIDE / 2 + _fraction * [self travel], size.height / 2);
    [CATransaction commit];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    [self place];
}

- (void)setFraction:(float)fraction {
    _fraction = MIN(1, MAX(0, fraction));
    [self place];
}

- (void)setColors:(NSArray *)colors {
    NSMutableArray *refs = [NSMutableArray array];
    for (UIColor *color in colors)
        [refs addObject:(id)color.CGColor];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _track.colors = refs;
    [CATransaction commit];
}

- (void)setHandleColor:(UIColor *)color {
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _handle.backgroundColor = color.CGColor;
    [CATransaction commit];
}

- (void)swell:(BOOL)held {
    YZAccentSpring(^{ _handle.transform = held ? CATransform3DMakeScale(1.18f, 1.18f, 1) : CATransform3DIdentity; });
}

- (void)moveToTouch:(UITouch *)touch {
    float before = _fraction;
    self.fraction = (float)(([touch locationInView:self].x - THUMB_SIDE / 2) / [self travel]);
    if (_fraction != before)
        [self sendActionsForControlEvents:UIControlEventValueChanged];
}

- (BOOL)beginTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event {
    [self swell:YES];
    [self moveToTouch:touch];
    return YES;
}

- (BOOL)continueTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event {
    [self moveToTouch:touch];
    return YES;
}

- (void)endTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event {
    [self swell:NO];
    [self sendActionsForControlEvents:UIControlEventTouchUpInside];
}

- (void)cancelTrackingWithEvent:(UIEvent *)event {
    [self swell:NO];
    [self sendActionsForControlEvents:UIControlEventTouchUpInside];
}

// the bar is 40 points tall, a finger needs 44
- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event {
    return CGRectContainsPoint(CGRectInset(self.bounds, -8, -MAX((CGFloat)0, (44 - self.bounds.size.height) / 2)), point);
}

- (NSString *)accessibilityLabel { return _spokenName; }
- (NSString *)accessibilityValue { return [NSString stringWithFormat:@"%.0f%%", _fraction * 100]; }

- (void)stepBy:(float)step {
    self.fraction = _fraction + step;
    [self sendActionsForControlEvents:UIControlEventValueChanged];
    [self sendActionsForControlEvents:UIControlEventTouchUpInside];
}

- (void)accessibilityIncrement { [self stepBy:0.05f]; }
- (void)accessibilityDecrement { [self stepBy:-0.05f]; }
@end

@implementation YZAccentEditor
@synthesize hue = _hue;
@synthesize saturation = _saturation;
@synthesize brightness = _brightness;
@synthesize morphing = _morphing;

- (void)setMorphing:(BOOL)morphing {
    if (morphing == _morphing)
        return;
    _morphing = morphing;
    [[NSNotificationCenter defaultCenter] removeObserver:self name:UIApplicationDidBecomeActiveNotification object:nil];
    if (morphing) {
        // Core Animation drops the running animations while the app is hidden
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(refresh) name:UIApplicationDidBecomeActiveNotification object:nil];
    } else {
        YZShapeStopMorph(_cookie);
        _cookie.path = YZAccentCookiePath(34);
    }
    [self refresh];
}

- (void)didMoveToWindow {
    [super didMoveToWindow];
    if (_morphing)
        [self refresh];
}

- (CGFloat)preferredHeight { return EDITOR_HEIGHT; }
- (CGRect)editingRect { return _card.frame; }

// a ten lobed disc behind the digits, turned by the hue
static CGPathRef YZAccentCookiePath(CGFloat radius) {
    CGMutablePathRef path = CGPathCreateMutable();
    int steps = 120;
    for (int i = 0; i <= steps; ++i) {
        CGFloat angle = (CGFloat)i / steps * (CGFloat)(M_PI * 2);
        CGFloat r = radius * (1 + 0.09f * cosf((float)(angle * LOBES)));
        CGPoint point = CGPointMake(radius + r * cosf((float)angle), radius + r * sinf((float)angle));
        if (i == 0)
            CGPathMoveToPoint(path, NULL, point.x, point.y);
        else
            CGPathAddLineToPoint(path, NULL, point.x, point.y);
    }
    CGPathCloseSubpath(path);
    return (CGPathRef)[(id)path autorelease];
}

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.backgroundColor = [UIColor clearColor];
    _chosenChip = -1;
    _brightness = 1;
    _card = [[UIView alloc] init];
    _card.layer.cornerRadius = CARD_RADIUS;
    [self addSubview:_card];
    _cookie = [[CAShapeLayer alloc] init];
    _cookie.path = YZAccentCookiePath(34);
    _cookie.bounds = CGRectMake(0, 0, 68, 68);
    [_card.layer addSublayer:_cookie];
    _hash = [[UILabel alloc] init];
    _hash.text = @"#";
    _hash.font = [YZTheme boldSystemFontOfSize:32];
    _hash.backgroundColor = [UIColor clearColor];
    [_card addSubview:_hash];
    _note = [[UILabel alloc] init];
    _note.text = YZT(@"Цвет акцента");
    _note.font = [YZTheme boldSystemFontOfSize:15];
    _note.backgroundColor = [UIColor clearColor];
    [_card addSubview:_note];
    _field = [[UITextField alloc] init];
    _field.delegate = self;
    _field.backgroundColor = [UIColor clearColor];
    _field.font = [YZTheme boldSystemFontOfSize:32];
    _field.contentVerticalAlignment = UIControlContentVerticalAlignmentCenter;
    _field.autocapitalizationType = UITextAutocapitalizationTypeAllCharacters;
    _field.autocorrectionType = UITextAutocorrectionTypeNo;
    _field.keyboardType = UIKeyboardTypeASCIICapable;
    _field.returnKeyType = UIReturnKeyDone;
    _field.accessibilityLabel = YZT(@"Цвет в HEX");
    [_field addTarget:self action:@selector(hexEdited) forControlEvents:UIControlEventEditingChanged];
    [_card addSubview:_field];
    _hueName = [[UILabel alloc] init];
    _hueName.text = YZT(@"Оттенок");
    _saturationName = [[UILabel alloc] init];
    _saturationName.text = YZT(@"Насыщенность");
    _brightnessName = [[UILabel alloc] init];
    _brightnessName.text = YZT(@"Яркость");
    for (UILabel *label in [NSArray arrayWithObjects:_hueName, _saturationName, _brightnessName, nil]) {
        label.font = [YZTheme boldSystemFontOfSize:13];
        label.backgroundColor = [UIColor clearColor];
        [self addSubview:label];
    }
    YZAccentTrack *hueTrack = [[YZAccentTrack alloc] init];
    hueTrack.spokenName = _hueName.text;
    [hueTrack addTarget:self action:@selector(hueMoved) forControlEvents:UIControlEventValueChanged];
    [hueTrack addTarget:self action:@selector(released) forControlEvents:UIControlEventTouchUpInside];
    NSMutableArray *rainbow = [NSMutableArray array];
    for (int i = 0; i <= 6; ++i)
        [rainbow addObject:YZAccentShown(i * 60.0f, 0.78f)];
    [hueTrack setColors:rainbow];
    [self addSubview:hueTrack];
    _hueTrack = hueTrack;
    YZAccentTrack *saturationTrack = [[YZAccentTrack alloc] init];
    saturationTrack.spokenName = _saturationName.text;
    [saturationTrack addTarget:self action:@selector(saturationMoved) forControlEvents:UIControlEventValueChanged];
    [saturationTrack addTarget:self action:@selector(released) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:saturationTrack];
    _saturationTrack = saturationTrack;
    YZAccentTrack *brightnessTrack = [[YZAccentTrack alloc] init];
    brightnessTrack.spokenName = _brightnessName.text;
    [brightnessTrack addTarget:self action:@selector(brightnessMoved) forControlEvents:UIControlEventValueChanged];
    [brightnessTrack addTarget:self action:@selector(released) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:brightnessTrack];
    _brightnessTrack = brightnessTrack;

    _chips = [[YZScrollView alloc] init];
    _chips.showsHorizontalScrollIndicator = NO;
    _chips.showsVerticalScrollIndicator = NO;
    _chips.scrollsToTop = NO;
    [self addSubview:_chips];
    _chipButtons = [[NSMutableArray alloc] init];
    for (NSUInteger i = 0; i < PRESET_COUNT; ++i) {
        UIButton *chip = [UIButton buttonWithType:UIButtonTypeCustom];
        chip.tag = (NSInteger)i;
        chip.layer.cornerRadius = CHIP_SIDE / 2;
        chip.backgroundColor = YZAccentShown(YZAccentPresets[i].hue, YZAccentPresets[i].saturation);
        chip.accessibilityLabel = YZAccentHex(YZAccentPresets[i].hue, YZAccentPresets[i].saturation, 1);
        [chip setImage:YZAccentCheckImage() forState:UIControlStateSelected];
        [chip addTarget:self action:@selector(chipTapped:) forControlEvents:UIControlEventTouchUpInside];
        [_chips addSubview:chip];
        [_chipButtons addObject:chip];
    }
    [self applyTheme];
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _field.delegate = nil;
    [_card release];
    [_cardGradient removeFromSuperlayer];
    [_cardGradient release];
    [_cookie release];
    [_hash release];
    [_note release];
    [_field release];
    [_hueName release];
    [_saturationName release];
    [_brightnessName release];
    [_hueTrack release];
    [_saturationTrack release];
    [_brightnessTrack release];
    [_chips release];
    [_chipButtons release];
    [super dealloc];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width, inner = width - 2 * MARGIN;
    CGFloat y = 12;
    _card.frame = CGRectMake(MARGIN, y, inner, CARD_HEIGHT);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _cardGradient.frame = _card.bounds;
    [CATransaction commit];
    // the field is as wide as the widest six digits, so it does not jump while the color changes
    CGFloat digits = 0;
    for (NSString *sample in [NSArray arrayWithObjects:@"000000", @"888888", @"FFFFFF", @"BBBBBB", nil]) {
        UILabel *probe = [[[UILabel alloc] init] autorelease];
        probe.font = _field.font;
        probe.text = sample;
        [probe sizeToFit];
        digits = MAX(digits, probe.frame.size.width);
    }
    [_hash sizeToFit];
    CGFloat hashWidth = _hash.frame.size.width;
    _hash.frame = CGRectMake(24, 18, hashWidth, 44);
    _field.frame = CGRectMake(24 + hashWidth + 1, 18, ceil(digits) + 6, 44);
    _note.frame = CGRectMake(24, 64, inner - 48 - 68, 22);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _cookie.position = CGPointMake(inner - 24 - 34 - 6, CARD_HEIGHT / 2);
    [CATransaction commit];
    y += CARD_HEIGHT + 22;
    _hueName.frame = CGRectMake(MARGIN + 4, y, inner, 18);
    _hueTrack.frame = CGRectMake(MARGIN, y + 20, inner, SLIDER_ROW);
    y += 20 + SLIDER_ROW + 12;
    _saturationName.frame = CGRectMake(MARGIN + 4, y, inner, 18);
    _saturationTrack.frame = CGRectMake(MARGIN, y + 20, inner, SLIDER_ROW);
    y += 20 + SLIDER_ROW + 12;
    _brightnessName.frame = CGRectMake(MARGIN + 4, y, inner, 18);
    _brightnessTrack.frame = CGRectMake(MARGIN, y + 20, inner, SLIDER_ROW);
    y += 20 + SLIDER_ROW + 22;
    _chips.frame = CGRectMake(0, y, width, CHIP_SIDE + 8);
    for (NSUInteger i = 0; i < [_chipButtons count]; ++i)
        [[_chipButtons objectAtIndex:i] setFrame:CGRectMake(MARGIN + i * (CHIP_SIDE + CHIP_GAP), 4, CHIP_SIDE, CHIP_SIDE)];
    _chips.contentSize = CGSizeMake(2 * MARGIN + [_chipButtons count] * (CHIP_SIDE + CHIP_GAP) - CHIP_GAP, CHIP_SIDE + 8);
}

- (void)applyTheme {
    UIColor *secondary = [YZTheme subtleTextColor];
    _hueName.textColor = secondary;
    _saturationName.textColor = secondary;
    _brightnessName.textColor = secondary;
    [self refresh];
}

// everything that follows the color: the card and its ink, the digits, the handles and gradients of the tracks and the chosen chip
- (void)refresh {
    UIColor *color = YZAccentColor(_hue, _saturation, _brightness);
    CGFloat c[4] = { 0, 0, 0, 1 };
    [color getRed:&c[0] green:&c[1] blue:&c[2] alpha:&c[3]];
    // dark words on a light card, white ones on a dark card
    BOOL light = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2] > 0.62f;
    UIColor *ink = light ? [UIColor colorWithWhite:0.1f alpha:1] : [UIColor whiteColor];
    _card.backgroundColor = color;
    if (_morphing) {
        // lit from the top left so the card stands off the ground behind it
        if (!_cardGradient) {
            _cardGradient = [[CAGradientLayer alloc] init];
            _cardGradient.cornerRadius = CARD_RADIUS;
            _cardGradient.startPoint = CGPointMake(0, 0);
            _cardGradient.endPoint = CGPointMake(1, 1);
            [_card.layer insertSublayer:_cardGradient atIndex:0];
            _cardGradient.frame = _card.bounds;
        }
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        _cardGradient.colors = [NSArray arrayWithObjects:(id)YZShade(color, 0.28f).CGColor, (id)color.CGColor, (id)YZShade(color, -0.38f).CGColor, nil];
        _cardGradient.locations = [NSArray arrayWithObjects:[NSNumber numberWithFloat:0], [NSNumber numberWithFloat:0.5f], [NSNumber numberWithFloat:1], nil];
        _cardGradient.hidden = NO;
        [CATransaction commit];
    } else
        _cardGradient.hidden = YES;
    _hash.textColor = [ink colorWithAlphaComponent:0.6f];
    _note.textColor = [ink colorWithAlphaComponent:0.6f];
    _field.textColor = ink;
    if ([_field respondsToSelector:@selector(setTintColor:)])
        [_field performSelector:@selector(setTintColor:) withObject:ink];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (_morphing) {
        // the shape turns by itself, the layer is not turned by the hue as well
        _cookie.fillColor = [ink colorWithAlphaComponent:0.3f].CGColor;
        _cookie.transform = CATransform3DIdentity;
        if (self.window)
            YZShapeStartMorph(_cookie, 68);
        else
            YZShapeStopMorph(_cookie);
    } else {
        _cookie.fillColor = [ink colorWithAlphaComponent:0.16f].CGColor;
        _cookie.transform = CATransform3DMakeRotation(_hue / 360.0f * (CGFloat)(M_PI * 2) / LOBES, 0, 0, 1);
    }
    [CATransaction commit];
    if (!_field.isFirstResponder)
        _field.text = YZAccentHex(_hue, _saturation, _brightness);
    YZAccentTrack *hueTrack = (YZAccentTrack *)_hueTrack, *saturationTrack = (YZAccentTrack *)_saturationTrack, *brightnessTrack = (YZAccentTrack *)_brightnessTrack;
    hueTrack.fraction = _hue / 360.0f;
    saturationTrack.fraction = _saturation;
    brightnessTrack.fraction = _brightness;
    [hueTrack setHandleColor:YZAccentShown(_hue, MAX(0.78f, _saturation))];
    [saturationTrack setHandleColor:color];
    [saturationTrack setColors:[NSArray arrayWithObjects:YZAccentColor(_hue, 0, _brightness), YZAccentColor(_hue, 1, _brightness), nil]];
    [brightnessTrack setHandleColor:color];
    [brightnessTrack setColors:[NSArray arrayWithObjects:[UIColor blackColor], YZAccentColor(_hue, _saturation, 1), nil]];
    NSInteger chosen = -1;
    for (NSUInteger i = 0; i < PRESET_COUNT; ++i) {
        float dh = fabsf(YZAccentPresets[i].hue - _hue);
        if (dh > 180)
            dh = 360 - dh;
        BOOL grey = YZAccentPresets[i].saturation == 0;
        if (_brightness > 0.97f && fabsf(YZAccentPresets[i].saturation - _saturation) < 0.03f && (grey || dh < 3)) {
            chosen = (NSInteger)i;
            break;
        }
    }
    if (chosen == _chosenChip)
        return;
    _chosenChip = chosen;
    for (NSUInteger i = 0; i < [_chipButtons count]; ++i) {
        UIButton *chip = [_chipButtons objectAtIndex:i];
        BOOL on = (NSInteger)i == chosen;
        chip.selected = on;
        // the chosen chip turns from a disc into a rounded square, on the spring
        CGFloat radius = on ? 14 : CHIP_SIDE / 2;
        if ([YZTheme animationsEnabled]) {
            CABasicAnimation *morph = [CABasicAnimation animationWithKeyPath:@"cornerRadius"];
            morph.fromValue = [NSNumber numberWithDouble:chip.layer.cornerRadius];
            morph.toValue = [NSNumber numberWithDouble:radius];
            morph.duration = 0.3;
            morph.timingFunction = [CAMediaTimingFunction functionWithControlPoints:0.34f:1.45f:0.64f:1.0f];
            [chip.layer addAnimation:morph forKey:@"cornerRadius"];
        }
        chip.layer.cornerRadius = radius;
    }
}

- (void)setHue:(float)hue {
    _hue = MIN(360, MAX(0, hue));
    [self refresh];
}

- (void)setSaturation:(float)saturation {
    _saturation = MIN(1, MAX(0, saturation));
    [self refresh];
}

- (void)setBrightness:(float)brightness {
    _brightness = MIN(1, MAX(0, brightness));
    [self refresh];
}

// close the open field first, its end of editing would put the old digits back
- (void)endTyping {
    [_field resignFirstResponder];
}

- (void)hueMoved {
    float fraction = ((YZAccentTrack *)_hueTrack).fraction;
    [self endTyping];
    _hue = fraction * 360.0f;
    [self refresh];
    [self sendActionsForControlEvents:UIControlEventValueChanged];
}

- (void)saturationMoved {
    float fraction = ((YZAccentTrack *)_saturationTrack).fraction;
    [self endTyping];
    _saturation = fraction;
    [self refresh];
    [self sendActionsForControlEvents:UIControlEventValueChanged];
}

- (void)brightnessMoved {
    float fraction = ((YZAccentTrack *)_brightnessTrack).fraction;
    [self endTyping];
    _brightness = fraction;
    [self refresh];
    [self sendActionsForControlEvents:UIControlEventValueChanged];
}

- (void)released {
    [self sendActionsForControlEvents:UIControlEventTouchUpInside];
}

- (void)chipTapped:(UIButton *)chip {
    [self endTyping];
    _hue = YZAccentPresets[chip.tag].hue;
    _saturation = YZAccentPresets[chip.tag].saturation;
    _brightness = 1;
    [self refresh];
    [YZTheme animateFeedbackOfView:chip];
    [self sendActionsForControlEvents:UIControlEventTouchUpInside];
}

- (void)hexEdited {
    yozora_accent accent = { _hue, _saturation, _brightness };
    NSString *text = _field.text;
    if (text.length != 6 || !yozora_accent_parse_hex([text UTF8String], 6, &accent))
        return;
    _hue = accent.hue;
    _saturation = accent.saturation;
    _brightness = accent.brightness;
    [self refresh];
    [self sendActionsForControlEvents:UIControlEventValueChanged];
}

- (BOOL)textField:(UITextField *)field shouldChangeCharactersInRange:(NSRange)range replacementString:(NSString *)string {
    return YZAccentHexAllowed([field.text stringByReplacingCharactersInRange:range withString:[string uppercaseString]]);
}

- (BOOL)textFieldShouldReturn:(UITextField *)field {
    [field resignFirstResponder];
    return NO;
}

// the color is taken when the field is done; a short or broken entry goes back to the color shown by the tracks
- (void)textFieldDidEndEditing:(UITextField *)field {
    yozora_accent accent = { _hue, _saturation, _brightness };
    NSString *text = field.text;
    BOOL valid = (text.length == 3 || text.length == 6) && yozora_accent_parse_hex([text UTF8String], text.length, &accent);
    if (valid) {
        _hue = accent.hue;
        _saturation = accent.saturation;
        _brightness = accent.brightness;
    }
    [self refresh];
    if (valid)
        [self sendActionsForControlEvents:UIControlEventTouchUpInside];
}

// a tap on the card types the digits
- (BOOL)beginTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event {
    return CGRectContainsPoint(_card.frame, [touch locationInView:self]);
}

- (void)endTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event {
    [_field becomeFirstResponder];
}
@end
