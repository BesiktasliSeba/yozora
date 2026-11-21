#import "YZClassic.h"
#import "YZTheme.h"
#import <QuartzCore/QuartzCore.h>

static UIColor *YZHSB(CGFloat h, CGFloat s, CGFloat b) {
    return [UIColor colorWithHue:h saturation:MAX((CGFloat)0, MIN((CGFloat)1, s)) brightness:MAX((CGFloat)0, MIN((CGFloat)1, b)) alpha:1];
}

// hue, saturation and brightness of the accent of the palette in use, a bar is built from them
static void YZAccentHSB(CGFloat *h, CGFloat *s, CGFloat *b) {
    CGFloat alpha = 1;
    UIColor *accent = [YZTheme accentColor];
    // the private session wears the accent chosen for it as it is: the palette lightens it for text on a dark ground, which a bar should not be
    if ([YZSettings incognito]) {
        yozora_accent chosen = { YOZORA_PRIVATE_PALETTE_HUE, YOZORA_ACCENT_PALETTE_SATURATION, 1 };
        [YZSettings customPrivateAccent:&chosen];
        float r, g, blue;
        yozora_accent_rgb(&chosen, &r, &g, &blue);
        accent = [UIColor colorWithRed:r green:g blue:blue alpha:1];
    }
    if (![accent getHue:h saturation:s brightness:b alpha:&alpha]) {
        *h = 0.78f;
        *s = 0.6f;
        *b = 0.75f;
    }
}

// the light theme shows the accent as it is, the dark ones a deeper copy so the white ink keeps its contrast
static CGFloat YZBarDepth(void) {
    if ([YZSettings incognito])
        return 0.66f;
    if (![YZTheme isDark])
        return 1;
    return [YZSettings theme] == YZThemeBlack && ![YZSettings incognito] ? 0.4f : 0.55f;
}

@implementation YZClassic

+ (UIColor *)barTopColor {
    CGFloat h, s, b;
    YZAccentHSB(&h, &s, &b);
    CGFloat depth = YZBarDepth();
    return YZHSB(h, s * 0.82f, b * depth + 0.15f);
}

+ (UIColor *)barMidColor {
    CGFloat h, s, b;
    YZAccentHSB(&h, &s, &b);
    return YZHSB(h, s, b * YZBarDepth());
}

+ (UIColor *)barBottomColor {
    CGFloat h, s, b;
    YZAccentHSB(&h, &s, &b);
    return YZHSB(h, s + 0.05f, b * YZBarDepth() * 0.78f);
}

+ (UIColor *)inkColor { return [UIColor whiteColor]; }
+ (UIColor *)subtleInkColor { return [UIColor colorWithWhite:1 alpha:0.72f]; }
+ (UIColor *)stampColor { return [UIColor colorWithWhite:0 alpha:0.45f]; }
+ (UIColor *)glossTopColor { return [UIColor colorWithWhite:1 alpha:0.38f]; }
+ (UIColor *)glossBottomColor { return [UIColor colorWithWhite:1 alpha:0.10f]; }

