#import "YZTheme.h"
#import "YZCompat.h"
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/QuartzCore.h>
#import "YZSettings.h"
#import "YZMotion.h"
#import "YZClassic.h"
#include <dlfcn.h>

static UIColor *YZRGB(int r, int g, int b) {
    return [UIColor colorWithRed:r / 255.0f green:g / 255.0f blue:b / 255.0f alpha:1];
}

@interface YZSelectionView : UIView {
    UIView *_pill;
}
- (void)applyTheme;
@end

@implementation YZSelectionView
- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.backgroundColor = [UIColor clearColor];
    _pill = [[UIView alloc] initWithFrame:CGRectZero];
    [self addSubview:_pill];
    return self;
}
- (void)applyTheme { _pill.backgroundColor = [YZTheme stateColor]; }
- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect frame = CGRectInset(self.bounds, 8, 2);
    _pill.frame = frame;
    _pill.layer.cornerRadius = MIN(frame.size.height / 2, (CGFloat)18);
}
- (void)dealloc {
    [_pill release];
    [super dealloc];
}
@end

@implementation YZTheme

// each color follows the chosen theme, the private session has a purple palette of its own over all of them.
// the private accent screen shows it in a normal session too, so the person sees what they pick
static BOOL YZPreviewsPrivate = NO;

static BOOL YZPaletteIsPrivate(void) {
    return [YZSettings incognito] || YZPreviewsPrivate;
}

static UIColor *YZPick(UIColor *light, UIColor *dark, UIColor *black, UIColor *incognito) {
    if (YZPaletteIsPrivate())
        return incognito;
    switch ([YZSettings theme]) {
    case YZThemeDark: return dark;
    case YZThemeBlack: return black;
    default: return light;
    }
}

// the tonal Material You palette of exteria: a surface for pages and lists, a container for bars, a higher
// one for fields, one purple accent. black keeps pure black grounds, the private session is the deep purple
// of Firefox private mode. an accent turns hue, saturation and (for grounds only) brightness, greys of black
// stay grey
static UIColor *YZTint(UIColor *color, BOOL dim) {
    yozora_accent accent;
    BOOL isPrivate = YZPaletteIsPrivate();
    if (isPrivate ? ![YZSettings customPrivateAccent:&accent] : ![YZSettings customAccent:&accent])
        return color;
    CGFloat c[4] = { 0, 0, 0, 1 };
    if (![color getRed:&c[0] green:&c[1] blue:&c[2] alpha:&c[3]])
        return color;
    float r = (float)c[0], g = (float)c[1], b = (float)c[2];
    // the private palette is measured from its own purple, 269 degrees, and not from the 286 of the normal one
    yozora_accent_turn_about(&accent, isPrivate ? YOZORA_PRIVATE_PALETTE_HUE : YOZORA_ACCENT_PALETTE_HUE, dim ? 1 : 0, &r, &g, &b);
    return [UIColor colorWithRed:r green:g blue:b alpha:c[3]];
}

+ (BOOL)isClassic { return [YZSettings design] == YZDesignClassic; }

+ (CGFloat)barOverlap:(UINavigationBar *)bar {
    return [self isClassic] || !bar ? 0 : CGRectGetMaxY(bar.frame);
}

// the classic design keeps the greys of the old system for its grounds, lists and fields, and the accent only on bars and
// keys. the private session keeps its purple
static UIColor *YZGrey(UIColor *light, UIColor *dark, UIColor *black, UIColor *incognito, BOOL dim) {
    if (YZPaletteIsPrivate())
        return YZTint(incognito, dim);
    switch ([YZSettings theme]) {
    case YZThemeDark: return dark;
    case YZThemeBlack: return black;
    default: return light;
    }
}

