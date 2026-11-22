#import "YZTintPicker.h"

#import <QuartzCore/QuartzCore.h>

#import "YZStrings.h"
#import "YZTheme.h"
#include "yozora_accent.h"

static const CGFloat YZPickerHeight = 256;
static const CGFloat YZHexStrip = 56;
static const CGFloat YZPickerInset = 16;
static const CGFloat YZThumbSide = 36;
static const CGFloat YZSwatchSide = 34;
// the square is drawn this coarse and the layer smooths it up, a pixel per point would be 60 thousand colors on every layout
static const int YZRenderWidth = 120;
static const int YZRenderHeight = 60;

static UIColor *YZTintColor(float hue, float saturation, float brightness) {
    yozora_accent accent = { hue, saturation, brightness };
    float red, green, blue;
    yozora_accent_rgb(&accent, &red, &green, &blue);
    return [UIColor colorWithRed:red green:green blue:blue alpha:1];
}

// a ring of turned metal, light at the top and dark below, around a face of the color: the face is lit from above as well
static UIImage *YZMetalDisc(UIColor *face, CGFloat side) {
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(side, side), NO, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGRect outer = CGRectInset(CGRectMake(0, 0, side, side), 0.5f, 0.5f);
    CGRect inner = CGRectInset(outer, 4, 4);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextSaveGState(context);
    CGContextAddEllipseInRect(context, outer);
    CGContextClip(context);
    CGFloat rim[12] = { 0.98f, 0.98f, 0.99f, 1, 0.66f, 0.67f, 0.70f, 1, 0.34f, 0.35f, 0.38f, 1 };
    CGFloat at[3] = { 0, 0.5f, 1 };
    CGGradientRef metal = CGGradientCreateWithColorComponents(space, rim, at, 3);
    CGContextDrawLinearGradient(context, metal, CGPointMake(0, 0), CGPointMake(0, side), 0);
    CGGradientRelease(metal);
    CGContextRestoreGState(context);
    CGContextSetLineWidth(context, 1);
    CGContextSetRGBStrokeColor(context, 0, 0, 0, 0.55f);
    CGContextStrokeEllipseInRect(context, outer);
    CGContextSaveGState(context);
    CGContextAddEllipseInRect(context, inner);
    CGContextClip(context);
    [face setFill];
    CGContextFillRect(context, inner);
    CGFloat shine[8] = { 1, 1, 1, 0.38f, 1, 1, 1, 0 };
    CGGradientRef gloss = CGGradientCreateWithColorComponents(space, shine, NULL, 2);
    CGContextDrawLinearGradient(context, gloss, CGPointMake(0, CGRectGetMinY(inner)), CGPointMake(0, CGRectGetMidY(inner)), 0);
    CGGradientRelease(gloss);
    CGContextRestoreGState(context);
    CGContextSetRGBStrokeColor(context, 0, 0, 0, 0.6f);
    CGContextStrokeEllipseInRect(context, CGRectInset(inner, 0.5f, 0.5f));
    CGColorSpaceRelease(space);
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return image;
}

static NSString *YZTintHex(float hue, float saturation, float brightness) {
    yozora_accent accent = { hue, saturation, brightness };
    char hex[7];
    yozora_accent_hex(&accent, hex);
    return [NSString stringWithUTF8String:hex];
}

static BOOL YZTintHexAllowed(NSString *text) {
    if (text.length > 6)
        return NO;
    NSCharacterSet *bad = [[NSCharacterSet characterSetWithCharactersInString:@"0123456789ABCDEFabcdef"] invertedSet];
    return [text rangeOfCharacterFromSet:bad].location == NSNotFound;
}

// three digits are the short form of six, anything else that is not six hex digits is no color
static BOOL YZTintOfHex(NSString *text, float *hue, float *saturation, float *brightness) {
    if (text.length != 3 && text.length != 6)
        return NO;
    yozora_accent accent = { *hue, *saturation, *brightness };
    const char *utf8 = [text UTF8String];
    if (!utf8 || !yozora_accent_parse_hex(utf8, (size_t)text.length, &accent))
        return NO;
    *hue = accent.hue;
    *saturation = accent.saturation;
    *brightness = accent.brightness;
    return YES;
}

