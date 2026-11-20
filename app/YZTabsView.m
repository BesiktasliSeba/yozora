#import "YZTabsView.h"
#import "YZControls.h"
#import "YZFavicons.h"
#import "YZMotion.h"
#import "YZStrings.h"
#import "YZTab.h"
#import "YZTheme.h"
#import <QuartzCore/QuartzCore.h>

static const CGFloat YZCardGap = 12;
static const CGFloat YZCardTitleHeight = 36;
static const CGFloat YZCardPictureInset = 6;
static const CGFloat YZTrayHeaderHeight = 56;
static const CGFloat YZFabSize = 56;

@interface YZCardButton : UIButton {
    YZTab *_tab;
}
@property(nonatomic, retain) YZTab *tab;
@end
@implementation YZCardButton
@synthesize tab = _tab;
- (void)setHighlighted:(BOOL)highlighted {
    BOOL pressed = highlighted && !self.highlighted;
    [super setHighlighted:highlighted];
    if (pressed)
        [YZTheme animateFeedbackOfView:self.currentImage ? self.imageView : self.superview];
}
- (void)dealloc {
    [_tab release];
    [super dealloc];
}
@end

@interface YZTabCard : UIView {
@public
    YZCardButton *open;
    YZCardButton *close;
    UIImageView *picture;
    // shown over the empty picture of a tab that has no preview: the search glass, or the mask of the private session
    UIImageView *glyph;
    // the icon of the site in front of the title
    UIImageView *favicon;
    UILabel *title;
}
- (id)initWithTab:(YZTab *)tab target:(id)target;
- (void)updateSelected:(BOOL)selected;
@end

@implementation YZTabCard
- (id)initWithTab:(YZTab *)tab target:(id)target {
    self = [super initWithFrame:CGRectZero];
    if (!self)
        return nil;
    self.layer.cornerRadius = [YZTheme cardRadius];
    self.layer.masksToBounds = YES;
    picture = [[UIImageView alloc] initWithFrame:CGRectZero];
    picture.contentMode = UIViewContentModeScaleAspectFit;
    picture.clipsToBounds = YES;
    picture.layer.cornerRadius = [YZTheme cardRadius] - YZCardPictureInset;
    [self addSubview:picture];
    glyph = [[UIImageView alloc] initWithFrame:CGRectZero];
    glyph.contentMode = UIViewContentModeCenter;
    [picture addSubview:glyph];
    favicon = [[UIImageView alloc] initWithFrame:CGRectZero];
    favicon.contentMode = UIViewContentModeScaleAspectFit;
    [self addSubview:favicon];
    title = [[UILabel alloc] initWithFrame:CGRectZero];
    title.font = [YZTheme boldSystemFontOfSize:13];
    title.backgroundColor = [UIColor clearColor];
    [self addSubview:title];
    open = [[YZCardButton buttonWithType:UIButtonTypeCustom] retain];
    open.tab = tab;
    [open addTarget:target action:@selector(cardPressed:) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:open];
    close = [[YZCardButton buttonWithType:UIButtonTypeCustom] retain];
    close.tab = tab;
    [close addTarget:target action:@selector(closePressed:) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:close];
    return self;
}