// the bar is the gradient of the accent, the flat colors that meet it (the window behind the bars, the strip under the status
// bar) are its middle
+ (UIColor *)toolbarColor {
    if ([self isClassic])
        return [YZClassic barMidColor];
    return YZTint(YZPick(YZRGB(0xf5, 0xf1, 0xfb), YZRGB(0x1b, 0x17, 0x28), YZRGB(0, 0, 0), YZRGB(0x2b, 0x10, 0x48)), YES);
}
+ (UIColor *)fieldColor {
    if ([self isClassic])
        return YZGrey(YZRGB(0xff, 0xff, 0xff), YZRGB(0x38, 0x3a, 0x40), YZRGB(0x2c, 0x2c, 0x2e), YZRGB(0x3d, 0x1d, 0x66), YES);
    return YZTint(YZPick(YZRGB(0xea, 0xe4, 0xf5), YZRGB(0x2b, 0x27, 0x3e), YZRGB(28, 28, 30), YZRGB(0x3d, 0x1d, 0x66)), YES);
}
+ (UIColor *)textColor {
    if ([self isClassic])
        return YZGrey(YZRGB(0x1d, 0x1f, 0x24), YZRGB(0xf1, 0xf2, 0xf5), YZRGB(255, 255, 255), YZRGB(0xf6, 0xef, 0xff), NO);
    return YZTint(YZPick(YZRGB(0x1d, 0x19, 0x2b), YZRGB(0xf0, 0xee, 0xf8), YZRGB(255, 255, 255), YZRGB(0xf6, 0xef, 0xff)), NO);
}
+ (UIColor *)subtleTextColor {
    if ([self isClassic])
        return YZGrey(YZRGB(0x6b, 0x6f, 0x78), YZRGB(0x9b, 0x9e, 0xa8), YZRGB(152, 152, 157), YZRGB(0xc9, 0xb4, 0xea), NO);
    return YZTint(YZPick(YZRGB(0x65, 0x5a, 0x7a), YZRGB(0xb0, 0xaa, 0xc4), YZRGB(152, 152, 157), YZRGB(0xc9, 0xb4, 0xea)), NO);
}
+ (UIColor *)barInkColor { return [self isClassic] ? [YZClassic inkColor] : [self textColor]; }
+ (UIColor *)barSubtleInkColor { return [self isClassic] ? [YZClassic subtleInkColor] : [self subtleTextColor]; }
// the light theme shows the chosen color as it is, the dark ones turn their lighter accent to its hue and
// saturation to keep contrast
+ (UIColor *)accentColor {
    yozora_accent accent;
    if (!YZPaletteIsPrivate() && [YZSettings theme] == YZThemeLight && [YZSettings customAccent:&accent]) {
        float r, g, b;
        yozora_accent_rgb(&accent, &r, &g, &b);
        return [UIColor colorWithRed:r green:g blue:b alpha:1];
    }
    return YZTint(YZPick(YZRGB(0xa4, 0x4b, 0xbe), YZRGB(0xac, 0x62, 0xde), YZRGB(0xb9, 0x78, 0xe6), YZRGB(0xd9, 0xb8, 0xff)), NO);
}
+ (UIColor *)separatorColor {
    if ([self isClassic])
        return YZGrey(YZRGB(0xc6, 0xc9, 0xd0), YZRGB(0x14, 0x15, 0x18), YZRGB(0x2c, 0x2c, 0x2e), YZRGB(0x4a, 0x28, 0x73), YES);
    return YZTint(YZPick(YZRGB(0xde, 0xd8, 0xe8), YZRGB(0x3a, 0x34, 0x4c), YZRGB(44, 44, 46), YZRGB(0x4a, 0x28, 0x73)), YES);
}
+ (UIColor *)backdropColor {
    if ([self isClassic])
        return YZGrey(YZRGB(0xd5, 0xd8, 0xde), YZRGB(0x10, 0x11, 0x13), YZRGB(0, 0, 0), YZRGB(0x16, 0x07, 0x29), YES);
    return YZTint(YZPick(YZRGB(0xf0, 0xea, 0xf8), YZRGB(0x11, 0x0e, 0x19), YZRGB(0, 0, 0), YZRGB(0x16, 0x07, 0x29)), YES);
}
+ (UIColor *)surfaceColor {
    if ([self isClassic])
        return YZGrey(YZRGB(0xe6, 0xe8, 0xec), YZRGB(0x1c, 0x1d, 0x21), YZRGB(0, 0, 0), YZRGB(0x1f, 0x0b, 0x38), YES);
    return YZTint(YZPick(YZRGB(0xfb, 0xfa, 0xfe), YZRGB(0x15, 0x12, 0x1f), YZRGB(0, 0, 0), YZRGB(0x1f, 0x0b, 0x38)), YES);
}
+ (UIColor *)cardColor {
    if ([self isClassic])
        return YZGrey(YZRGB(0xff, 0xff, 0xff), YZRGB(0x2b, 0x2c, 0x31), YZRGB(0x1c, 0x1c, 0x1e), YZRGB(0x33, 0x16, 0x52), YES);
    return YZTint(YZPick(YZRGB(0xf1, 0xec, 0xf8), YZRGB(0x23, 0x1f, 0x32), YZRGB(0x1c, 0x1c, 0x1e), YZRGB(0x33, 0x16, 0x52)), YES);
}
+ (UIColor *)errorColor { return YZRGB(0xc5, 0x2d, 0x4f); }
// the pill behind a pressed or selected control: the accent over the surface at a fifth
+ (UIColor *)stateColor { return [[self accentColor] colorWithAlphaComponent:0.2f]; }
+ (UIColor *)errorContainerColor { return YZPick(YZRGB(0xf9, 0xde, 0xdc), YZRGB(0x8c, 0x1d, 0x18), YZRGB(0x8c, 0x1d, 0x18), YZRGB(0x8c, 0x1d, 0x4a)); }
+ (UIColor *)onErrorContainerColor { return YZPick(YZRGB(0x41, 0x0e, 0x0b), YZRGB(0xf9, 0xde, 0xdc), YZRGB(0xf9, 0xde, 0xdc), YZRGB(0xff, 0xe0, 0xec)); }
// the strip under the status bar is black, deepest purple in private. from iOS 7 it is the toolbar color as
// in Chrome and Safari and the status text follows the theme, older systems draw their own black status bar
// which the strip continues
static BOOL YZModernStatusBar(void) {
    return [[[UIDevice currentDevice] systemVersion] floatValue] >= 7.0f;
}
+ (UIColor *)statusStripColor {
    // the classic bar starts at the top of the screen and the strip is the top of its gradient
    if ([self isClassic])
        return [YZClassic barTopColor];
    if (YZModernStatusBar())
        return [self toolbarColor];
    return YZPaletteIsPrivate() ? YZRGB(0x10, 0x04, 0x20) : [UIColor blackColor];
}
+ (UIColor *)scrimColor { return [UIColor colorWithWhite:0 alpha:0.4f]; }

