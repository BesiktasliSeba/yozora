#import "YZPopupMenu.h"
#import "YZCompat.h"
#import "YZControls.h"
#import "YZClassic.h"
#import "YZMotion.h"
#import "YZTheme.h"
#import <QuartzCore/QuartzCore.h>

static const CGFloat YZPopupRowHeight = 52;
static const CGFloat YZPopupGlyph = 24;
static const CGFloat YZPopupPadding = 8;
static const CGFloat YZPopupMargin = 8;
static const CGFloat YZPopupWidth = 304;

@interface YZPopupRow : YZStateButton
@end
@implementation YZPopupRow
- (void)setHighlighted:(BOOL)highlighted {
    BOOL pressed = highlighted && !self.highlighted;
    [super setHighlighted:highlighted];
    if (pressed)
        [YZTheme animateFeedbackOfView:self.imageView];
}
@end

@implementation YZPopupMenu
@synthesize bottomInset = _bottomInset;
@synthesize closing = _closing;

- (id)initWithTitles:(NSArray *)titles icons:(NSArray *)icons handler:(void (^)(NSInteger))handler {
    self = [super initWithFrame:CGRectZero];
    if (!self)
        return nil;
    _titles = [titles copy];
    _icons = [icons copy];
    _handler = [handler copy];
    self.backgroundColor = [UIColor colorWithWhite:0 alpha:0.18f];
    self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    UIButton *outside = [UIButton buttonWithType:UIButtonTypeCustom];
    outside.tag = 1;
    outside.frame = self.bounds;
    outside.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [outside addTarget:self action:@selector(outsidePressed) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:outside];
    return self;
}

- (void)dealloc {
    [_titles release];
    [_icons release];
    [_handler release];
    [_panel release];
    [super dealloc];
}

- (void)outsidePressed { [self dismissAnimated:YES]; }

- (void)rowPressed:(UIButton *)row { [self leaveWithIndex:row.tag]; }

// the panel is built for the room around the anchor: it hangs from the anchor when there is room under it, and
// it grows upward or to the left from the finger when there is not, so the card never covers the touched spot
- (void)buildPanelAround:(CGPoint)anchor {
    CGFloat width = _hostSize.width, height = _hostSize.height;
    CGFloat panelWidth = MIN(width - 2 * YZPopupMargin, YZPopupWidth);
    CGFloat floorY = MAX(YZPopupMargin + YZPopupRowHeight, height - MAX((CGFloat)0, _bottomInset) - YZPopupMargin);
    CGFloat content = 2 * YZPopupPadding + YZPopupRowHeight * [_titles count];
    CGFloat below = floorY - anchor.y, above = anchor.y - YZPopupMargin;
    BOOL downward = below >= MIN(content, (CGFloat)(YZPopupRowHeight * 3)) || below >= above;
    CGFloat room = MAX(YZPopupRowHeight, downward ? below : above);
    CGFloat panelHeight = MIN(content, MAX(YZPopupRowHeight * 2, room));
    BOOL leftward = anchor.x > width / 2;
    CGFloat x = leftward ? anchor.x - panelWidth : anchor.x;
    x = MAX(YZPopupMargin, MIN(x, width - YZPopupMargin - panelWidth));
    CGFloat y = downward ? anchor.y : anchor.y - panelHeight;
    y = MAX(YZPopupMargin, MIN(y, floorY - panelHeight));
    _panel = [[UIView alloc] initWithFrame:CGRectMake(0, 0, panelWidth, panelHeight)];
    _panel.layer.anchorPoint = CGPointMake(leftward ? 1 : 0, downward ? 0 : 1);
    _panel.layer.position = CGPointMake(x + (leftward ? panelWidth : 0), y + (downward ? 0 : panelHeight));
    // the shadow lies on the panel and the corners are cut on the view inside it, a view that clips has no shadow
    _panel.layer.shadowColor = [[UIColor blackColor] CGColor];
    _panel.layer.shadowOpacity = 0.3f;
    _panel.layer.shadowRadius = 14;
    _panel.layer.shadowOffset = CGSizeMake(0, 5);
    _panel.layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:_panel.bounds cornerRadius:[YZTheme cardRadius]].CGPath;
    UIView *card = [[[YZPlateView alloc] initWithFrame:_panel.bounds] autorelease];
    card.layer.masksToBounds = YES;
    [_panel addSubview:card];
    UIScrollView *list = [[[YZScrollView alloc] initWithFrame:card.bounds] autorelease];
    list.tag = 9;
    list.contentSize = CGSizeMake(panelWidth, content);
    list.showsHorizontalScrollIndicator = NO;
    list.alwaysBounceVertical = NO;
    // a touch on a row must not wait for the scroll view to decide it is no drag (YZScrollView lets a drag that
    // starts on a row scroll)
    list.delaysContentTouches = NO;
    [card addSubview:list];
    for (NSUInteger i = 0; i < [_titles count]; ++i) {
        YZPopupRow *row = [YZPopupRow buttonWithType:UIButtonTypeCustom];
        row.frame = CGRectMake(0, YZPopupPadding + i * YZPopupRowHeight, panelWidth, YZPopupRowHeight);
        row.tag = (NSInteger)i;
        row.pillInsets = UIEdgeInsetsMake(2, 8, 2, 8);
        row.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
        row.contentEdgeInsets = UIEdgeInsetsMake(0, 20, 0, 16);
        row.titleEdgeInsets = UIEdgeInsetsMake(0, 14, 0, 0);
        [row setImage:[YZTheme icon:(YZIcon)[[_icons objectAtIndex:i] intValue] size:YZPopupGlyph color:[YZTheme accentColor]] forState:UIControlStateNormal];
        [row setTitle:[_titles objectAtIndex:i] forState:UIControlStateNormal];
        [row setTitleColor:[YZTheme textColor] forState:UIControlStateNormal];
        row.titleLabel.font = [YZTheme systemFontOfSize:15];
        // the longest title (open an image in a new tab) does not fit one line of the card in Russian
        row.titleLabel.numberOfLines = 2;
        row.titleLabel.lineBreakMode = YZLineBreakWordWrapping;
        [row addTarget:self action:@selector(rowPressed:) forControlEvents:UIControlEventTouchUpInside];
        [list addSubview:row];
    }
    [self addSubview:_panel];
}