- (void)updateSelected:(BOOL)selected {
    // a card is the container of the page it shows: the lowest surface on a light theme, the bar color on the dark ones
    self.backgroundColor = [YZTheme isDark] ? [YZTheme toolbarColor] : [YZTheme surfaceColor];
    self.layer.borderWidth = selected ? 2 : 0;
    self.layer.borderColor = [[YZTheme accentColor] CGColor];
    picture.backgroundColor = [YZTheme fieldColor];
    UIImage *preview = open.tab.thumbnail;
    if (picture.image != preview)
        picture.image = preview;
    // the site icon of a private tab is not looked up, the mask of the session stands in its place
    UIImage *siteIcon = open.tab.isPrivate ? nil : [YZFavicons iconForURL:open.tab.url];
    favicon.image = siteIcon;
    favicon.hidden = !siteIcon;
    glyph.hidden = preview != nil;
    if (preview)
        glyph.image = nil;
    else if (siteIcon)
        glyph.image = siteIcon;
    else
        glyph.image = [YZTheme icon:(open.tab.isPrivate ? YZIconIncognito : YZIconSearch) size:36 color:[[YZTheme subtleTextColor] colorWithAlphaComponent:0.6f]];
    // a large icon is shown at its own size, the glass and the mask are drawn to fit
    glyph.contentMode = siteIcon && !preview ? UIViewContentModeScaleAspectFit : UIViewContentModeCenter;
    [self setNeedsLayout];
    NSString *text = [open.tab.title length] ? open.tab.title : ([open.tab.url length] ? open.tab.url : YZT(@"Новая вкладка"));
    if (![title.text isEqualToString:text])
        title.text = text;
    title.textColor = [YZTheme textColor];
    [close setImage:[YZTheme icon:YZIconClose size:18 color:[YZTheme subtleTextColor]] forState:UIControlStateNormal];
    close.accessibilityLabel = YZT(@"Закрыть вкладку");
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width;
    picture.frame = CGRectMake(YZCardPictureInset, YZCardTitleHeight, width - 2 * YZCardPictureInset, self.bounds.size.height - YZCardTitleHeight - YZCardPictureInset);
    glyph.frame = picture.bounds;
    favicon.frame = CGRectMake(12, (YZCardTitleHeight - 16) / 2, 16, 16);
    CGFloat titleLeft = favicon.hidden ? 14 : 34;
    title.frame = CGRectMake(titleLeft, 0, width - titleLeft - YZCardTitleHeight, YZCardTitleHeight);
    // a large site icon stays well inside the empty picture
    if (glyph.contentMode == UIViewContentModeScaleAspectFit)
        glyph.frame = CGRectInset(picture.bounds, MAX((CGFloat)0, (picture.bounds.size.width - 56) / 2), MAX((CGFloat)0, (picture.bounds.size.height - 56) / 2));
    open.frame = self.bounds;
    close.frame = CGRectMake(width - YZCardTitleHeight, 0, YZCardTitleHeight, YZCardTitleHeight);
}

- (void)dealloc {
    [open release];
    [close release];
    [picture release];
    [glyph release];
    [favicon release];
    [title release];
    [super dealloc];
}
@end

// bounds and center do not depend on the transform of a view that a motion is scaling
static void YZPlaceView(UIView *view, CGRect frame) {
    view.bounds = CGRectMake(0, 0, frame.size.width, frame.size.height);
    view.center = CGPointMake(CGRectGetMidX(frame), CGRectGetMidY(frame));
}

@implementation YZTabsView
@synthesize delegate = _delegate;
@synthesize edgeInsets = _edgeInsets;
@synthesize incognito = _incognito;

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.backgroundColor = [YZTheme backdropColor];
    _scroll = [[YZScrollView alloc] initWithFrame:CGRectZero];
    _scroll.alwaysBounceVertical = YES;
    [self addSubview:_scroll];
    _header = [[UIView alloc] initWithFrame:CGRectZero];
    [self addSubview:_header];
    YZSegmentedPill *modes = [[YZSegmentedPill alloc] initWithTitles:[NSArray arrayWithObjects:YZT(@"Обычные"), YZT(@"Инкогнито"), nil]
                                                              icons:[NSArray arrayWithObjects:[NSNumber numberWithInt:YZIconTabs], [NSNumber numberWithInt:YZIconIncognito], nil]];
    [modes addTarget:self action:@selector(modeChanged:) forControlEvents:UIControlEventValueChanged];
    _modes = modes;
    [_header addSubview:_modes];
    _done = [[YZStateButton buttonWithType:UIButtonTypeCustom] retain];
    [(YZStateButton *)_done setPillInsets:UIEdgeInsetsMake(10, 4, 10, 4)];
    [_done setTitle:YZT(@"Готово") forState:UIControlStateNormal];
    [_done setTitleColor:[YZTheme accentColor] forState:UIControlStateNormal];
    _done.titleLabel.font = [YZTheme boldSystemFontOfSize:15];
    [_done addTarget:self action:@selector(donePressed) forControlEvents:UIControlEventTouchUpInside];
    [_header addSubview:_done];
    // the button of a new tab floats over the cards, a rounded square of the accent
    _newTab = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    _newTab.layer.cornerRadius = 18;
    _newTab.layer.shadowOpacity = 0.28f;
    _newTab.layer.shadowRadius = 8;
    _newTab.layer.shadowOffset = CGSizeMake(0, 3);
    _newTab.layer.shadowColor = [[UIColor blackColor] CGColor];
    _newTab.accessibilityLabel = YZT(@"Новая вкладка");
    [_newTab addTarget:self action:@selector(newTabPressed) forControlEvents:UIControlEventTouchUpInside];
    YZMotionAttachBounce(_newTab, 0.08f);
    [self addSubview:_newTab];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(iconLoaded:) name:YZFaviconDidLoadNotification object:nil];
    [self applyTheme];
    return self;
}

