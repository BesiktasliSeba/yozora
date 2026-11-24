#import "YZTabStack.h"
#import "YZCompat.h"
#import "YZMotion.h"
#import "YZStrings.h"
#import "YZTab.h"
#import "YZTheme.h"
#import <QuartzCore/QuartzCore.h>

static const CGFloat YZStackHeader = 44;
// the pages stand flat, as wide as they are, and keep the proportions of their picture: a page that leaned back squeezed its picture by the cosine of the
// angle. depth is shown by size instead, a page near the top of the screen is farther and a little narrower than one at the foot
static const CGFloat YZStackFarScale = 0.9f;

@interface YZStackCard : UIView {
@public
    YZTab *tab;
    UIView *plate;
    CAGradientLayer *bar;
    CALayer *rule;
    UIImageView *picture;
    UIImageView *glyph;
    UILabel *title;
    UIButton *open;
    UIButton *close;
    CGFloat baseY;
    // the height of the part of the page that no page after it covers, the part that takes touches
    CGFloat visible;
}
- (id)initWithTab:(YZTab *)tab target:(id)target;
- (void)updateLook;
/* the title bar of a page that opens leaves while the page grows: the browser has its own bar there. showHeader brings it back */
- (void)hideHeader:(BOOL)animated;
- (void)showHeader;
@end

@implementation YZStackCard

- (id)initWithTab:(YZTab *)aTab target:(id)target {
    self = [super initWithFrame:CGRectZero];
    if (!self)
        return nil;
    tab = [aTab retain];
    self.layer.anchorPoint = CGPointMake(0.5f, 0);
    self.layer.shadowColor = [[UIColor blackColor] CGColor];
    // the page in front throws a shadow up over the strip of the page behind it
    self.layer.shadowOpacity = 0.6f;
    self.layer.shadowRadius = 7;
    self.layer.shadowOffset = CGSizeMake(0, -4);
    plate = [[UIView alloc] initWithFrame:CGRectZero];
    plate.layer.cornerRadius = 4;
    plate.layer.masksToBounds = YES;
    [self addSubview:plate];
    picture = [[UIImageView alloc] initWithFrame:CGRectZero];
    picture.userInteractionEnabled = NO;
    [plate addSubview:picture];
    glyph = [[UIImageView alloc] initWithFrame:CGRectZero];
    glyph.contentMode = UIViewContentModeCenter;
    glyph.userInteractionEnabled = NO;
    [plate addSubview:glyph];
    bar = [[CAGradientLayer alloc] init];
    [plate.layer addSublayer:bar];
    rule = [[CALayer alloc] init];
    [plate.layer addSublayer:rule];
    title = [[UILabel alloc] initWithFrame:CGRectZero];
    title.backgroundColor = [UIColor clearColor];
    title.textAlignment = YZTextAlignmentCenter;
    title.lineBreakMode = YZLineBreakTailTruncation;
    title.userInteractionEnabled = NO;
    [plate addSubview:title];
    open = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    [open addTarget:target action:@selector(cardOpened:) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:open];
    close = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    close.accessibilityLabel = YZT(@"Закрыть вкладку");
    [close addTarget:target action:@selector(cardClosed:) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:close];
    [self updateLook];
    return self;
}

- (void)dealloc {
    [tab release];
    [plate release];
    [bar release];
    [rule release];
    [picture release];
    [glyph release];
    [title release];
    [open release];
    [close release];
    [super dealloc];
}