// the picture of a bar at 44 points: the gradient, the hard edged gloss over the upper half, a light line below the top edge and a
// dark one at the bottom. stretched, so it serves any height of bar
+ (UIImage *)barImage {
    static UIImage *cached;
    static NSString *cachedKey;
    CGFloat c[3] = { 0, 0, 0 };
    CGFloat a = 1;
    [[self barMidColor] getRed:&c[0] green:&c[1] blue:&c[2] alpha:&a];
    NSString *key = [NSString stringWithFormat:@"%.3f:%.3f:%.3f", c[0], c[1], c[2]];
    if (cached && [cachedKey isEqualToString:key])
        return cached;
    CGFloat height = 44;
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(4, height), YES, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    NSArray *body = [NSArray arrayWithObjects:(id)[[self barTopColor] CGColor], (id)[[self barBottomColor] CGColor], nil];
    CGGradientRef gradient = CGGradientCreateWithColors(space, (CFArrayRef)body, NULL);
    CGContextDrawLinearGradient(context, gradient, CGPointMake(0, 0), CGPointMake(0, height), 0);
    CGGradientRelease(gradient);
    NSArray *gloss = [NSArray arrayWithObjects:(id)[[self glossTopColor] CGColor], (id)[[self glossBottomColor] CGColor], nil];
    CGGradientRef shine = CGGradientCreateWithColors(space, (CFArrayRef)gloss, NULL);
    CGContextSaveGState(context);
    CGContextClipToRect(context, CGRectMake(0, 0, 4, height / 2));
    CGContextDrawLinearGradient(context, shine, CGPointMake(0, 0), CGPointMake(0, height / 2), 0);
    CGContextRestoreGState(context);
    CGGradientRelease(shine);
    CGColorSpaceRelease(space);
    [[UIColor colorWithWhite:1 alpha:0.32f] setFill];
    UIRectFill(CGRectMake(0, 0, 4, 1));
    [[UIColor colorWithWhite:0 alpha:0.5f] setFill];
    UIRectFill(CGRectMake(0, height - 1, 4, 1));
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    [cached release];
    [cachedKey release];
    cached = [[image stretchableImageWithLeftCapWidth:1 topCapHeight:(NSInteger)(height / 2)] retain];
    cachedKey = [key retain];
    return cached;
}

+ (UIImage *)stampedImage:(UIImage *)glyph {
    if (!glyph)
        return nil;
    CGSize size = glyph.size;
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(size.width, size.height + 1), NO, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();
    // the dark copy lies a point lower, the glyph is drawn over it in white
    CGContextSaveGState(context);
    CGContextSetShadowWithColor(context, CGSizeMake(0, -1), 0, [[self stampColor] CGColor]);
    [glyph drawAtPoint:CGPointMake(0, 0)];
    CGContextRestoreGState(context);
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return image;
}

+ (void)stampLabel:(UILabel *)label {
    label.shadowColor = [self stampColor];
    label.shadowOffset = CGSizeMake(0, -1);
}

+ (UIImage *)keyImageWithColor:(UIColor *)color radius:(CGFloat)radius pressed:(BOOL)pressed {
    CGFloat h, s, b, alpha;
    if (![color getHue:&h saturation:&s brightness:&b alpha:&alpha]) {
        h = 0;
        s = 0;
        b = 0.5f;
    }
    CGFloat dim = pressed ? 0.8f : 1;
    CGFloat side = 2 * radius + 2;
    CGFloat height = MAX(side, (CGFloat)30);
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(side, height), NO, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGRect rect = CGRectMake(0.5f, 0.5f, side - 1, height - 1);
    UIBezierPath *shape = [UIBezierPath bezierPathWithRoundedRect:rect cornerRadius:radius];
    CGContextSaveGState(context);
    [shape addClip];
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    NSArray *body = [NSArray arrayWithObjects:(id)[YZHSB(h, s * 0.8f, (b + 0.14f) * dim) CGColor], (id)[YZHSB(h, s + 0.04f, b * 0.8f * dim) CGColor], nil];
    CGGradientRef gradient = CGGradientCreateWithColors(space, (CFArrayRef)body, NULL);
    CGContextDrawLinearGradient(context, gradient, CGPointMake(0, 0), CGPointMake(0, height), 0);
    CGGradientRelease(gradient);
    NSArray *gloss = [NSArray arrayWithObjects:(id)[[UIColor colorWithWhite:1 alpha:pressed ? 0.2f : 0.42f] CGColor], (id)[[UIColor colorWithWhite:1 alpha:pressed ? 0.05f : 0.12f] CGColor], nil];
    CGGradientRef shine = CGGradientCreateWithColors(space, (CFArrayRef)gloss, NULL);
    CGContextSaveGState(context);
    CGContextClipToRect(context, CGRectMake(0, 0, side, height / 2));
    CGContextDrawLinearGradient(context, shine, CGPointMake(0, 0), CGPointMake(0, height / 2), 0);
    CGContextRestoreGState(context);
    CGGradientRelease(shine);
    CGColorSpaceRelease(space);
    CGContextRestoreGState(context);
    [[UIColor colorWithWhite:0 alpha:0.55f] setStroke];
    shape.lineWidth = 1;
    [shape stroke];
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return [image resizableImageWithCapInsets:UIEdgeInsetsMake(height / 2 - 1, radius + 0.5f, height / 2 - 1, radius + 0.5f)];
}