// the cards whose site icon arrived show it
- (void)iconLoaded:(NSNotification *)note {
    (void)note;
    YZTab *selected = _selected < [_tabs count] ? [_tabs objectAtIndex:_selected] : nil;
    for (YZTabCard *card in _cards)
        [card updateSelected:card->open.tab == selected];
}

- (void)setIncognito:(BOOL)incognito {
    _incognito = incognito;
    [(YZSegmentedPill *)_modes setSelectedSegment:incognito ? 1 : 0 animated:self.window != nil && !self.hidden];
}

- (CGPoint)modeSwitchCenterInView:(UIView *)view {
    // the circle opens from the segment that was chosen, not from the middle of the switch
    CGRect segment = _modes.bounds;
    segment.size.width /= 2;
    segment.origin.x = _incognito ? segment.size.width : 0;
    return [view convertPoint:CGPointMake(CGRectGetMidX(segment), CGRectGetMidY(segment)) fromView:_modes];
}

- (void)modeChanged:(YZSegmentedPill *)modes {
    _incognito = modes.selectedSegment == 1;
    if ([_delegate respondsToSelector:@selector(tabsView:didChooseIncognito:)])
        [_delegate tabsView:self didChooseIncognito:_incognito];
}

- (void)applyTheme {
    self.backgroundColor = [YZTheme backdropColor];
    [(YZSegmentedPill *)_modes setTitles:[NSArray arrayWithObjects:YZT(@"Обычные"), YZT(@"Инкогнито"), nil]];
    [(YZSegmentedPill *)_modes setSelectedSegment:_incognito ? 1 : 0];
    _newTab.backgroundColor = [YZTheme accentColor];
    [_newTab setImage:[YZTheme icon:YZIconPlus size:26 color:[YZTheme surfaceColor]] forState:UIControlStateNormal];
    _newTab.accessibilityLabel = YZT(@"Новая вкладка");
    [_done setTitle:YZT(@"Готово") forState:UIControlStateNormal];
    [_done setTitleColor:[YZTheme accentColor] forState:UIControlStateNormal];
    YZTab *selected = _selected < [_tabs count] ? [_tabs objectAtIndex:_selected] : nil;
    for (YZTabCard *card in _cards)
        [card updateSelected:card->open.tab == selected];
    [self setNeedsLayout];
}

- (void)refreshThumbnails {
    for (YZTabCard *card in _cards) {
        [card updateSelected:card->open.tab == (_selected < [_tabs count] ? [_tabs objectAtIndex:_selected] : nil)];
    }
}

- (void)newTabPressed { [_delegate tabsViewDidRequestNewTab:self]; }
- (void)donePressed { [_delegate tabsViewDidFinish:self]; }

- (void)cardPressed:(YZCardButton *)button {
    NSUInteger index = [_tabs indexOfObjectIdenticalTo:button.tab];
    if (index != NSNotFound && button.superview.superview == _scroll)
        [_delegate tabsView:self didSelectIndex:index];
}
- (void)closePressed:(YZCardButton *)button {
    YZTab *tab = button.tab;
    if ([_tabs indexOfObjectIdenticalTo:tab] == NSNotFound || button.superview.superview != _scroll)
        return;
    UIView *card = button.superview;
    button.userInteractionEnabled = NO;
    // another close may finish first, so the completion resolves the tab identity again
    YZMotionAnimateView(card, CGAffineTransformMakeScale(0.8f, 0.8f), 0, YOZORA_MOTION_DISMISS, 0, ^(BOOL finished) {
        (void)finished;
        NSUInteger current = [_tabs indexOfObjectIdenticalTo:tab];
        if (current != NSNotFound) {
            _animateNextLayout = YES;
            [_delegate tabsView:self didCloseIndex:current];
        }
    });
}

- (void)present {
    [self layoutIfNeeded];
    NSUInteger animated = 0;
    for (UIView *card in _cards) {
        if (!CGRectIntersectsRect(card.frame, _scroll.bounds))
            continue;
        // the cards rise and settle on the wobble curve, passing their place a little
        YZMotionAnimateEnter(card, CGAffineTransformConcat(CGAffineTransformMakeScale(0.9f, 0.9f), CGAffineTransformMakeTranslation(0, 28)), 0,
                             YOZORA_MOTION_WOBBLE, MIN((NSTimeInterval)0.16, animated * 0.025));
        if (++animated == 8)
            break;
    }
    YZMotionAnimateEnter(_newTab, CGAffineTransformMakeScale(0.4f, 0.4f), 0, YOZORA_MOTION_WOBBLE, 0.1);
}