+ (CGFloat)cardRadius { return [self isClassic] ? 10 : 20; }
+ (CGFloat)tileRadius { return [self isClassic] ? 9 : 16; }
+ (CGFloat)sheetRadius { return [self isClassic] ? 10 : 28; }

+ (CGFloat)textScale {
    CGFloat zoom = (CGFloat)[YZSettings textZoom] / 100;
    return MIN((CGFloat)1.3, MAX((CGFloat)0.85, 1 + (zoom - 1) * (CGFloat)0.65));
}

// half points keep the text sharp on a screen of two or three pixels to the point and make the steps of the setting show
+ (UIFont *)systemFontOfSize:(CGFloat)size { return [UIFont systemFontOfSize:round(size * [self textScale] * 2) / 2]; }
+ (UIFont *)boldSystemFontOfSize:(CGFloat)size { return [UIFont boldSystemFontOfSize:round(size * [self textScale] * 2) / 2]; }

+ (BOOL)isDark { return YZPaletteIsPrivate() || [YZSettings theme] != YZThemeLight; }
+ (void)setPrivatePalettePreview:(BOOL)on { YZPreviewsPrivate = on; }
+ (BOOL)isIncognito { return [YZSettings incognito]; }

static UIImage *YZSolidImage(UIColor *color) {
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(1, 1), YES, 0);
    [color setFill];
    UIRectFill(CGRectMake(0, 0, 1, 1));
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return [image stretchableImageWithLeftCapWidth:0 topCapHeight:0];
}

