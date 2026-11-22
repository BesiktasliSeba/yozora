#import "YZTabsView.h"
#import "YZClassic.h"
#import "YZControls.h"
#import "YZFavicons.h"
#import "YZMotion.h"
#import "YZSettings.h"
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
    BOOL classic = [YZTheme isClassic];
    self.layer.cornerRadius = [YZTheme cardRadius];
    picture.layer.cornerRadius = classic ? 3 : [YZTheme cardRadius] - YZCardPictureInset;
    if (classic) {
        // a classic card is a lit plate with an outline, the chosen one outlined in the accent
        self.backgroundColor = [YZTheme cardColor];
        self.layer.borderWidth = selected ? 3 : 1;
        self.layer.borderColor = [(selected ? [YZClassic barMidColor] : [YZTheme separatorColor]) CGColor];
    } else {
        self.backgroundColor = [YZTheme isDark] ? [YZTheme toolbarColor] : [YZTheme surfaceColor];
        self.layer.borderWidth = selected ? 2 : 0;
        self.layer.borderColor = [[YZTheme accentColor] CGColor];
    }
    picture.backgroundColor = [YZTheme isClassic] ? [YZTheme surfaceColor] : [YZTheme fieldColor];
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
    _pager = [[YZTabPager alloc] initWithFrame:CGRectZero];
    _pager.delegate = self;
    _pager.hidden = YES;
    [self addSubview:_pager];
    // the bar of the system with the two buttons of the page switcher of ios 5 and 6
    _toolbar = [[UIToolbar alloc] initWithFrame:CGRectZero];
    _toolbar.hidden = YES;
    _toolbarDone = [[UIBarButtonItem alloc] initWithTitle:YZT(@"Готово") style:UIBarButtonItemStyleDone target:self action:@selector(donePressed)];
    UIBarButtonItem *space = [[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil] autorelease];
    // done only: a new page is made by the page with the plus at the end of the row
    _toolbar.items = [NSArray arrayWithObjects:space, _toolbarDone, nil];
    [self addSubview:_toolbar];
    _pagesBar = [[UIView alloc] initWithFrame:CGRectZero];
    _pagesBar.hidden = YES;
    [self addSubview:_pagesBar];
    _pagesBarLine = [[UIView alloc] initWithFrame:CGRectZero];
    [_pagesBar addSubview:_pagesBarLine];
    _pagesDone = [[YZStateButton buttonWithType:UIButtonTypeCustom] retain];
    [(YZStateButton *)_pagesDone setPillInsets:UIEdgeInsetsMake(6, 4, 6, 4)];
    [_pagesDone addTarget:self action:@selector(donePressed) forControlEvents:UIControlEventTouchUpInside];
    [_pagesBar addSubview:_pagesDone];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(iconLoaded:) name:YZFaviconDidLoadNotification object:nil];
    [self applyTheme];
    return self;
}

// the setting can change between two openings of the tray: what each layout needs is shown or hidden here
- (void)applyLayoutMode {
    BOOL pages = [self showsPages], bar = [self usesToolbar];
    if (_pager.hidden != !pages || _toolbar.hidden != !bar || _pagesBar.hidden != ![self usesPagesBar])
        [self applyTheme];
}

- (BOOL)showsPages { return [YZSettings tabsLayout] == YZTabsLayoutPages; }
// the classic pages stand over the toolbar of the system, the modern ones keep the header and the floating button
- (BOOL)usesToolbar { return [self showsPages] && [YZTheme isClassic]; }
// the modern pages stand over a flat bar of two buttons instead of the floating button
- (BOOL)usesPagesBar { return [self showsPages] && ![YZTheme isClassic]; }

- (void)tabPager:(YZTabPager *)pager didSelectIndex:(NSUInteger)index {
    (void)pager;
    [_delegate tabsView:self didSelectIndex:index];
}