- (void)showTabs:(NSArray *)tabs selected:(NSUInteger)selected {
    NSMutableArray *cards = [NSMutableArray arrayWithCapacity:[tabs count]];
    for (YZTab *tab in tabs) {
        YZTabCard *card = nil;
        for (YZTabCard *existing in _cards) {
            if (existing->open.tab == tab) {
                card = existing;
                break;
            }
        }
        if (!card) {
            card = [[[YZTabCard alloc] initWithTab:tab target:self] autorelease];
            [_scroll addSubview:card];
        }
        [card updateSelected:[cards count] == selected];
        [cards addObject:card];
    }
    for (YZTabCard *card in _cards) {
        if ([cards indexOfObjectIdenticalTo:card] == NSNotFound)
            [card removeFromSuperview];
    }
    [_cards release];
    _cards = [cards copy];
    [tabs retain];
    [_tabs release];
    _tabs = tabs;
    _selected = selected;
    [self setNeedsLayout];
    if (_animateNextLayout && !self.hidden && [YZTheme animationsEnabled]) {
        _animateNextLayout = NO;
        // moving cards must not take taps at their final positions before they arrive
        [UIView animateWithDuration:0.22 delay:0
                            options:UIViewAnimationOptionCurveEaseOut | UIViewAnimationOptionBeginFromCurrentState
                         animations:^{ [self layoutIfNeeded]; } completion:nil];
        return;
    }
    _animateNextLayout = NO;
    [self layoutIfNeeded];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width, height = self.bounds.size.height;
    if (width <= 0 || height <= 0)
        return;
    _header.frame = CGRectMake(0, _edgeInsets.top, width, YZTrayHeaderHeight);
    _modes.frame = CGRectMake(_edgeInsets.left + 16, (YZTrayHeaderHeight - 36) / 2, 216, 36);
    _done.frame = CGRectMake(width - _edgeInsets.right - 88, 0, 80, YZTrayHeaderHeight);
    CGFloat scrollTop = _edgeInsets.top + YZTrayHeaderHeight;
    // the cards scroll under the button of a new tab and can still be lifted clear of it
    CGFloat fabClearance = YZFabSize + 32;
    _scroll.frame = CGRectMake(0, scrollTop, width, height - scrollTop);
    _scroll.contentInset = UIEdgeInsetsMake(0, 0, fabClearance + _edgeInsets.bottom, 0);
    YZPlaceView(_newTab, CGRectMake(width - _edgeInsets.right - 16 - YZFabSize, height - _edgeInsets.bottom - 16 - YZFabSize, YZFabSize, YZFabSize));
    NSUInteger columns = width > 400 ? 3 : 2;
    CGFloat cardWidth = (width - YZCardGap * (columns + 1)) / columns;
    CGFloat thumbnailHeight = cardWidth * 1.2f;
    CGFloat cardHeight = YZCardTitleHeight + thumbnailHeight;
    for (NSUInteger i = 0; i < [_cards count]; ++i) {
        YZTabCard *card = [_cards objectAtIndex:i];
        CGRect frame = CGRectMake(YZCardGap + (i % columns) * (cardWidth + YZCardGap),
                                  YZCardGap + (i / columns) * (cardHeight + YZCardGap), cardWidth, cardHeight);
        // bounds and center remain defined while a closing card has a scale transform
        CGRect bounds = CGRectMake(0, 0, cardWidth, cardHeight);
        CGPoint center = CGPointMake(CGRectGetMidX(frame), CGRectGetMidY(frame));
        if (!CGRectEqualToRect(card.bounds, bounds))
            card.bounds = bounds;
        if (!CGPointEqualToPoint(card.center, center))
            card.center = center;
    }
    NSUInteger rows = ([_tabs count] + columns - 1) / columns;
    _scroll.contentSize = CGSizeMake(width, YZCardGap + rows * (cardHeight + YZCardGap));
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_scroll release];
    [_header release];
    [_modes release];
    [_newTab release];
    [_done release];
    [_tabs release];
    [_cards release];
    [super dealloc];
}
@end