// a flat bar of the screen color with no line below, the system gradient and glossy buttons do not follow the
// theme
+ (void)styleNavigationBar:(UINavigationBar *)bar {
    if ([self isClassic]) {
        [self styleClassicNavigationBar:bar];
        return;
    }
    [bar setBackgroundImage:YZSolidImage([self surfaceColor]) forBarMetrics:UIBarMetricsDefault];
    // an empty image takes the hairline away, the iOS 6 headers do not declare the property
    if ([bar respondsToSelector:NSSelectorFromString(@"setShadowImage:")])
        [bar setValue:[[[UIImage alloc] init] autorelease] forKey:@"shadowImage"];
    if ([bar respondsToSelector:@selector(setTitleTextAttributes:)])
        bar.titleTextAttributes = [NSDictionary dictionaryWithObject:[self textColor] forKey:YZTextColorAttribute];
    // ios 15 draws the scroll edge appearance (transparent, no shadow) whenever content sits at the top or is
    // short and ignores the background image, so every appearance is set to the opaque one
    Class appearanceClass = NSClassFromString(@"UINavigationBarAppearance");
    if (appearanceClass) {
        id appearance = [[[appearanceClass alloc] init] autorelease];
        [appearance performSelector:NSSelectorFromString(@"configureWithOpaqueBackground")];
        [appearance setValue:[self surfaceColor] forKey:@"backgroundColor"];
        [appearance setValue:[UIColor clearColor] forKey:@"shadowColor"];
        [appearance setValue:[NSDictionary dictionaryWithObject:[self textColor] forKey:YZTextColorAttribute] forKey:@"titleTextAttributes"];
        for (NSString *key in [NSArray arrayWithObjects:@"standardAppearance", @"scrollEdgeAppearance", @"compactAppearance", @"compactScrollEdgeAppearance", nil]) {
            NSString *setter = [NSString stringWithFormat:@"set%@%@:", [[key substringToIndex:1] uppercaseString], [key substringFromIndex:1]];
            if ([bar respondsToSelector:NSSelectorFromString(setter)])
                [bar setValue:appearance forKey:key];
        }
    }
}

+ (void)styleTransparentNavigationBar:(UINavigationBar *)bar {
    // the classic bar is opaque, lit and glossy, there is no content to show through it
    if ([self isClassic]) {
        [self styleClassicNavigationBar:bar];
        return;
    }
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(1, 1), NO, 0);
    UIImage *clear = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    [bar setBackgroundImage:[clear stretchableImageWithLeftCapWidth:0 topCapHeight:0] forBarMetrics:UIBarMetricsDefault];
    if ([bar respondsToSelector:NSSelectorFromString(@"setShadowImage:")])
        [bar setValue:[[[UIImage alloc] init] autorelease] forKey:@"shadowImage"];
    if ([bar respondsToSelector:@selector(setTitleTextAttributes:)])
        bar.titleTextAttributes = [NSDictionary dictionaryWithObject:[self textColor] forKey:YZTextColorAttribute];
    // ios 5 and 6 let the content run under a translucent bar only
    bar.translucent = YES;
    Class appearanceClass = NSClassFromString(@"UINavigationBarAppearance");
    if (appearanceClass) {
        id appearance = [[[appearanceClass alloc] init] autorelease];
        [appearance performSelector:NSSelectorFromString(@"configureWithTransparentBackground")];
        [appearance setValue:[UIColor clearColor] forKey:@"shadowColor"];
        [appearance setValue:[NSDictionary dictionaryWithObject:[self textColor] forKey:YZTextColorAttribute] forKey:@"titleTextAttributes"];
        for (NSString *key in [NSArray arrayWithObjects:@"standardAppearance", @"scrollEdgeAppearance", @"compactAppearance", @"compactScrollEdgeAppearance", nil]) {
            NSString *setter = [NSString stringWithFormat:@"set%@%@:", [[key substringToIndex:1] uppercaseString], [key substringFromIndex:1]];
            if ([bar respondsToSelector:NSSelectorFromString(setter)])
                [bar setValue:appearance forKey:key];
        }
    }
}