@implementation YZTintPicker

@synthesize hue = _hue;
@synthesize saturation = _saturation;
@synthesize brightness = _brightness;
@synthesize morphing = _morphing;

- (CGFloat)preferredHeight { return YZPickerHeight; }

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.backgroundColor = [UIColor clearColor];
    _brightness = 1;
    self.isAccessibilityElement = YES;
    self.accessibilityTraits = UIAccessibilityTraitAdjustable;
    _ground = [[UIView alloc] init];
    _ground.userInteractionEnabled = NO;
    _ground.layer.cornerRadius = 8;
    _ground.layer.masksToBounds = YES;
    _ground.layer.borderWidth = 1;
    _ground.layer.borderColor = [[UIColor colorWithWhite:0 alpha:0.5f] CGColor];
    [self addSubview:_ground];
    _thumb = [[UIImageView alloc] init];
    _thumb.userInteractionEnabled = NO;
    _thumb.layer.shadowColor = [[UIColor blackColor] CGColor];
    _thumb.layer.shadowOpacity = 0.45f;
    _thumb.layer.shadowRadius = 3;
    _thumb.layer.shadowOffset = CGSizeMake(0, 2);
    [self addSubview:_thumb];
    _swatch = [[UIImageView alloc] init];
    _swatch.userInteractionEnabled = NO;
    [self addSubview:_swatch];
    // the plate holds the # and the digits as two neighbours on one line
    _plate = [[UIView alloc] init];
    _plate.layer.cornerRadius = 8;
    _plate.layer.borderWidth = 1;
    [self addSubview:_plate];
    _hash = [[UILabel alloc] init];
    _hash.text = @"#";
    _hash.backgroundColor = [UIColor clearColor];
    [_plate addSubview:_hash];
    _field = [[UITextField alloc] init];
    _field.delegate = self;
    _field.backgroundColor = [UIColor clearColor];
    _field.contentVerticalAlignment = UIControlContentVerticalAlignmentCenter;
    _field.autocapitalizationType = UITextAutocapitalizationTypeAllCharacters;
    _field.autocorrectionType = UITextAutocorrectionTypeNo;
    _field.keyboardType = UIKeyboardTypeASCIICapable;
    _field.returnKeyType = UIReturnKeyDone;
    _field.clearButtonMode = UITextFieldViewModeNever;
    _field.accessibilityLabel = YZT(@"Цвет акцента");
    [_field addTarget:self action:@selector(hexEdited) forControlEvents:UIControlEventEditingChanged];
    [_plate addSubview:_field];
    [self applyTheme];
    return self;
}

- (void)dealloc {
    _field.delegate = nil;
    [_ground release];
    [_thumb release];
    [_swatch release];
    [_field release];
    [_hash release];
    [_plate release];
    [super dealloc];
}

- (void)applyTheme {
    _plate.backgroundColor = [YZTheme fieldColor];
    _plate.layer.borderColor = [[UIColor colorWithWhite:0 alpha:0.45f] CGColor];
    _hash.font = [YZTheme boldSystemFontOfSize:17];
    _hash.textColor = [YZTheme subtleTextColor];
    _field.font = [YZTheme boldSystemFontOfSize:17];
    _field.textColor = [YZTheme textColor];
    _field.keyboardAppearance = [YZTheme isDark] ? UIKeyboardAppearanceAlert : UIKeyboardAppearanceDefault;
    [self setNeedsLayout];
}

- (CGRect)squareRect {
    return CGRectMake(YZPickerInset, 8, self.bounds.size.width - 2 * YZPickerInset, self.bounds.size.height - 8 - YZHexStrip - 4);
}