- (void)tabPagerDidRequestNewTab:(YZTabPager *)pager {
    (void)pager;
    [_delegate tabsViewDidRequestNewTab:self];
}

- (void)tabPager:(YZTabPager *)pager didCloseIndex:(NSUInteger)index {
    (void)pager;
    [_delegate tabsView:self didCloseIndex:index];
}

// the cards whose site icon arrived show it
- (void)iconLoaded:(NSNotification *)note {
    (void)note;
    YZTab *selected = _selected < [_tabs count] ? [_tabs objectAtIndex:_selected] : nil;
    for (YZTabCard *card in _cards)
        [card updateSelected:card->open.tab == selected];
    [_pager refreshThumbnails];
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
    self.backgroundColor = [YZTheme isClassic] ? [YZTheme screenColor] : [YZTheme backdropColor];
    BOOL pages = [self showsPages], bar = [self usesToolbar];
    _scroll.hidden = pages;
    _pager.hidden = !pages;
    BOOL pagesBar = [self usesPagesBar];
    _toolbar.hidden = !bar;
    _pagesBar.hidden = !pagesBar;
    // the header's done button and the floating plus belong to the grid, the pages have their own two buttons
    _done.hidden = pages;
    _newTab.hidden = pages;
    if (pagesBar) {
        _pagesBar.backgroundColor = [YZTheme toolbarColor];
        _pagesBarLine.backgroundColor = [YZTheme separatorColor];
        [_pagesDone setTitle:YZT(@"Готово") forState:UIControlStateNormal];
        [_pagesDone setTitleColor:[YZTheme accentColor] forState:UIControlStateNormal];
        _pagesDone.titleLabel.font = [YZTheme boldSystemFontOfSize:15];
    }
    if (bar) {
        // the ground behind the pages of ios 5 and 6: blue grey, lighter at the top
        if (!_ground) {
            _ground = [[CAGradientLayer alloc] init];
            [self.layer insertSublayer:_ground atIndex:0];
        }
        // the accent of the theme, lighter at the top and deep at the foot, in place of the blue grey of Safari
        CGFloat r = 0, g = 0, b = 0, a = 1;
        [[YZClassic barMidColor] getRed:&r green:&g blue:&b alpha:&a];
        UIColor *top = [UIColor colorWithRed:r + (1 - r) * 0.22f green:g + (1 - g) * 0.22f blue:b + (1 - b) * 0.22f alpha:1];
        UIColor *foot = [UIColor colorWithRed:r * 0.5f green:g * 0.5f blue:b * 0.5f alpha:1];
        _ground.colors = [NSArray arrayWithObjects:(id)[top CGColor], (id)[foot CGColor], nil];
        _ground.hidden = NO;
        self.backgroundColor = foot;
        // the done key is the accent too, the system draws it blue unless it is told
        _toolbarDone.tintColor = [YZTheme accentColor];
        if ([_toolbar respondsToSelector:@selector(setBarTintColor:)])
            [_toolbar setValue:[YZClassic barMidColor] forKey:@"barTintColor"];
        else
            _toolbar.tintColor = [YZClassic barMidColor];
        _toolbarDone.title = YZT(@"Готово");
    } else {
        _ground.hidden = YES;
    }
    [_pager applyTheme];
    [(YZSegmentedPill *)_modes setTitles:[NSArray arrayWithObjects:YZT(@"Обычные"), YZT(@"Инкогнито"), nil]];
    [(YZSegmentedPill *)_modes setSelectedSegment:_incognito ? 1 : 0];
    if ([YZTheme isClassic]) {
        // a glossy key with a white plus stamped on it
        _newTab.layer.cornerRadius = 0;
        _newTab.backgroundColor = [UIColor clearColor];
        [_newTab setBackgroundImage:nil forState:UIControlStateNormal];
        [_newTab setBackgroundImage:nil forState:UIControlStateHighlighted];
        [YZClassic lightView:_newTab color:[YZClassic barMidColor] radius:10];
        _newTab.showsTouchWhenHighlighted = YES;
        [_newTab setImage:[YZClassic stampedImage:[YZTheme icon:YZIconPlus size:26 color:[UIColor whiteColor]]] forState:UIControlStateNormal];
    } else {
        [YZClassic unlightView:_newTab];
        _newTab.showsTouchWhenHighlighted = NO;
        _newTab.layer.cornerRadius = 18;
        [_newTab setBackgroundImage:nil forState:UIControlStateNormal];
        [_newTab setBackgroundImage:nil forState:UIControlStateHighlighted];
        _newTab.backgroundColor = [YZTheme accentColor];
        [_newTab setImage:[YZTheme icon:YZIconPlus size:26 color:[YZTheme surfaceColor]] forState:UIControlStateNormal];
    }
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
    if ([self showsPages]) {
        [_pager focusSelected];
        return;
    }
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
    [self applyLayoutMode];
    if ([self showsPages]) {
        // the pages are the other view of the same tabs, the grid keeps no cards meanwhile
        for (YZTabCard *card in _cards)
            [card removeFromSuperview];
        [_cards release];
        _cards = nil;
        [tabs retain];
        [_tabs release];
        _tabs = tabs;
        _selected = selected;
        [_pager showTabs:tabs selected:selected];
        [self setNeedsLayout];
        return;
    }
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
    // the pages stand in the middle of the screen and so does the switch above them, the grid keeps it at the left edge
    CGFloat modesLeft = [self showsPages] ? floor((width - 216) / 2) : _edgeInsets.left + 16;
    _modes.frame = CGRectMake(modesLeft, (YZTrayHeaderHeight - 36) / 2, 216, 36);
    _done.frame = CGRectMake(width - _edgeInsets.right - 88, 0, 80, YZTrayHeaderHeight);
    CGFloat scrollTop = _edgeInsets.top + YZTrayHeaderHeight;
    CGFloat barHeight = 44 + _edgeInsets.bottom;
    if ([self usesToolbar]) {
        _toolbar.frame = CGRectMake(0, height - barHeight, width, barHeight);
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        _ground.frame = self.bounds;
        [CATransaction commit];
    }
    CGFloat pagesBarHeight = 52 + _edgeInsets.bottom;
    if ([self usesPagesBar]) {
        _pagesBar.frame = CGRectMake(0, height - pagesBarHeight, width, pagesBarHeight);
        _pagesBarLine.frame = CGRectMake(0, 0, width, 1);
        _pagesDone.frame = CGRectMake(width - _edgeInsets.right - 88, 0, 80, 52);
    }
    CGFloat below = [self usesToolbar] ? barHeight : ([self usesPagesBar] ? pagesBarHeight : 0);
    _pager.frame = CGRectMake(0, scrollTop, width, height - scrollTop - below);
    // the cards scroll under the button of a new tab and can still be lifted clear of it
    CGFloat fabClearance = YZFabSize + 32;
    _scroll.frame = CGRectMake(0, scrollTop, width, height - scrollTop);
    _scroll.contentInset = UIEdgeInsetsMake(0, 0, fabClearance + _edgeInsets.bottom, 0);
    YZPlaceView(_newTab, CGRectMake(width - _edgeInsets.right - 16 - YZFabSize, height - _edgeInsets.bottom - 16 - YZFabSize, YZFabSize, YZFabSize));
    // the pages have no cards to place
    if ([self showsPages])
        return;
    // an ipad shows more and smaller cards: three columns of 320 points would be pictures the size of a phone
    NSUInteger columns = [YZTheme isPad] ? MIN((NSUInteger)5, MAX((NSUInteger)3, (NSUInteger)(width / 180))) : (width > 400 ? 3 : 2);
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
    _pager.delegate = nil;
    [_pager release];
    [_toolbar release];
    [_toolbarDone release];
    [_pagesBar release];
    [_pagesBarLine release];
    [_pagesDone release];
    [_ground release];
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