// the bar of the classic design is the picture of the app bar, with white stamped titles. ios 7 and later paint the bar from
// an appearance, which gets the same picture
+ (void)styleClassicNavigationBar:(UINavigationBar *)bar {
    UIImage *image = [YZClassic barImage];
    [bar setBackgroundImage:image forBarMetrics:UIBarMetricsDefault];
    if ([bar respondsToSelector:NSSelectorFromString(@"setShadowImage:")])
        [bar setValue:[[[UIImage alloc] init] autorelease] forKey:@"shadowImage"];
    bar.translucent = NO;
    if ([bar respondsToSelector:@selector(setTitleTextAttributes:)]) {
        bar.titleTextAttributes = [NSDictionary dictionaryWithObjectsAndKeys:[YZClassic inkColor], YZTextColorAttribute, nil];
    }
    Class appearanceClass = NSClassFromString(@"UINavigationBarAppearance");
    if (appearanceClass) {
        id appearance = [[[appearanceClass alloc] init] autorelease];
        [appearance performSelector:NSSelectorFromString(@"configureWithOpaqueBackground")];
        [appearance setValue:image forKey:@"backgroundImage"];
        [appearance setValue:[UIColor clearColor] forKey:@"shadowColor"];
        [appearance setValue:[NSDictionary dictionaryWithObject:[YZClassic inkColor] forKey:YZTextColorAttribute] forKey:@"titleTextAttributes"];
        for (NSString *key in [NSArray arrayWithObjects:@"standardAppearance", @"scrollEdgeAppearance", @"compactAppearance", @"compactScrollEdgeAppearance", nil]) {
            NSString *setter = [NSString stringWithFormat:@"set%@%@:", [[key substringToIndex:1] uppercaseString], [key substringFromIndex:1]];
            if ([bar respondsToSelector:NSSelectorFromString(setter)])
                [bar setValue:appearance forKey:key];
        }
    }
}

// the system blue selection is dark gray on a light theme, a pressed row takes a rounded pill of the theme a
// little inside the row
+ (void)styleSelectionOfCell:(UITableViewCell *)cell {
    if (![cell.selectedBackgroundView isKindOfClass:[YZSelectionView class]])
        cell.selectedBackgroundView = [[[YZSelectionView alloc] initWithFrame:CGRectZero] autorelease];
    [(YZSelectionView *)cell.selectedBackgroundView applyTheme];
}

+ (UIView *)titleHeaderWithText:(NSString *)text width:(CGFloat)width {
    UIView *header = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, width, 76)] autorelease];
    header.backgroundColor = [self surfaceColor];
    UILabel *label = [[[UILabel alloc] initWithFrame:CGRectMake(28, 12, width - 56, 48)] autorelease];
    label.text = text;
    label.font = [self boldSystemFontOfSize:32];
    label.textColor = [self textColor];
    label.backgroundColor = [UIColor clearColor];
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    // a title that does not fit goes whole to a second line, it was cut at the edge or shrunk until it no
    // longer looked like a title
    label.numberOfLines = 0;
    label.lineBreakMode = YZLineBreakWordWrapping;
    CGSize fitted = [label sizeThatFits:CGSizeMake(MAX((CGFloat)40, width - 56), 400)];
    CGFloat height = MAX((CGFloat)48, ceil(fitted.height));
    label.frame = CGRectMake(28, 12, width - 56, height);
    header.frame = CGRectMake(0, 0, width, MAX((CGFloat)76, height + 28));
    [header addSubview:label];
    return header;
}

+ (UIBarButtonItem *)barButtonItemWithTitle:(NSString *)title bold:(BOOL)bold target:(id)target action:(SEL)action {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeCustom];
    button.titleLabel.font = bold ? [self boldSystemFontOfSize:16] : [self systemFontOfSize:16];
    [button setTitle:title forState:UIControlStateNormal];
    [button setTitleColor:[self isClassic] ? [YZClassic inkColor] : [self accentColor] forState:UIControlStateNormal];
    if ([self isClassic]) {
        button.titleLabel.shadowColor = [YZClassic stampColor];
        button.titleLabel.shadowOffset = CGSizeMake(0, -1);
    }
    [button addTarget:target action:action forControlEvents:UIControlEventTouchUpInside];
    CGSize size = YZTextSize(title, button.titleLabel.font);
    button.frame = CGRectMake(0, 0, size.width + 20, 44);
    return [[[UIBarButtonItem alloc] initWithCustomView:button] autorelease];
}

+ (UIImageView *)disclosureView {
    UIImageView *view = [[[UIImageView alloc] initWithImage:[self icon:YZIconChevron size:20 color:[self subtleTextColor]]] autorelease];
    return view;
}
+ (UIStatusBarStyle)statusBarStyle { return YZStatusBarStyle(YZModernStatusBar() ? [self isDark] : YES); }