- (void)updateLook {
    BOOL dark = [YZTheme isDark];
    plate.backgroundColor = dark ? [YZTheme cardColor] : [UIColor whiteColor];
    // the bar of a page is lit from above and ends in a line, as the title bar of a page of ios 7 does
    UIColor *top = dark ? [UIColor colorWithWhite:0.26f alpha:1] : [UIColor colorWithWhite:0.99f alpha:1];
    UIColor *foot = dark ? [UIColor colorWithWhite:0.19f alpha:1] : [UIColor colorWithWhite:0.91f alpha:1];
    bar.colors = [NSArray arrayWithObjects:(id)[top CGColor], (id)[foot CGColor], nil];
    rule.backgroundColor = [[UIColor colorWithWhite:0 alpha:dark ? 0.6f : 0.28f] CGColor];
    title.font = [UIFont boldSystemFontOfSize:17];
    title.textColor = dark ? [UIColor colorWithWhite:0.93f alpha:1] : [UIColor colorWithWhite:0.12f alpha:1];
    title.text = [tab.title length] ? tab.title : YZT(@"Новая вкладка");
    UIImage *preview = tab.thumbnail;
    if (picture.image != preview)
        picture.image = preview;
    glyph.hidden = preview != nil;
    if (!preview)
        glyph.image = [YZTheme icon:(tab.isPrivate ? YZIconIncognito : YZIconSearch) size:40 color:[UIColor colorWithWhite:0.6f alpha:1]];
    [close setImage:[YZTheme icon:YZIconClose size:17 color:dark ? [UIColor colorWithWhite:0.75f alpha:1] : [UIColor colorWithWhite:0.38f alpha:1]] forState:UIControlStateNormal];
    [self setNeedsLayout];
}

- (void)hideHeader:(BOOL)animated {
    if (animated) {
        CABasicAnimation *fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
        fade.fromValue = [NSNumber numberWithFloat:1];
        fade.toValue = [NSNumber numberWithFloat:0];
        fade.duration = 0.3;
        fade.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
        [bar addAnimation:fade forKey:@"yz.header"];
        [rule addAnimation:fade forKey:@"yz.header"];
    }
    bar.opacity = 0;
    rule.opacity = 0;
    title.alpha = 0;
    close.alpha = 0;
}

- (void)showHeader {
    [bar removeAnimationForKey:@"yz.header"];
    [rule removeAnimationForKey:@"yz.header"];
    bar.opacity = 1;
    rule.opacity = 1;
    title.alpha = 1;
    close.alpha = 1;
}

// the pages overlap, and a page is as tall as the screen: a touch on the strip of one that shows must reach that page and not the
// one in front, whose bounds reach far over it
- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event {
    (void)event;
    CGRect bounds = self.bounds;
    return point.x >= 0 && point.x <= bounds.size.width && point.y >= 0 && point.y <= MIN(bounds.size.height, visible > 0 ? visible : bounds.size.height);
}

// a page is its title bar and a window on the page of the screen's proportions: the picture is fitted to the width from the top and what does
// not fit is cut off at the foot, as in Safari. a picture of another shape (a page kept in landscape, a short one) is never stretched to fill it
- (CGFloat)heightForWidth:(CGFloat)width bodyAspect:(CGFloat)aspect {
    return YZStackHeader + 1 + ceil(width * aspect);
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect bounds = self.bounds;
    plate.frame = bounds;
    open.frame = bounds;
    close.frame = CGRectMake(0, 0, YZStackHeader + 8, YZStackHeader);
    title.frame = CGRectMake(YZStackHeader + 6, 0, MAX((CGFloat)0, bounds.size.width - 2 * (YZStackHeader + 6)), YZStackHeader);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    bar.frame = CGRectMake(0, 0, bounds.size.width, YZStackHeader);
    rule.frame = CGRectMake(0, YZStackHeader, bounds.size.width, 1.0f / [[UIScreen mainScreen] scale]);
    [CATransaction commit];
    CGRect body = CGRectMake(0, YZStackHeader + 1, bounds.size.width, MAX((CGFloat)0, bounds.size.height - YZStackHeader - 1));
    glyph.frame = body;
    UIImage *preview = picture.image;
    if (preview && preview.size.width > 0 && preview.size.height > 0) {
        // fitted to the width, from the top: the whole picture shows
        CGFloat scale = body.size.width / preview.size.width;
        picture.frame = CGRectMake(body.origin.x, body.origin.y, body.size.width, ceil(preview.size.height * scale));
    } else
        picture.frame = CGRectZero;
    self.layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:bounds cornerRadius:4].CGPath;
}