+ (void)styleKey:(UIButton *)button color:(UIColor *)color radius:(CGFloat)radius modernRadius:(CGFloat)modernRadius {
    if ([YZTheme isClassic]) {
        button.backgroundColor = [UIColor clearColor];
        button.layer.cornerRadius = 0;
        [button setBackgroundImage:[self keyImageWithColor:color radius:radius pressed:NO] forState:UIControlStateNormal];
        [button setBackgroundImage:[self keyImageWithColor:color radius:radius pressed:YES] forState:UIControlStateHighlighted];
        [button setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
        button.titleLabel.shadowColor = [self stampColor];
        button.titleLabel.shadowOffset = CGSizeMake(0, -1);
        return;
    }
    [button setBackgroundImage:nil forState:UIControlStateNormal];
    [button setBackgroundImage:nil forState:UIControlStateHighlighted];
    button.backgroundColor = color;
    button.layer.cornerRadius = modernRadius;
    button.titleLabel.shadowColor = nil;
}

+ (void)styleGlyphKey:(UIButton *)button icon:(YZIcon)icon size:(CGFloat)size modernRadius:(CGFloat)modernRadius {
    BOOL classic = [YZTheme isClassic];
    [self styleKey:button color:classic ? [self barMidColor] : [YZTheme stateColor] radius:9 modernRadius:modernRadius];
    UIImage *glyph = classic ? [self stampedImage:[YZTheme icon:icon size:size color:[UIColor whiteColor]]] : [YZTheme icon:icon size:size color:[YZTheme accentColor]];
    [button setImage:glyph forState:UIControlStateNormal];
}

+ (UIImage *)wellImageWithRadius:(CGFloat)radius {
    CGFloat side = 2 * radius + 2;
    CGFloat height = MAX(side, (CGFloat)30);
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(side, height), NO, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGRect rect = CGRectMake(0.5f, 0.5f, side - 1, height - 1);
    UIBezierPath *shape = [UIBezierPath bezierPathWithRoundedRect:rect cornerRadius:radius];
    [[YZTheme fieldColor] setFill];
    [shape fill];
    // the shadow of the bar along the upper edge, set in
    CGContextSaveGState(context);
    [shape addClip];
    NSArray *shade = [NSArray arrayWithObjects:(id)[[UIColor colorWithWhite:0 alpha:0.28f] CGColor], (id)[[UIColor colorWithWhite:0 alpha:0] CGColor], nil];
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGGradientRef gradient = CGGradientCreateWithColors(space, (CFArrayRef)shade, NULL);
    CGContextDrawLinearGradient(context, gradient, CGPointMake(0, 0), CGPointMake(0, 6), 0);
    CGGradientRelease(gradient);
    CGColorSpaceRelease(space);
    CGContextRestoreGState(context);
    [[UIColor colorWithWhite:0 alpha:0.5f] setStroke];
    shape.lineWidth = 1;
    [shape stroke];
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return [image resizableImageWithCapInsets:UIEdgeInsetsMake(height / 2 - 1, radius + 0.5f, height / 2 - 1, radius + 0.5f)];
}
@end

@implementation YZGradientView
+ (Class)layerClass { return [CAGradientLayer class]; }

- (void)setTop:(UIColor *)top bottom:(UIColor *)bottom {
    CAGradientLayer *layer = (CAGradientLayer *)self.layer;
    layer.colors = [NSArray arrayWithObjects:(id)[top CGColor], (id)[bottom CGColor], nil];
    layer.startPoint = CGPointMake(0.5f, 0);
    layer.endPoint = CGPointMake(0.5f, 1);
}
@end

@implementation YZGlossBarView
@synthesize darkLineAtTop = _darkLineAtTop;

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    _gloss = [[CAGradientLayer alloc] init];
    [self.layer addSublayer:_gloss];
    _light = [[CALayer alloc] init];
    [self.layer addSublayer:_light];
    _dark = [[CALayer alloc] init];
    [self.layer addSublayer:_dark];
    [self applyTheme];
    return self;
}