+ (BOOL)isPad {
    return [[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad;
}

// icons of Firefox for Android (MPL 2.0, app/icons/ui, made by tools/make_ui_icons.py), one 96 pixel mask per
// glyph, flat in the bundle
static NSString *YZIconFile(YZIcon icon) {
    static NSString *const names[] = {
        @"yz-back", @"yz-forward", @"yz-chevron", @"yz-reload", @"yz-close", @"yz-menu", @"yz-lock", @"yz-search", @"yz-star", @"yz-star-filled",
        @"yz-plus", @"yz-close", @"yz-bookmarks", @"yz-history", @"yz-tabs", @"yz-shield", @"yz-settings", @"yz-play", @"yz-pause", @"yz-download",
        @"yz-incognito", @"yz-copy", @"yz-check", @"yz-delete", @"yz-language", @"yz-palette", @"yz-text-size", @"yz-cookie", @"yz-sweep",
        @"yz-code", @"yz-speed", @"yz-animation", @"yz-font", @"yz-info", @"yz-desktop", @"yz-arrow-up", @"yz-arrow-down",
        @"yz-share", @"yz-tab-new", @"yz-image", @"yz-link", @"yz-translate", @"yz-swap",
        @"yz-hardware", @"yz-bug-report", @"yz-terminal", @"yz-receipt-long", @"yz-bolt", @"yz-memory", @"yz-brush"
    };
    return names[icon];
}

static CGImageRef YZIconMask(YZIcon icon) {
    static NSMutableDictionary *masks;
    if (!masks)
        masks = [[NSMutableDictionary alloc] init];
    NSString *name = YZIconFile(icon);
    UIImage *mask = [masks objectForKey:name];
    if (!mask) {
        mask = [UIImage imageWithContentsOfFile:[[NSBundle mainBundle] pathForResource:name ofType:@"png"]];
        if (!mask) {
            NSLog(@"yozora: icon %@ is missing from the bundle", name);
            return NULL;
        }
        [masks setObject:mask forKey:name];
    }
    return mask.CGImage;
}

// the mask is a glyph in the alpha channel, clipping to it fills the color through the glyph only
static UIImage *YZTintedMask(CGImageRef mask, CGFloat size, UIColor *color, void (^extra)(CGContextRef)) {
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(size, size), NO, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGContextSaveGState(context);
    CGContextTranslateCTM(context, 0, size);
    CGContextScaleCTM(context, 1, -1);
    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
    CGContextClipToMask(context, CGRectMake(0, 0, size, size), mask);
    CGContextSetFillColorWithColor(context, color.CGColor);
    CGContextFillRect(context, CGRectMake(0, 0, size, size));
    CGContextRestoreGState(context);
    if (extra)
        extra(context);
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return image;
}

+ (UIImage *)icon:(YZIcon)icon size:(CGFloat)size color:(UIColor *)color {
    static NSMutableDictionary *cache;
    if (!cache)
        cache = [[NSMutableDictionary alloc] init];
    CGFloat red = 0, green = 0, blue = 0, alpha = 0;
    [color getRed:&red green:&green blue:&blue alpha:&alpha];
    NSString *key = [NSString stringWithFormat:@"%d-%.0f-%.2f-%.2f-%.2f-%.2f", (int)icon, size, red, green, blue, alpha];
    UIImage *cached = [cache objectForKey:key];
    if (cached)
        return cached;
    CGImageRef mask = YZIconMask(icon);
    if (!mask)
        return nil;
    UIImage *image = YZTintedMask(mask, size, color, nil);
    if (image)
        [cache setObject:image forKey:key];
    return image;
}

+ (UIImage *)barIcon:(YZIcon)icon size:(CGFloat)size disabled:(BOOL)disabled {
    if ([self isClassic]) {
        UIImage *glyph = [self icon:icon size:size color:disabled ? [UIColor colorWithWhite:1 alpha:0.35f] : [YZClassic inkColor]];
        return disabled ? glyph : [YZClassic stampedImage:glyph];
    }
    return [self icon:icon size:size color:disabled ? [[self subtleTextColor] colorWithAlphaComponent:0.4f] : [self textColor]];
}

+ (UIImage *)barTabsIconWithCount:(NSUInteger)count size:(CGFloat)size {
    if ([self isClassic])
        return [YZClassic stampedImage:[self tabsIconWithCount:count size:size color:[YZClassic inkColor]]];
    return [self tabsIconWithCount:count size:size color:[self textColor]];
}

+ (UIColor *)barHighlightProgressColor {
    return [self isClassic] ? [UIColor colorWithWhite:1 alpha:0.92f] : [self accentColor];
}

+ (UIColor *)pressedColor {
    if ([self isClassic])
        return YZGrey(YZRGB(0xd3, 0xd6, 0xdd), YZRGB(0x3d, 0x3f, 0x46), YZRGB(0x2c, 0x2c, 0x2e), YZRGB(0x4a, 0x28, 0x73), YES);
    return [self fieldColor];
}

+ (UIColor *)barHighlightColor {
    return [self isClassic] ? [UIColor colorWithRed:1 green:0.84f blue:0.25f alpha:1] : [self accentColor];
}

// the tab counter is a rounded square with the number inside. the tab tray glyph has a bar under the frame and reads as a monitor
+ (UIImage *)tabsIconWithCount:(NSUInteger)count size:(CGFloat)size color:(UIColor *)color {
    NSString *text = count > 99 ? @":)" : [NSString stringWithFormat:@"%lu", (unsigned long)count];
    UIFont *font = [UIFont boldSystemFontOfSize:size * (count > 9 ? 0.3f : 0.38f)];
    CGFloat line = size * 2 / 24, edge = size * 4 / 24;
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(size, size), NO, 0);
    CGRect frame = CGRectInset(CGRectMake(0, 0, size, size), edge, edge);
    UIBezierPath *outline = [UIBezierPath bezierPathWithRoundedRect:CGRectInset(frame, line / 2, line / 2) cornerRadius:size * 3 / 24];
    outline.lineWidth = line;
    [color setStroke];
    [outline stroke];
    CGSize textSize = YZTextSize(text, font);
    YZDrawText(text, CGPointMake((size - textSize.width) / 2, (size - textSize.height) / 2 + size * 0.01f), font, color);
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return image;
}

+ (UIImage *)logoNamed:(NSString *)name {
    static NSMutableDictionary *logos;
    if (!logos)
        logos = [[NSMutableDictionary alloc] init];
    UIImage *logo = [logos objectForKey:name];
    if (!logo) {
        logo = [UIImage imageWithContentsOfFile:[[NSBundle mainBundle] pathForResource:[@"yz-logo-" stringByAppendingString:name] ofType:@"png"]];
        if (!logo) {
            NSLog(@"yozora: logo %@ is missing from the bundle", name);
            return nil;
        }
        [logos setObject:logo forKey:name];
    }
    return logo;
}

+ (UIImage *)mascotImage:(NSString *)place {
    return [self mascotImage:place mascot:[YZSettings mascot]];
}

+ (UIImage *)mascotImage:(NSString *)place mascot:(YZMascot)mascot {
    return [UIImage imageNamed:[NSString stringWithFormat:@"%@-%@", mascot == YZMascotFox ? @"fox" : @"yozora", place]];
}

+ (UIImage *)wordmarkWithHeight:(CGFloat)height color:(UIColor *)color {
    static UIImage *mask;
    if (!mask)
        mask = [[UIImage imageWithContentsOfFile:[[NSBundle mainBundle] pathForResource:@"yz-wordmark" ofType:@"png"]] retain];
    if (!mask.CGImage || mask.size.height < 1) {
        NSLog(@"yozora: yz-wordmark is missing from the bundle");
        return nil;
    }
    CGSize size = CGSizeMake(ceilf(height * mask.size.width / mask.size.height), height);
    UIGraphicsBeginImageContextWithOptions(size, NO, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGContextTranslateCTM(context, 0, size.height);
    CGContextScaleCTM(context, 1, -1);
    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
    CGContextClipToMask(context, CGRectMake(0, 0, size.width, size.height), mask.CGImage);
    CGContextSetFillColorWithColor(context, color.CGColor);
    CGContextFillRect(context, CGRectMake(0, 0, size.width, size.height));
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return image;
}

+ (UIColor *)tileColorForHost:(NSString *)host {
    static const int palette[][3] = {
        {255, 154, 162}, {255, 183, 120}, {255, 218, 120}, {170, 215, 130},
        {120, 205, 190}, {130, 190, 240}, {170, 160, 235}, {220, 160, 220}
    };
    unsigned hash = 5381;
    const char *bytes = [host UTF8String];
    for (; bytes && *bytes; ++bytes)
        hash = hash * 33 + (unsigned char)*bytes;
    const int *pick = palette[hash % (sizeof palette / sizeof palette[0])];
    return YZRGB(pick[0], pick[1], pick[2]);
}

// the reduced motion switch exists from ios 8, older systems have no such setting and keep the animations
+ (BOOL)animationsEnabled {
    static BOOL looked = NO;
    static BOOL (*reduced)(void) = NULL;
    if (!looked) {
        looked = YES;
        reduced = (BOOL (*)(void))dlsym(RTLD_DEFAULT, "UIAccessibilityIsReduceMotionEnabled");
    }
    return !(reduced && reduced());
}

+ (void)animate:(void (^)(void))animations completion:(void (^)(BOOL finished))completion {
    if (![self animationsEnabled]) {
        animations();
        if (completion)
            completion(YES);
        return;
    }
    [UIView animateWithDuration:0.28 delay:0
                        options:UIViewAnimationOptionCurveEaseOut | UIViewAnimationOptionBeginFromCurrentState | UIViewAnimationOptionAllowUserInteraction
                     animations:animations completion:completion];
}

+ (void)animateEntranceOfView:(UIView *)view offset:(CGPoint)offset delay:(NSTimeInterval)delay {
    if (!view)
        return;
    YZMotionAnimateEnter(view, CGAffineTransformConcat(view.transform, CGAffineTransformMakeTranslation(offset.x, offset.y)), 0,
                         YOZORA_MOTION_POPUP_ENTER, MIN((NSTimeInterval)0.12, MAX((NSTimeInterval)0, delay)));
}

// a pulse that grows back from 0.78 on the wobble curve, which passes the full size and swings back twice
+ (void)animateFeedbackOfView:(UIView *)view {
    if (!view)
        return;
    YZMotionAnimateEnter(view, CGAffineTransformConcat(view.transform, CGAffineTransformMakeScale(0.78f, 0.78f)), view.alpha, YOZORA_MOTION_WOBBLE, 0);
}

+ (void)stretchImage:(UIImage *)image caps:(CGFloat)caps onView:(UIView *)view {
    CGImageRef picture = image.CGImage;
    if (!picture || view.layer.contents == (id)picture)
        return;
    CGFloat side = image.size.width, height = image.size.height;
    if (side <= 2 * caps || height <= 2 * caps)
        return;
    view.layer.contents = (id)picture;
    view.layer.contentsScale = image.scale;
    // the stretched part is the middle, a rectangle of the picture in unit coordinates
    view.layer.contentsCenter = CGRectMake(caps / side, caps / height, (side - 2 * caps) / side, (height - 2 * caps) / height);
}

+ (void)setView:(UIView *)view shown:(BOOL)shown scale:(CGFloat)scale {
    CGAffineTransform away = CGAffineTransformMakeScale(scale, scale);
    if (shown) {
        if (!view.hidden && view.alpha == 1 && CGAffineTransformIsIdentity(view.transform))
            return;
        if (view.hidden) {
            view.alpha = 0;
            view.transform = away;
            view.hidden = NO;
        }
        // a view that grows into place wobbles into it, a plain fade takes the emphasized curve of a sheet
        YZMotionAnimateView(view, CGAffineTransformIdentity, 1, scale == 1 ? YOZORA_MOTION_POPUP_ENTER : YOZORA_MOTION_WOBBLE, 0, nil);
        return;
    }
    if (view.hidden)
        return;
    YZMotionAnimateView(view, away, 0, YOZORA_MOTION_DISMISS, 0, ^(BOOL finished) {
        (void)finished;
        // a view that was shown again while it faded stays
        if (view.alpha == 0) {
            view.hidden = YES;
            view.alpha = 1;
            view.transform = CGAffineTransformIdentity;
        }
    });
}
@end