@end

@implementation YZTabStack
@synthesize delegate = _delegate;

+ (UIColor *)groundColor {
    return [YZTheme isIncognito] ? [UIColor colorWithRed:0.09f green:0.04f blue:0.16f alpha:1] : [UIColor colorWithRed:0.11f green:0.12f blue:0.15f alpha:1];
}

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.backgroundColor = [UIColor clearColor];
    _scroll = [[UIScrollView alloc] initWithFrame:CGRectZero];
    _scroll.delegate = self;
    _scroll.alwaysBounceVertical = YES;
    _scroll.showsVerticalScrollIndicator = NO;
    _scroll.clipsToBounds = YES;
    _scroll.decelerationRate = UIScrollViewDecelerationRateNormal;
    [self addSubview:_scroll];
    for (NSUInteger i = 0; i < 2; ++i) {
        UISwipeGestureRecognizer *swipe = [[[UISwipeGestureRecognizer alloc] initWithTarget:self action:@selector(groundSwiped:)] autorelease];
        swipe.direction = i ? UISwipeGestureRecognizerDirectionRight : UISwipeGestureRecognizerDirectionLeft;
        swipe.delegate = self;
        [self addGestureRecognizer:swipe];
    }
    _cards = [[NSMutableArray alloc] init];
    return self;
}

- (void)dealloc {
    [self stopRelax];
    _scroll.delegate = nil;
    [_scroll release];
    [_cards release];
    [_tabs release];
    [super dealloc];
}

- (void)applyTheme {
    for (YZStackCard *card in _cards)
        [card updateLook];
}

- (void)refreshThumbnails {
    for (YZStackCard *card in _cards)
        [card updateLook];
    [self setNeedsLayout];
}

- (CGFloat)cardWidth {
    return MIN(round(self.bounds.size.width * 0.9f), (CGFloat)640);
}

// the distance on the screen between the tops of two pages: over a quarter of the height, so each page shows its bar and a piece of its picture.
// fewer pages are spread so that the last one still stands at the foot
- (CGFloat)step {
    CGFloat height = self.bounds.size.height;
    CGFloat base = MAX((CGFloat)118, round(height * 0.29f));
    NSUInteger count = [_cards count];
    if (count < 2)
        return base;
    CGFloat fit = (height - [self floor] - 0.6f * base) / (count - 1);
    return MIN(MAX(base, round(fit)), round(height * 0.5f));
}

// the place of the first page under the top
- (CGFloat)floor {
    return MAX((CGFloat)20, round(self.bounds.size.height * 0.07f));
}

- (NSUInteger)indexOfCard:(YZStackCard *)card {
    return [_tabs indexOfObjectIdenticalTo:card->tab];
}

- (CGFloat)maxOffset {
    NSUInteger count = [_cards count];
    if (!count)
        return 0;
    // the stack scrolls until the strip of the last page is just above the bar, a page does not come up to fill the screen
    CGFloat step = [self step];
    return MAX((CGFloat)0, (count - 1) * step + [self floor] - (self.bounds.size.height - 0.55f * step));
}

