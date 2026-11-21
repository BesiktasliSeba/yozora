#import "YZErrorPopup.h"
#import "YZClassic.h"
#import "YZCompat.h"
#import "YZMotion.h"
#import "YZStrings.h"
#import "YZTheme.h"
#import <QuartzCore/QuartzCore.h>

static const CGFloat YZPopupMaxWidth = 340;
static const CGFloat YZPopupSide = 24;
static const CGFloat YZPopupPad = 20;
static const CGFloat YZPopupPictureHeight = 150;
static const CGFloat YZPopupButtonHeight = 44;

@implementation YZErrorPopup
@synthesize titleText = _titleText;
@synthesize messageText = _messageText;
@synthesize dismissHandler = _dismissHandler;
@synthesize isShown = _shown;

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.backgroundColor = [YZTheme scrimColor];
    self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _card = [[YZPlateView alloc] initWithFrame:CGRectZero];
    [(YZPlateView *)_card setSurfaceInModern:YES];
    [(YZPlateView *)_card setSheetCornersInModern:YES];
    [(YZPlateView *)_card applyTheme];
    _card.layer.shadowColor = [UIColor blackColor].CGColor;
    _card.layer.shadowOpacity = 0.3f;
    _card.layer.shadowRadius = 18;
    _card.layer.shadowOffset = CGSizeMake(0, 8);
    [self addSubview:_card];
    // the sticker has its own outline, so it is drawn whole and never clipped
    _picture = [[UIImageView alloc] initWithImage:[YZTheme mascotImage:@"error"]];
    _picture.contentMode = UIViewContentModeScaleAspectFit;
    [_card addSubview:_picture];
    _title = [[UILabel alloc] initWithFrame:CGRectZero];
    _title.backgroundColor = [UIColor clearColor];
    _title.textColor = [YZTheme textColor];
    _title.font = [YZTheme boldSystemFontOfSize:20];
    _title.textAlignment = YZTextAlignmentCenter;
    _title.numberOfLines = 0;
    [_card addSubview:_title];
    _message = [[UILabel alloc] initWithFrame:CGRectZero];
    _message.backgroundColor = [UIColor clearColor];
    _message.textColor = [YZTheme subtleTextColor];
    _message.font = [YZTheme systemFontOfSize:15];
    _message.textAlignment = YZTextAlignmentCenter;
    _message.numberOfLines = 0;
    [_card addSubview:_message];
    _button = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    _button.titleLabel.font = [YZTheme boldSystemFontOfSize:16];
    [_button setTitle:YZT(@"Понятно") forState:UIControlStateNormal];
    [_button setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    [YZClassic styleKey:_button color:[YZTheme isClassic] ? [YZClassic barMidColor] : [YZTheme accentColor] radius:8 modernRadius:YZPopupButtonHeight / 2];
    [_button addTarget:self action:@selector(dismiss) forControlEvents:UIControlEventTouchUpInside];
    YZMotionAttachBounce(_button, 0.04f);
    [_card addSubview:_button];
    UITapGestureRecognizer *tap = [[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(backdropTapped:)] autorelease];
    tap.delegate = (id<UIGestureRecognizerDelegate>)self;
    [self addGestureRecognizer:tap];
    return self;
}

- (void)dealloc {
    [_card release];
    [_picture release];
    [_title release];
    [_message release];
    [_button release];
    [_titleText release];
    [_messageText release];
    [_dismissHandler release];
    [super dealloc];
}

// a tap on the card keeps it, only the dimmed part around it closes the card
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)recognizer shouldReceiveTouch:(UITouch *)touch {
    (void)recognizer;
    return !CGRectContainsPoint(_card.frame, [touch locationInView:self]);
}

- (void)backdropTapped:(UITapGestureRecognizer *)tap {
    (void)tap;
    [self dismiss];
}

- (void)setMessageText:(NSString *)text {
    if (text == _messageText || [text isEqualToString:_messageText])
        return;
    [text retain];
    [_messageText release];
    _messageText = text;
    [self setNeedsLayout];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    // a layout while the card leaves would fit it again under its own fade
    if (_leaving)
        return;
    CGFloat width = MIN(self.bounds.size.width - 2 * YZPopupSide, YZPopupMaxWidth);
    CGFloat inner = MAX((CGFloat)0, width - 2 * YZPopupPad);
    // a phone on its side has little height, the sticker shrinks before the card is taller than the screen
    CGFloat picture = MIN(YZPopupPictureHeight, MAX((CGFloat)60, self.bounds.size.height * 0.28f));
    _title.text = _titleText;
    _message.text = _messageText;
    CGSize title = [_title sizeThatFits:CGSizeMake(inner, 400)];
    CGSize message = [_message sizeThatFits:CGSizeMake(inner, 400)];
    CGFloat y = YZPopupPad;
    _picture.frame = CGRectMake(YZPopupPad, y, inner, picture);
    y += picture + 12;
    _title.frame = CGRectMake(YZPopupPad, y, inner, ceil(title.height));
    y += ceil(title.height) + 8;
    _message.frame = CGRectMake(YZPopupPad, y, inner, ceil(message.height));
    y += ceil(message.height) + 20;
    _button.frame = CGRectMake(YZPopupPad, y, inner, YZPopupButtonHeight);
    y += YZPopupButtonHeight + YZPopupPad;
    CGAffineTransform held = _card.transform;
    _card.transform = CGAffineTransformIdentity;
    _card.frame = CGRectMake(floor((self.bounds.size.width - width) / 2), MAX((CGFloat)0, floor((self.bounds.size.height - y) / 2)), width, y);
    _card.layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:_card.bounds cornerRadius:[YZTheme isClassic] ? [YZTheme cardRadius] : [YZTheme sheetRadius]].CGPath;
    _card.transform = held;
}

- (void)showInView:(UIView *)host {
    if (_shown)
        return;
    _shown = YES;
    self.frame = host.bounds;
    [host addSubview:self];
    [self layoutIfNeeded];
    CGAffineTransform from = CGAffineTransformConcat(CGAffineTransformMakeScale(0.82f, 0.82f), CGAffineTransformMakeTranslation(0, -24));
    // the backdrop fades in while the card springs in from above and passes its place twice
    self.alpha = 0;
    YZMotionAnimateView(self, CGAffineTransformIdentity, 1, YOZORA_MOTION_ENTER, 0, nil);
    YZMotionAnimateEnter(_card, from, 0, YOZORA_MOTION_WOBBLE, 0);
}

- (void)dismiss {
    if (!_shown || _leaving)
        return;
    _leaving = YES;
    self.userInteractionEnabled = NO;
    [self retain];
    void (^finish)(void) = ^{
        [self removeFromSuperview];
        _shown = NO;
        void (^handler)(void) = [_dismissHandler retain];
        if (handler)
            handler();
        [handler release];
        [self autorelease];
    };
    CGAffineTransform away = CGAffineTransformConcat(CGAffineTransformMakeScale(0.9f, 0.9f), CGAffineTransformMakeTranslation(0, -16));
    YZMotionAnimateView(_card, away, 0, YOZORA_MOTION_DISMISS, 0, nil);
    // the window leaves when its own fade ends, the card runs the same curve but may end a frame earlier
    YZMotionAnimateView(self, CGAffineTransformIdentity, 0, YOZORA_MOTION_DISMISS, 0, ^(BOOL finished) {
        (void)finished;
        finish();
    });
}

@end