- (void)showInView:(UIView *)host anchor:(CGPoint)anchor {
    if (self.superview)
        return;
    self.frame = host.bounds;
    _hostSize = host.bounds.size;
    [_panel removeFromSuperview];
    [_panel release];
    _panel = nil;
    [self buildPanelAround:anchor];
    [host addSubview:self];
    UIColor *scrim = [self.backgroundColor retain];
    self.backgroundColor = [UIColor clearColor];
    [UIView animateWithDuration:[YZTheme animationsEnabled] ? 0.25 : 0 animations:^{ self.backgroundColor = scrim; } completion:^(BOOL finished) {
        (void)finished;
        [scrim release];
    }];
    _panel.alpha = 0;
    _panel.transform = CGAffineTransformMakeScale(0.4f, 0.4f);
    YZMotionAnimateView(_panel, CGAffineTransformIdentity, 1, YOZORA_MOTION_WOBBLE, 0, nil);
    // the glyphs wobble in one after the other while the card opens
    NSUInteger animated = 0;
    for (UIView *row in [[_panel viewWithTag:9] subviews]) {
        if (![row isKindOfClass:[YZPopupRow class]])
            continue;
        YZMotionAnimateEnter([(UIButton *)row imageView], CGAffineTransformMakeScale(0.5f, 0.5f), 0, YOZORA_MOTION_WOBBLE, 0.08 + animated * 0.025);
        if (++animated == 8)
            break;
    }
}

// a turned screen moves the anchor away from the finger, the card closes instead of hanging from a stale point
- (void)layoutSubviews {
    [super layoutSubviews];
    if (_panel && !_closing && !CGSizeEqualToSize(self.bounds.size, _hostSize))
        [self dismissAnimated:NO];
}

- (void)leaveWithIndex:(NSInteger)index {
    if (_closing)
        return;
    _closing = YES;
    self.userInteractionEnabled = NO;
    void (^handler)(NSInteger) = [_handler retain];
    BOOL animated = self.superview != nil;
    [self retain];
    if (animated)
        [UIView animateWithDuration:[YZTheme animationsEnabled] ? 0.18 : 0 animations:^{ self.backgroundColor = [UIColor clearColor]; } completion:nil];
    void (^finish)(BOOL) = ^(BOOL finished) {
        (void)finished;
        // the completion runs at once when animations are off, inside the touch of the row button, so the menu
        // must outlive that touch
        [[self retain] autorelease];
        [self removeFromSuperview];
        if (handler)
            handler(index);
        [handler release];
        [self release];
    };
    if (!animated) {
        finish(YES);
        return;
    }
    YZMotionAnimateView(_panel, CGAffineTransformMakeScale(0.7f, 0.7f), 0, YOZORA_MOTION_DISMISS, 0, finish);
}

- (void)dismissAnimated:(BOOL)animated {
    if (_closing)
        return;
    if (!animated) {
        // the page goes away under the menu: no animation and no answer beyond -1
        _closing = YES;
        void (^handler)(NSInteger) = [_handler retain];
        [self retain];
        [self removeFromSuperview];
        if (handler)
            handler(-1);
        [handler release];
        [self release];
        return;
    }
    [self leaveWithIndex:-1];
}
@end