- (void)showTabs:(NSArray *)tabs selected:(NSUInteger)selected {
    NSMutableArray *cards = [NSMutableArray arrayWithCapacity:[tabs count]];
    for (YZTab *tab in tabs) {
        YZStackCard *found = nil;
        for (YZStackCard *card in _cards) {
            if (card->tab == tab) {
                found = card;
                break;
            }
        }
        if (!found) {
            found = [[[YZStackCard alloc] initWithTab:tab target:self] autorelease];
            UIPanGestureRecognizer *pan = [[[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(cardPanned:)] autorelease];
            pan.delegate = self;
            [found addGestureRecognizer:pan];
            [_scroll addSubview:found];
        }
        found.userInteractionEnabled = YES;
        [found updateLook];
        [cards addObject:found];
    }
    for (YZStackCard *card in _cards) {
        if ([cards indexOfObjectIdenticalTo:card] == NSNotFound)
            [card removeFromSuperview];
    }
    BOOL closing = _closing;
    _closing = NO;
    [_cards setArray:cards];
    [tabs retain];
    [_tabs release];
    _tabs = tabs;
    _selected = selected;
    [self setNeedsLayout];
    if (closing && self.window && [YZTheme animationsEnabled]) {
        [UIView animateWithDuration:0.3 delay:0 options:UIViewAnimationOptionCurveEaseOut | UIViewAnimationOptionBeginFromCurrentState
                         animations:^{ [self layoutIfNeeded]; } completion:nil];
        return;
    }
    [self layoutIfNeeded];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width, height = self.bounds.size.height;
    if (width <= 0 || height <= 0)
        return;
    _scroll.frame = self.bounds;
    CGFloat step = [self step];
    for (NSUInteger i = 0; i < [_cards count]; ++i)
        ((YZStackCard *)[_cards objectAtIndex:i])->baseY = i * step;
    _scroll.contentSize = CGSizeMake(width, MAX([self maxOffset] + height, height + 1));
    [self placeCards];
}

// the size of a page whose top is at this place on the screen: full at the foot, YZStackFarScale at the first place
- (CGFloat)depthAtY:(CGFloat)drawn {
    CGFloat floorY = [self floor], span = MAX((CGFloat)1, self.bounds.size.height - floorY);
    CGFloat near = MIN((CGFloat)1, MAX((CGFloat)0, (drawn - floorY) / span));
    return YZStackFarScale + (1 - YZStackFarScale) * near;
}

- (void)placeCards {
    CGFloat width = self.bounds.size.width, offset = _scroll.contentOffset.y;
    CGFloat cardWidth = [self cardWidth];
    CGFloat step = [self step], floorY = [self floor];
    CGFloat aspect = [self bodyAspect], middle = self.bounds.size.height / 2;
    NSUInteger count = [_cards count];
    for (NSUInteger index = 0; index < count; ++index) {
        YZStackCard *card = [_cards objectAtIndex:index];
        // the order of the subviews is the order of the pages: the one that is later stands in front, for the eye and for touches. opening
        // a page used to leave it last and it took the touches of the pages that it covers
        if (index < [_scroll.subviews count] && [_scroll.subviews indexOfObjectIdenticalTo:card] != index)
            [_scroll insertSubview:card atIndex:index];
        // a page that is dragged or settling back is placed by its gesture, the scroll and the relax of the spread must not move it under the finger
        if (card == _panned)
            continue;
        // the top edge of a page is where it stands on the screen, at the pace of the finger, and a page that has gone past the first place
        // fades out over the next step
        CGFloat y = floorY + card->baseY - offset;
        CGFloat gone = y >= floorY ? 0 : MIN((CGFloat)1, (floorY - y) / (step * 0.9f));
        CGFloat cardHeight = [card heightForWidth:cardWidth bodyAspect:aspect];
        CGRect bounds = CGRectMake(0, 0, cardWidth, cardHeight);
        if (!CGRectEqualToRect(card.bounds, bounds)) {
            card.bounds = bounds;
            // laid out now, so a page that is new has its plate and its shadow when it is first drawn and not a shadow alone
            [card layoutIfNeeded];
        }
        // the cards live in the content of the scroll view, which moves them by the offset: the place on the screen is added to it. the spread
        // pushes each page away from the middle of the screen, the more the farther it is
        CGFloat drawn = y + _spread * ((y - middle) / MAX(step, (CGFloat)1));
        card.layer.position = CGPointMake(width / 2, drawn + offset);
        // the page grows toward the foot of the screen and is full size at the bottom edge; it scales about its top, the anchor of the card
        CGFloat depth = [self depthAtY:drawn];
        card.transform = CGAffineTransformMakeScale(depth, depth);
        // equal depth: what is in front is decided by the order of the subviews alone
        card.layer.zPosition = 0;
        card.alpha = gone >= 0.98f ? 0 : 1 - gone;
        // the strip of a page that no later page covers, in its own length: a scaled page is shorter on the screen by its scale
        card->visible = index + 1 < count ? MAX((CGFloat)YZStackHeader + 8, step / depth + 6) : cardHeight;
    }
}

// the proportions of the body of a page: those of the page of the browser, or of the stack when the browser does not say
- (CGFloat)bodyAspect {
    CGFloat aspect = 0;
    if ([_delegate respondsToSelector:@selector(tabStackPageFrame:)]) {
        CGRect page = [_delegate tabStackPageFrame:self];
        if (!CGRectIsNull(page) && page.size.width > 0)
            aspect = page.size.height / page.size.width;
    }
    if (aspect <= 0 && self.bounds.size.width > 0)
        aspect = self.bounds.size.height / self.bounds.size.width;
    return MIN(MAX(aspect, (CGFloat)0.5), (CGFloat)2.5);
}

- (void)stopRelax {
    [_relax invalidate];
    _relax = nil;
}

// the spread falls back to nothing a little after the finger and the deceleration stop pushing it
- (void)relaxTick:(CADisplayLink *)link {
    (void)link;
    _spread *= 0.84f;
    if (_spread < 0.1f || !self.userInteractionEnabled || !self.window) {
        _spread = 0;
        [self stopRelax];
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [self placeCards];
    [CATransaction commit];
}

- (void)scrollViewDidScroll:(UIScrollView *)scrollView {
    CFTimeInterval now = CACurrentMediaTime();
    CGFloat offset = scrollView.contentOffset.y;
    if ([YZTheme animationsEnabled] && _lastScrollTime > 0 && now > _lastScrollTime && self.userInteractionEnabled) {
        CGFloat speed = fabs(offset - _lastScrollOffset) / (now - _lastScrollTime);
        CGFloat pushed = MIN((CGFloat)10, speed * 0.01f);
        if (pushed > _spread)
            _spread = pushed;
        if (_spread > 0.1f && !_relax) {
            _relax = [CADisplayLink displayLinkWithTarget:self selector:@selector(relaxTick:)];
            [_relax addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
        }
    }
    _lastScrollTime = now;
    _lastScrollOffset = offset;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [self placeCards];
    [CATransaction commit];
}

- (void)present {
    [self layoutIfNeeded];
    for (YZStackCard *card in _cards)
        [card showHeader];
    CGFloat height = self.bounds.size.height;
    CGFloat offset = MIN([self maxOffset], _selected * [self step]);
    _scroll.contentOffset = CGPointMake(0, offset);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [self placeCards];
    [CATransaction commit];
    if (![YZTheme animationsEnabled] || _selected >= [_cards count])
        return;
    // each page comes in on the flat ease, the open one first and the others one after the other by their distance from it: the open page
    // settles from a little smaller, those below rise from under it and those above sink in from over it
    for (NSUInteger i = 0; i < [_cards count]; ++i) {
        YZStackCard *card = [_cards objectAtIndex:i];
        NSInteger distance = (NSInteger)i - (NSInteger)_selected;
        NSInteger away = distance < 0 ? -distance : distance;
        CFTimeInterval delay = MIN(0.24, 0.045 * away);
        CGFloat shift = distance > 0 ? height * 0.5f : (distance < 0 ? -height * 0.1f : 0);
        CGAffineTransform from = CGAffineTransformConcat(CGAffineTransformScale(card.transform, 0.94f, 0.94f), CGAffineTransformMakeTranslation(0, shift));
        YZMotionAnimateEnter(card, from, distance == 0 ? 1 : 0, YOZORA_MOTION_WOBBLE, delay);
    }
}

- (void)openSelected {
    if (_selected < [_cards count])
        [self openCard:[_cards objectAtIndex:_selected]];
    else
        [_delegate tabStack:self didSelectIndex:_selected];
}

- (void)cardOpened:(UIButton *)button {
    [self openCard:(YZStackCard *)button.superview];
}

- (void)openCard:(YZStackCard *)card {
    NSUInteger index = [self indexOfCard:card];
    if (index == NSNotFound)
        return;
    self.userInteractionEnabled = NO;
    CGFloat width = self.bounds.size.width, height = self.bounds.size.height;
    CGFloat offset = _scroll.contentOffset.y;
    [_scroll bringSubviewToFront:card];
    // the page lands where the browser draws its page, with its title bar above that place under the top bar of the browser. without a
    // place it fills the stack
    CGRect land = CGRectMake(0, 0, width, height);
    CGFloat headerHeight = 0;
    if ([_delegate respondsToSelector:@selector(tabStackPageFrame:)]) {
        CGRect page = [_delegate tabStackPageFrame:self];
        if (!CGRectIsNull(page) && page.size.width > 0 && page.size.height > 0) {
            CGFloat across = page.size.width / card.bounds.size.width;
            headerHeight = round((YZStackHeader + 1) * across);
            land = CGRectMake(page.origin.x, page.origin.y - headerHeight, page.size.width, page.size.height + headerHeight);
        }
    }
    BOOL toPage = headerHeight > 0;
    void (^grow)(void) = ^{
        // the page stands up, grows to the screen and the pages around it sink away
        for (YZStackCard *other in _cards) {
            if (other == card)
                continue;
            other.alpha = 0;
            other.layer.position = CGPointMake(other.layer.position.x, other.layer.position.y + height * 0.35f);
        }
        card.layer.transform = CATransform3DMakeScale(land.size.width / card.bounds.size.width, land.size.height / card.bounds.size.height, 1);
        card.layer.position = CGPointMake(CGRectGetMidX(land), offset + land.origin.y);
        if (toPage) {
            [card hideHeader:YES];
            if ([_delegate respondsToSelector:@selector(tabStackWillOpenPage:)])
                [_delegate tabStackWillOpenPage:self];
        }
    };
    void (^done)(BOOL) = ^(BOOL finished) {
        (void)finished;
        self.userInteractionEnabled = YES;
        [_delegate tabStack:self didSelectIndex:index];
    };
    if (![YZTheme animationsEnabled]) {
        grow();
        done(YES);
        return;
    }
    [UIView animateWithDuration:0.3 delay:0 options:UIViewAnimationOptionCurveEaseInOut animations:grow completion:done];
}

// the page leaves the way it was pushed: a page swiped to the right goes off to the right, the cross sends it to the left
- (void)dismissCard:(YZStackCard *)card {
    card.userInteractionEnabled = NO;
    _panned = card;
    CGFloat width = self.bounds.size.width;
    YZTab *tab = card->tab;
    CGFloat pushed = card.transform.tx;
    CGFloat depth = [self depthAtY:[self floor] + card->baseY - _scroll.contentOffset.y];
    CGAffineTransform gone = CGAffineTransformConcat(CGAffineTransformMakeScale(depth, depth), CGAffineTransformMakeTranslation((pushed > 0 ? 1 : -1) * width * 1.1f, 0));
    void (^remove)(BOOL) = ^(BOOL finished) {
        (void)finished;
        _panned = nil;
        NSUInteger index = [_tabs indexOfObjectIdenticalTo:tab];
        if (index != NSNotFound) {
            _closing = YES;
            [_delegate tabStack:self didCloseIndex:index];
        }
    };
    if (![YZTheme animationsEnabled]) {
        card.transform = gone;
        card.alpha = 0;
        remove(YES);
        return;
    }
    YZMotionAnimateView(card, gone, 0, YOZORA_MOTION_DISMISS, 0, remove);
}

- (void)cardClosed:(UIButton *)button {
    [self dismissCard:(YZStackCard *)button.superview];
}

// a swipe that starts on a page belongs to the page, which it closes
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gesture shouldReceiveTouch:(UITouch *)touch {
    if (![gesture isKindOfClass:[UISwipeGestureRecognizer class]])
        return YES;
    for (UIView *view = touch.view; view && view != self; view = view.superview) {
        if ([view isKindOfClass:[YZStackCard class]])
            return NO;
    }
    return YES;
}

- (void)groundSwiped:(UISwipeGestureRecognizer *)swipe {
    if (!self.userInteractionEnabled || swipe.state != UIGestureRecognizerStateEnded)
        return;
    if ([_delegate respondsToSelector:@selector(tabStack:didSwipeToward:)])
        [_delegate tabStack:self didSwipeToward:swipe.direction == UISwipeGestureRecognizerDirectionLeft ? -1 : 1];
}

- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer *)gesture {
    if (![gesture isKindOfClass:[UIPanGestureRecognizer class]])
        return YES;
    CGPoint speed = [(UIPanGestureRecognizer *)gesture velocityInView:self];
    return fabs(speed.x) > fabs(speed.y) * 1.4f;
}

- (void)cardPanned:(UIPanGestureRecognizer *)pan {
    YZStackCard *card = (YZStackCard *)pan.view;
    CGFloat width = self.bounds.size.width;
    CGFloat shift = [pan translationInView:self].x;
    CGFloat depth = [self depthAtY:[self floor] + card->baseY - _scroll.contentOffset.y];
    if (pan.state == UIGestureRecognizerStateBegan) {
        [card.layer removeAllAnimations];
        _panned = card;
    }
    if (pan.state == UIGestureRecognizerStateChanged && _panned == card) {
        // the page follows the finger by a translation, the place the layout gave it is not touched
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        card.transform = CGAffineTransformConcat(CGAffineTransformMakeScale(depth, depth), CGAffineTransformMakeTranslation(shift, 0));
        card.alpha = MAX((CGFloat)0.3f, 1 - fabs(shift) / (width * 0.9f));
        [CATransaction commit];
    } else if (pan.state == UIGestureRecognizerStateEnded || pan.state == UIGestureRecognizerStateCancelled || pan.state == UIGestureRecognizerStateFailed) {
        if (_panned != card)
            return;
        CGFloat speed = [pan velocityInView:self].x;
        if (pan.state == UIGestureRecognizerStateEnded && (fabs(shift) > width * 0.3f || fabs(speed) > 700)) {
            [self dismissCard:card];
            return;
        }
        // back to its place, the layout takes it over when it has arrived
        CGAffineTransform rest = CGAffineTransformMakeScale(depth, depth);
        void (^arrived)(BOOL) = ^(BOOL finished) {
            (void)finished;
            if (_panned == card)
                _panned = nil;
        };
        card.alpha = 1;
        if ([YZTheme animationsEnabled]) {
            YZMotionAnimateView(card, rest, 1, YOZORA_MOTION_WOBBLE, 0, arrived);
        } else {
            card.transform = rest;
            arrived(YES);
        }
    }
}

- (void)presentNewPage {
    self.userInteractionEnabled = NO;
    [self layoutIfNeeded];
    _scroll.contentOffset = CGPointMake(0, MIN([self maxOffset], _selected * [self step]));
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [self placeCards];
    [CATransaction commit];
    if (![YZTheme animationsEnabled] || _selected >= [_cards count]) {
        self.userInteractionEnabled = YES;
        [self openSelected];
        return;
    }
    YZStackCard *page = [_cards objectAtIndex:_selected];
    CGAffineTransform from = CGAffineTransformConcat(CGAffineTransformScale(page.transform, 0.9f, 0.9f), CGAffineTransformMakeTranslation(0, self.bounds.size.height * 0.3f));
    // the page opens when it has come in
    [CATransaction begin];
    [CATransaction setCompletionBlock:^{
        self.userInteractionEnabled = YES;
        [self openSelected];
    }];
    YZMotionAnimateEnter(page, from, 0, YOZORA_MOTION_WOBBLE, 0);
    [CATransaction commit];
}

@end