- (void)renderSquare {
    CGRect square = [self squareRect];
    if (square.size.width <= 0 || square.size.height <= 0)
        return;
    _ground.frame = square;
    if (CGSizeEqualToSize(_renderedSize, square.size) && _ground.layer.contents)
        return;
    uint8_t *pixels = malloc((size_t)YZRenderWidth * YZRenderHeight * 4);
    if (!pixels) {
        NSLog(@"yozora: the color square could not be drawn, no memory");
        return;
    }
    for (int y = 0; y < YZRenderHeight; ++y) {
        float saturation = 1 - (float)y / (YZRenderHeight - 1);
        for (int x = 0; x < YZRenderWidth; ++x) {
            yozora_accent accent = { 360.0f * x / (YZRenderWidth - 1), saturation, 1 };
            float red, green, blue;
            yozora_accent_rgb(&accent, &red, &green, &blue);
            uint8_t *pixel = pixels + ((size_t)y * YZRenderWidth + x) * 4;
            pixel[0] = (uint8_t)(red * 255 + 0.5f);
            pixel[1] = (uint8_t)(green * 255 + 0.5f);
            pixel[2] = (uint8_t)(blue * 255 + 0.5f);
            pixel[3] = 255;
        }
    }
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(pixels, YZRenderWidth, YZRenderHeight, 8, YZRenderWidth * 4, space, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(space);
    CGImageRef image = context ? CGBitmapContextCreateImage(context) : NULL;
    if (context)
        CGContextRelease(context);
    // the bitmap copies what it was given when the image is made, the buffer is free after
    free(pixels);
    if (!image) {
        NSLog(@"yozora: the color square could not be drawn");
        return;
    }
    _ground.layer.contents = (id)image;
    _ground.layer.magnificationFilter = kCAFilterLinear;
    CGImageRelease(image);
    _renderedSize = square.size;
}

- (void)placeThumb {
    CGRect square = [self squareRect];
    _thumb.bounds = CGRectMake(0, 0, YZThumbSide, YZThumbSide);
    _thumb.center = CGPointMake(square.origin.x + square.size.width * _hue / 360.0f, square.origin.y + square.size.height * (1 - _saturation));
    _thumb.image = YZMetalDisc(YZTintColor(_hue, _saturation, _brightness), YZThumbSide);
    [self showHex];
}

// the swatch and the plate stand in the middle of the strip under the square, the # and the digits are measured and set next to each other
- (void)layoutSubviews {
    [super layoutSubviews];
    [self renderSquare];
    [self placeThumb];
    static const CGFloat plateHeight = 36, gap = 2;
    CGFloat digits = 0;
    for (NSString *sample in [NSArray arrayWithObjects:@"000000", @"888888", @"FFFFFF", @"BBBBBB", nil]) {
        UILabel *probe = [[[UILabel alloc] init] autorelease];
        probe.font = _field.font;
        probe.text = sample;
        [probe sizeToFit];
        digits = MAX(digits, probe.frame.size.width);
    }
    [_hash sizeToFit];
    CGFloat hashWidth = _hash.frame.size.width, fieldWidth = ceil(digits) + 4;
    CGFloat plateWidth = 16 + hashWidth + gap + fieldWidth + 16;
    CGFloat group = YZSwatchSide + 12 + plateWidth;
    CGFloat left = floor((self.bounds.size.width - group) / 2), top = self.bounds.size.height - YZHexStrip + (YZHexStrip - plateHeight) / 2 - 4;
    _swatch.frame = CGRectMake(left, top + (plateHeight - YZSwatchSide) / 2, YZSwatchSide, YZSwatchSide);
    _plate.frame = CGRectMake(left + YZSwatchSide + 12, top, plateWidth, plateHeight);
    _hash.frame = CGRectMake(16, 0, hashWidth, plateHeight);
    _field.frame = CGRectMake(16 + hashWidth + gap, 0, fieldWidth, plateHeight);
}

- (CGRect)editingRect { return _plate.frame; }

- (void)showHex {
    _swatch.image = YZMetalDisc(YZTintColor(_hue, _saturation, _brightness), YZSwatchSide);
    if (!_field.isFirstResponder)
        _field.text = YZTintHex(_hue, _saturation, _brightness);
}

- (void)hexEdited {
    float hue = _hue, saturation = _saturation, brightness = _brightness;
    if (_field.text.length != 6 || !YZTintOfHex(_field.text, &hue, &saturation, &brightness))
        return;
    _hue = hue;
    _saturation = saturation;
    _brightness = brightness;
    [self placeThumb];
    [self sendActionsForControlEvents:UIControlEventValueChanged];
}

- (BOOL)textField:(UITextField *)field shouldChangeCharactersInRange:(NSRange)range replacementString:(NSString *)string {
    return YZTintHexAllowed([field.text stringByReplacingCharactersInRange:range withString:[string uppercaseString]]);
}

- (BOOL)textFieldShouldReturn:(UITextField *)field {
    [field resignFirstResponder];
    return NO;
}

// the color is taken when the field is done, a short or broken entry goes back to the color the square shows
- (void)textFieldDidEndEditing:(UITextField *)field {
    float hue = _hue, saturation = _saturation, brightness = _brightness;
    BOOL valid = YZTintOfHex(field.text, &hue, &saturation, &brightness);
    if (valid) {
        _hue = hue;
        _saturation = saturation;
        _brightness = brightness;
        [self placeThumb];
    }
    [self showHex];
    if (valid)
        [self sendActionsForControlEvents:UIControlEventTouchUpInside];
}

- (void)setHue:(float)hue {
    _hue = MIN(360, MAX(0, hue));
    [self placeThumb];
}

- (void)setSaturation:(float)saturation {
    _saturation = MIN(1, MAX(0, saturation));
    [self placeThumb];
}

- (void)setBrightness:(float)brightness {
    _brightness = MIN(1, MAX(0, brightness));
    [self placeThumb];
}

- (void)moveToPoint:(CGPoint)point {
    CGRect square = [self squareRect];
    if (square.size.width <= 0 || square.size.height <= 0)
        return;
    self.hue = (float)((point.x - square.origin.x) / square.size.width * 360.0);
    self.saturation = (float)(1 - (point.y - square.origin.y) / square.size.height);
}

// a UIControl keeps the touch when the scroll view around it starts to scroll, which a plain view does not on ios 5
- (BOOL)beginTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event {
    (void)event;
    CGPoint point = [touch locationInView:self];
    [_field resignFirstResponder];
    // a touch beside the square is no pick, it only closes the keyboard
    if (!CGRectContainsPoint(CGRectInset([self squareRect], -YZThumbSide / 2, -YZThumbSide / 2), point))
        return NO;
    [self moveToPoint:point];
    [self sendActionsForControlEvents:UIControlEventValueChanged];
    return YES;
}

- (BOOL)continueTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event {
    (void)event;
    [self moveToPoint:[touch locationInView:self]];
    [self sendActionsForControlEvents:UIControlEventValueChanged];
    return YES;
}

- (void)endTrackingWithTouch:(UITouch *)touch withEvent:(UIEvent *)event {
    (void)event;
    if (touch)
        [self moveToPoint:[touch locationInView:self]];
    [self sendActionsForControlEvents:UIControlEventTouchUpInside];
}

- (void)cancelTrackingWithEvent:(UIEvent *)event {
    (void)event;
    [self sendActionsForControlEvents:UIControlEventTouchUpInside];
}

- (NSString *)accessibilityLabel { return YZT(@"Цвет акцента"); }

- (NSString *)accessibilityValue {
    return [NSString stringWithFormat:@"%.0f°, %.0f%%", _hue, _saturation * 100];
}

// voice over steps the hue, which is the axis a color is mostly named by
- (void)accessibilityIncrement {
    self.hue = fmodf(_hue + 10, 360);
    [self sendActionsForControlEvents:UIControlEventTouchUpInside];
}

- (void)accessibilityDecrement {
    self.hue = fmodf(_hue + 350, 360);
    [self sendActionsForControlEvents:UIControlEventTouchUpInside];
}

@end
