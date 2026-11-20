#import <UIKit/UIKit.h>

/* armv7 builds against the ios 5 sdk and runs on 5 to 10, arm64 against a current sdk and runs on 7 and
   later. each uses the calls its sdk has for the oldest system of its slice */
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 70000
#define YZTextAlignmentLeft NSTextAlignmentLeft
#define YZTextAlignmentCenter NSTextAlignmentCenter
#define YZTextAlignmentRight NSTextAlignmentRight
#define YZLineBreakTailTruncation NSLineBreakByTruncatingTail
#define YZLineBreakWordWrapping NSLineBreakByWordWrapping
#define YZTextColorAttribute NSForegroundColorAttributeName
#else
#define YZTextAlignmentLeft UITextAlignmentLeft
#define YZTextAlignmentCenter UITextAlignmentCenter
#define YZTextAlignmentRight UITextAlignmentRight
#define YZLineBreakTailTruncation UILineBreakModeTailTruncation
#define YZLineBreakWordWrapping UILineBreakModeWordWrap
#define YZTextColorAttribute UITextAttributeTextColor
#endif

static inline CGSize YZTextSize(NSString *text, UIFont *font) {
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 70000
    return [text sizeWithAttributes:[NSDictionary dictionaryWithObject:font forKey:NSFontAttributeName]];
#else
    return [text sizeWithFont:font];
#endif
}

static inline void YZDrawText(NSString *text, CGPoint point, UIFont *font, UIColor *color) {
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 70000
    [text drawAtPoint:point withAttributes:[NSDictionary dictionaryWithObjectsAndKeys:font, NSFontAttributeName, color, NSForegroundColorAttributeName, nil]];
#else
    [color setFill];
    [text drawAtPoint:point withFont:font];
#endif
}

/* a label that shrinks its text to fit, down to three quarters of its size */
static inline void YZShrinkToFit(UILabel *label) {
    label.adjustsFontSizeToFitWidth = YES;
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 70000
    label.minimumScaleFactor = 0.75f;
#else
    label.minimumFontSize = label.font.pointSize * 0.75f;
#endif
}

/* the status bar over a dark toolbar: light text from iOS 7, the black bar before */
static inline UIStatusBarStyle YZStatusBarStyle(BOOL dark) {
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 70000
    return dark ? UIStatusBarStyleLightContent : UIStatusBarStyleDefault;
#else
    return dark ? UIStatusBarStyleBlackOpaque : UIStatusBarStyleDefault;
#endif
}

/* the system picks the placeholder color for the light look, black on black in dark themes. an attributed
   placeholder fixes it from ios 6, ios 5 draws a fixed light gray. the key is the literal string, the ios 5
   sdk has no constant */
static inline void YZSetPlaceholder(UITextField *field, NSString *text, UIColor *color) {
    field.placeholder = text;
    SEL setter = NSSelectorFromString(@"setAttributedPlaceholder:");
    Class attributed = NSClassFromString(@"NSAttributedString");
    if (!text || !color || !attributed || ![field respondsToSelector:setter])
        return;
    NSDictionary *attributes = [NSDictionary dictionaryWithObject:color forKey:@"NSColor"];
    id string = [[[attributed alloc] performSelector:NSSelectorFromString(@"initWithString:attributes:") withObject:text withObject:attributes] autorelease];
    [field performSelector:setter withObject:string];
}