- (void)dealloc {
    [_gloss release];
    [_light release];
    [_dark release];
    [super dealloc];
}

- (void)setDarkLineAtTop:(BOOL)top {
    _darkLineAtTop = top;
    [self setNeedsLayout];
}

- (void)applyTheme {
    BOOL classic = [YZTheme isClassic];
    _gloss.hidden = !classic;
    _light.hidden = !classic;
    _dark.hidden = !classic;
    if (!classic) {
        // the modern bar is the flat color of the theme
        UIColor *flat = [YZTheme toolbarColor];
        [self setTop:flat bottom:flat];
        return;
    }
    [self setTop:[YZClassic barTopColor] bottom:[YZClassic barBottomColor]];
    _gloss.colors = [NSArray arrayWithObjects:(id)[[YZClassic glossTopColor] CGColor], (id)[[YZClassic glossBottomColor] CGColor], nil];
    _gloss.startPoint = CGPointMake(0.5f, 0);
    _gloss.endPoint = CGPointMake(0.5f, 1);
    _light.backgroundColor = [[UIColor colorWithWhite:1 alpha:0.32f] CGColor];
    _dark.backgroundColor = [[UIColor colorWithWhite:0 alpha:0.5f] CGColor];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width, height = self.bounds.size.height;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _gloss.frame = CGRectMake(0, 0, width, floor(height / 2));
    CGFloat pixel = 1.0f / [[UIScreen mainScreen] scale];
    _light.frame = CGRectMake(0, _darkLineAtTop ? pixel : 0, width, pixel);
    _dark.frame = CGRectMake(0, _darkLineAtTop ? 0 : height - pixel, width, pixel);
    [CATransaction commit];
}
@end

@implementation YZPlateView
@synthesize surfaceInModern = _surface;
@synthesize sheetCornersInModern = _sheetCorners;

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    [self applyTheme];
    return self;
}

- (void)applyTheme {
    CALayer *layer = self.layer;
    if (![YZTheme isClassic]) {
        UIColor *flat = _surface ? [YZTheme surfaceColor] : [YZTheme cardColor];
        [self setTop:flat bottom:flat];
        layer.cornerRadius = _sheetCorners ? [YZTheme sheetRadius] : [YZTheme cardRadius];
        layer.borderWidth = 0;
        return;
    }
    BOOL dark = [YZTheme isDark];
    // pale glass on the light themes, charcoal on the dark ones, a lit rim and a thin outline
    [self setTop:dark ? [UIColor colorWithWhite:0.27f alpha:1] : [UIColor colorWithWhite:1 alpha:1]
          bottom:dark ? [UIColor colorWithWhite:0.15f alpha:1] : [UIColor colorWithWhite:0.88f alpha:1]];
    layer.cornerRadius = [YZTheme cardRadius];
    layer.borderWidth = 1;
    layer.borderColor = [(dark ? [UIColor colorWithWhite:0 alpha:0.8f] : [UIColor colorWithWhite:0 alpha:0.4f]) CGColor];
}
@end
