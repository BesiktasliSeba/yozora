#import "YZTabPager.h"
#import "YZClassic.h"
#import "YZCompat.h"
#import "YZStrings.h"
#import "YZTab.h"
#import "YZTheme.h"
#import <QuartzCore/QuartzCore.h>

// the room between two pages, the badge hangs out over the corner of a page
static const CGFloat YZPageGap = 28;
static const CGFloat YZBadgeSide = 30;
static const CGFloat YZDotsHeight = 24;

// the badge that closes a page: a red disc with a white rim and a white cross in the classic design (made once), a flat disc of the
// card color in the modern one (made again, it follows the theme)
static UIImage *YZBadgeImage(void) {
    static UIImage *classicImage;
    static UIColor *classicTint;
    BOOL classic = [YZTheme isClassic];
    UIColor *tint = classic ? [YZClassic barMidColor] : nil;
    if (classic && classicImage && [classicTint isEqual:tint])
        return classicImage;
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(YZBadgeSide, YZBadgeSide), NO, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGRect disc = CGRectInset(CGRectMake(0, 0, YZBadgeSide, YZBadgeSide), 3, 3);
    if (classic) {
        CGContextSaveGState(context);
        CGContextSetShadowWithColor(context, CGSizeMake(0, 2), 3, [[UIColor colorWithWhite:0 alpha:0.55f] CGColor]);
        [[UIColor whiteColor] setFill];
        CGContextFillEllipseInRect(context, disc);
        CGContextRestoreGState(context);
        CGRect red = CGRectInset(disc, 2, 2);
        CGContextSaveGState(context);
        CGContextAddEllipseInRect(context, red);
        CGContextClip(context);
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        // the accent of the bars, lit above and deep below, where the badge of Safari is red
        CGFloat r = 0, g = 0, b = 0, a = 1;
        [[YZClassic barMidColor] getRed:&r green:&g blue:&b alpha:&a];
        CGFloat colors[8] = { MIN((CGFloat)1, r + (1 - r) * 0.3f), MIN((CGFloat)1, g + (1 - g) * 0.3f), MIN((CGFloat)1, b + (1 - b) * 0.3f), 1,
                              r * 0.6f, g * 0.6f, b * 0.6f, 1 };
        CGGradientRef gradient = CGGradientCreateWithColorComponents(space, colors, NULL, 2);
        CGContextDrawLinearGradient(context, gradient, CGPointMake(0, CGRectGetMinY(red)), CGPointMake(0, CGRectGetMaxY(red)), 0);
        CGGradientRelease(gradient);
        CGColorSpaceRelease(space);
        CGContextRestoreGState(context);
    } else {
        CGContextSaveGState(context);
        CGContextSetShadowWithColor(context, CGSizeMake(0, 1), 3, [[UIColor colorWithWhite:0 alpha:0.35f] CGColor]);
        [([YZTheme isDark] ? [YZTheme cardColor] : [UIColor whiteColor]) setFill];
        CGContextFillEllipseInRect(context, disc);
        CGContextRestoreGState(context);
    }
    [classic ? [UIColor whiteColor] : [YZTheme textColor] setStroke];
    CGContextSetLineWidth(context, classic ? 3 : 2.2f);
    CGContextSetLineCap(context, kCGLineCapRound);
    CGFloat c = YZBadgeSide / 2, r = 5;
    CGContextMoveToPoint(context, c - r, c - r);
    CGContextAddLineToPoint(context, c + r, c + r);
    CGContextMoveToPoint(context, c + r, c - r);
    CGContextAddLineToPoint(context, c - r, c + r);
    CGContextStrokePath(context);
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    if (classic) {
        [classicImage release];
        [classicTint release];
        classicImage = [image retain];
        classicTint = [tint retain];
    }
    return image;
}

@interface YZPagerPage : UIView {
@public
    YZTab *tab;
    UIView *card;
    UIImageView *picture;
    UIImageView *glyph;
    UIButton *open;
    UIButton *close;
}
- (id)initWithTab:(YZTab *)tab target:(id)target;
- (void)updateLook;
@end

@implementation YZPagerPage

- (id)initWithTab:(YZTab *)aTab target:(id)target {
    self = [super initWithFrame:CGRectZero];
    if (!self)
        return nil;
    tab = [aTab retain];
    card = [[UIView alloc] initWithFrame:CGRectZero];
    [self addSubview:card];
    picture = [[UIImageView alloc] initWithFrame:CGRectZero];
    picture.userInteractionEnabled = NO;
    [card addSubview:picture];
    glyph = [[UIImageView alloc] initWithFrame:CGRectZero];
    glyph.contentMode = UIViewContentModeCenter;
    glyph.userInteractionEnabled = NO;
    [card addSubview:glyph];
    open = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    open.tag = 1;
    [open addTarget:target action:@selector(pageOpened:) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:open];
    close = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    close.tag = 2;
    close.accessibilityLabel = YZT(@"Закрыть вкладку");
    [close addTarget:target action:@selector(pageClosed:) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:close];
    [self updateLook];
    return self;
}

- (void)dealloc {
    [tab release];
    [card release];
    [picture release];
    [glyph release];
    [open release];
    [close release];
    [super dealloc];
}

// the picture is the top of the page at the width of the card, what does not fit is cut at the foot
- (void)updateLook {
    BOOL classic = [YZTheme isClassic];
    card.backgroundColor = classic ? [UIColor whiteColor] : (([YZTheme isDark]) ? [YZTheme cardColor] : [UIColor whiteColor]);
    card.layer.cornerRadius = classic ? 0 : [YZTheme cardRadius];
    card.layer.masksToBounds = YES;
    // the shadow lies on the page, the corners are cut on the card inside it, a view that clips has no shadow
    self.layer.shadowColor = [[UIColor blackColor] CGColor];
    self.layer.shadowOpacity = classic ? 0.6f : 0.3f;
    self.layer.shadowRadius = classic ? 6 : 10;
    self.layer.shadowOffset = CGSizeMake(0, classic ? 3 : 4);
    UIImage *preview = tab.thumbnail;
    if (picture.image != preview)
        picture.image = preview;
    glyph.hidden = preview != nil;
    if (!preview)
        glyph.image = [YZTheme icon:(tab.isPrivate ? YZIconIncognito : YZIconSearch) size:40
                              color:[UIColor colorWithWhite:classic ? 0.55f : 0.5f alpha:1]];
    [close setImage:YZBadgeImage() forState:UIControlStateNormal];
    [self setNeedsLayout];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect bounds = self.bounds;
    card.frame = bounds;
    glyph.frame = bounds;
    open.frame = bounds;
    UIImage *preview = picture.image;
    if (preview && preview.size.width > 0 && preview.size.height > 0) {
        // the picture fills the page: scaled until it covers the width and the height, its top at the top of the page and what is below the
        // page cut off. a picture that is shorter than the page left the foot of it bare
        CGFloat scale = MAX(bounds.size.width / preview.size.width, bounds.size.height / preview.size.height);
        CGFloat width = ceil(preview.size.width * scale), height = ceil(preview.size.height * scale);
        picture.frame = CGRectMake(floor((bounds.size.width - width) / 2), 0, width, height);
    } else
        picture.frame = CGRectZero;
    self.layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:bounds cornerRadius:card.layer.cornerRadius].CGPath;
    // the badge sits on the upper left corner and reaches out of the page
    close.frame = CGRectMake(-YZBadgeSide / 2 + 6, -YZBadgeSide / 2 + 6, YZBadgeSide, YZBadgeSide);
}

// the badge reaches outside the page, so a touch on it still has to find it
- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event {
    return [super pointInside:point withEvent:event] || CGRectContainsPoint(close.frame, point);
}
@end

@implementation YZTabPager
@synthesize delegate = _delegate;

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    _current = -1;
    _pages = [[NSMutableArray alloc] init];
    _title = [[UILabel alloc] initWithFrame:CGRectZero];
    _title.textAlignment = YZTextAlignmentCenter;
    _title.backgroundColor = [UIColor clearColor];
    _title.lineBreakMode = YZLineBreakTailTruncation;
    [self addSubview:_title];
    _address = [[UILabel alloc] initWithFrame:CGRectZero];
    _address.textAlignment = YZTextAlignmentCenter;
    _address.backgroundColor = [UIColor clearColor];
    _address.lineBreakMode = YZLineBreakTailTruncation;
    YZShrinkToFit(_title);
    YZShrinkToFit(_address);
    [self addSubview:_address];
    _scroll = [[UIScrollView alloc] initWithFrame:CGRectZero];
    _scroll.pagingEnabled = YES;
    _scroll.clipsToBounds = NO;
    _scroll.showsHorizontalScrollIndicator = NO;
    _scroll.showsVerticalScrollIndicator = NO;
    _scroll.delegate = self;
    [self addSubview:_scroll];
    // the empty page at the end of the row: a plus on a plate, the way a page is added by paging on
    _plus = [[UIView alloc] initWithFrame:CGRectZero];
    [_scroll addSubview:_plus];
    _plusIcon = [[UIImageView alloc] initWithFrame:CGRectZero];
    _plusIcon.contentMode = UIViewContentModeCenter;
    _plusIcon.userInteractionEnabled = NO;
    [_plus addSubview:_plusIcon];
    _plusCaption = [[UILabel alloc] initWithFrame:CGRectZero];
    _plusCaption.textAlignment = YZTextAlignmentCenter;
    _plusCaption.backgroundColor = [UIColor clearColor];
    _plusCaption.userInteractionEnabled = NO;
    [_plus addSubview:_plusCaption];
    UIButton *plusButton = [UIButton buttonWithType:UIButtonTypeCustom];
    plusButton.frame = CGRectZero;
    plusButton.tag = 5;
    plusButton.accessibilityLabel = YZT(@"Новая вкладка");
    [plusButton addTarget:self action:@selector(plusTapped) forControlEvents:UIControlEventTouchUpInside];
    [_plus addSubview:plusButton];
    _dots = [[UIPageControl alloc] initWithFrame:CGRectZero];
    [_dots addTarget:self action:@selector(dotsChanged:) forControlEvents:UIControlEventValueChanged];
    [self addSubview:_dots];
    [self applyTheme];
    return self;
}

- (void)dealloc {
    _scroll.delegate = nil;
    [_title release];
    [_address release];
    [_scroll release];
    [_dots release];
    [_plus release];
    [_plusIcon release];
    [_plusCaption release];
    [_tabs release];
    [_pages release];
    [super dealloc];
}

- (void)applyTheme {
    BOOL classic = [YZTheme isClassic];
    if (classic) {
        _title.font = [UIFont boldSystemFontOfSize:22];
        _title.textColor = [UIColor whiteColor];
        _title.shadowColor = [UIColor colorWithWhite:0 alpha:0.5f];
        _title.shadowOffset = CGSizeMake(0, -1);
        _address.font = [UIFont systemFontOfSize:17];
        _address.textColor = [UIColor colorWithWhite:0.86f alpha:1];
        _address.shadowColor = [UIColor colorWithWhite:0 alpha:0.5f];
        _address.shadowOffset = CGSizeMake(0, -1);
    } else {
        _title.font = [YZTheme boldSystemFontOfSize:20];
        _title.textColor = [YZTheme textColor];
        _title.shadowColor = nil;
        _address.font = [YZTheme systemFontOfSize:14];
        _address.textColor = [YZTheme subtleTextColor];
        _address.shadowColor = nil;
    }
    // ios 6 and later color the dots, the system of ios 5 draws them white
    if ([_dots respondsToSelector:NSSelectorFromString(@"setPageIndicatorTintColor:")]) {
        [_dots setValue:classic ? [UIColor colorWithWhite:1 alpha:0.35f] : [[YZTheme subtleTextColor] colorWithAlphaComponent:0.4f] forKey:@"pageIndicatorTintColor"];
        [_dots setValue:classic ? [UIColor whiteColor] : [YZTheme accentColor] forKey:@"currentPageIndicatorTintColor"];
    }
    for (YZPagerPage *page in _pages)
        [page updateLook];
    // the plate of the empty page looks like a page, drawn lighter, with a plus on it
    _plus.backgroundColor = classic ? [UIColor colorWithWhite:1 alpha:0.18f] : [YZTheme cardColor];
    _plus.layer.cornerRadius = classic ? 0 : [YZTheme cardRadius];
    _plus.layer.borderWidth = classic ? 1 : 0;
    _plus.layer.borderColor = [[UIColor colorWithWhite:1 alpha:0.4f] CGColor];
    _plusIcon.image = [YZTheme icon:YZIconPlus size:56 color:classic ? [UIColor colorWithWhite:1 alpha:0.85f] : [YZTheme accentColor]];
    _plusCaption.text = YZT(@"Новая вкладка");
    _plusCaption.font = classic ? [UIFont boldSystemFontOfSize:15] : [YZTheme boldSystemFontOfSize:14];
    _plusCaption.textColor = classic ? [UIColor colorWithWhite:1 alpha:0.85f] : [YZTheme textColor];
    _plusCaption.shadowColor = classic ? [UIColor colorWithWhite:0 alpha:0.5f] : nil;
    _plusCaption.shadowOffset = CGSizeMake(0, -1);
    [self showCurrentText];
    [self setNeedsLayout];
}

- (void)refreshThumbnails {
    for (YZPagerPage *page in _pages)
        [page updateLook];
}

- (void)showCurrentText {
    YZTab *tab = _current >= 0 && _current < (NSInteger)[_tabs count] ? [_tabs objectAtIndex:(NSUInteger)_current] : nil;
    NSString *title = [tab.title length] ? tab.title : YZT(@"Новая вкладка");
    // the page after the last tab has the name of what it makes
    _title.text = tab || _current == (NSInteger)[_tabs count] ? title : @"";
    _address.text = [tab.url length] ? tab.url : @"";
}

- (CGFloat)stride {
    return _scroll.bounds.size.width;
}

- (void)setCurrent:(NSInteger)index {
    if (index == _current)
        return;
    _current = index;
    _dots.currentPage = index < 0 ? 0 : index;
    [self showCurrentText];
}

- (void)showTabs:(NSArray *)tabs selected:(NSUInteger)selected {
    NSMutableArray *pages = [NSMutableArray arrayWithCapacity:[tabs count]];
    for (YZTab *tab in tabs) {
        YZPagerPage *page = nil;
        for (YZPagerPage *existing in _pages) {
            if (existing->tab == tab) {
                page = existing;
                break;
            }
        }
        if (!page) {
            page = [[[YZPagerPage alloc] initWithTab:tab target:self] autorelease];
            [_scroll addSubview:page];
        } else
            [page updateLook];
        [pages addObject:page];
    }
    for (YZPagerPage *page in _pages) {
        if ([pages indexOfObjectIdenticalTo:page] == NSNotFound)
            [page removeFromSuperview];
    }
    [_pages setArray:pages];
    [tabs retain];
    [_tabs release];
    _tabs = tabs;
    _selected = selected;
    _dots.numberOfPages = (NSInteger)[tabs count] + 1;
    _requested = NO;
    NSInteger keep = _current >= 0 ? MIN(_current, (NSInteger)[tabs count] - 1) : (NSInteger)MIN(selected, [tabs count] ? [tabs count] - 1 : 0);
    _current = -2;
    [self setNeedsLayout];
    [self layoutIfNeeded];
    [self setCurrent:keep];
    _scroll.contentOffset = CGPointMake([self stride] * (CGFloat)MAX(keep, (NSInteger)0), 0);
}

- (void)focusSelected {
    _requested = NO;
    if (![_tabs count])
        return;
    NSInteger index = (NSInteger)MIN(_selected, [_tabs count] - 1);
    [self setCurrent:index];
    _scroll.contentOffset = CGPointMake([self stride] * (CGFloat)index, 0);
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width, height = self.bounds.size.height;
    if (width <= 0 || height <= 0)
        return;
    BOOL wide = width > height;
    CGFloat y = wide ? 2 : 10;
    CGFloat lineGap = wide ? 0 : 2;
    _title.frame = CGRectMake(16, y, width - 32, 28);
    _address.frame = CGRectMake(16, CGRectGetMaxY(_title.frame) + lineGap, width - 32, 22);
    _dots.frame = CGRectMake(0, height - YZDotsHeight - 6, width, YZDotsHeight);
    CGFloat top = CGRectGetMaxY(_address.frame) + (wide ? 14 : 24), bottom = CGRectGetMinY(_dots.frame) - 12;
    CGFloat area = MAX((CGFloat)60, bottom - top);
    CGFloat pageWidth = MIN(floor(width * 0.6f), floor(area * 0.77f));
    CGFloat stride = pageWidth + YZPageGap;
    // the scroll view is as wide as one step and stands in the middle: pages beyond its edges show because it does not clip
    _scroll.frame = CGRectMake(floor((width - stride) / 2), top, stride, area);
    // one page more than the tabs: the empty one that opens a new tab
    _scroll.contentSize = CGSizeMake(stride * (CGFloat)([_pages count] + 1), area);
    _plus.frame = CGRectMake(stride * (CGFloat)[_pages count] + YZPageGap / 2, 0, pageWidth, area);
    _plusIcon.frame = CGRectMake(0, area / 2 - 56, pageWidth, 72);
    _plusCaption.frame = CGRectMake(8, area / 2 + 22, pageWidth - 16, 22);
    [[_plus viewWithTag:5] setFrame:_plus.bounds];
    for (NSUInteger i = 0; i < [_pages count]; ++i) {
        YZPagerPage *page = [_pages objectAtIndex:i];
        page.frame = CGRectMake(stride * (CGFloat)i + YZPageGap / 2, 0, pageWidth, area);
    }
    if (_current >= 0 && !_scroll.dragging && !_scroll.decelerating)
        _scroll.contentOffset = CGPointMake(stride * (CGFloat)_current, 0);
}

// a touch beside the middle page, on a neighbour or on the empty room between, pages the view as well. a view that is hidden or takes
// no touches answers none: the grid of the tray lies under this view while it is hidden
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    UIView *hit = [super hitTest:point withEvent:event];
    if (hit == self && point.y >= _scroll.frame.origin.y && point.y <= CGRectGetMaxY(_scroll.frame))
        return _scroll;
    return hit;
}

- (void)scrollViewDidScroll:(UIScrollView *)scroll {
    CGFloat stride = [self stride];
    if (stride <= 0 || ![_pages count])
        return;
    NSInteger index = (NSInteger)floor(scroll.contentOffset.x / stride + 0.5f);
    [self setCurrent:MAX((NSInteger)0, MIN(index, (NSInteger)[_pages count]))];
}

// the page stops on the empty one at the end: a new tab opens. only a page that came to rest asks, a drag across it does not
- (void)requestIfAtEnd {
    if (_requested || _current != (NSInteger)[_pages count])
        return;
    _requested = YES;
    [_delegate tabPagerDidRequestNewTab:self];
}
- (void)scrollViewDidEndDecelerating:(UIScrollView *)scroll { (void)scroll; [self requestIfAtEnd]; }
- (void)scrollViewDidEndScrollingAnimation:(UIScrollView *)scroll { (void)scroll; [self requestIfAtEnd]; }
- (void)scrollViewDidEndDragging:(UIScrollView *)scroll willDecelerate:(BOOL)decelerate {
    (void)scroll;
    if (!decelerate)
        [self requestIfAtEnd];
}

- (void)plusTapped {
    if (_requested)
        return;
    _requested = YES;
    [_delegate tabPagerDidRequestNewTab:self];
}

- (void)dotsChanged:(UIPageControl *)dots {
    NSInteger index = dots.currentPage;
    [_scroll setContentOffset:CGPointMake([self stride] * (CGFloat)index, 0) animated:YES];
}

- (void)pageOpened:(UIButton *)button {
    YZPagerPage *page = (YZPagerPage *)button.superview;
    NSUInteger index = [_pages indexOfObjectIdenticalTo:page];
    if (index != NSNotFound)
        [_delegate tabPager:self didSelectIndex:index];
}

- (void)pageClosed:(UIButton *)button {
    YZPagerPage *page = (YZPagerPage *)button.superview;
    YZTab *tab = page->tab;
    if ([_tabs indexOfObjectIdenticalTo:tab] == NSNotFound)
        return;
    button.userInteractionEnabled = NO;
    // the page rises and fades with the animation of the system, then the tab is closed and the pages close up
    [UIView animateWithDuration:0.25 delay:0 options:UIViewAnimationOptionCurveEaseIn animations:^{
        page.alpha = 0;
        page.transform = CGAffineTransformMakeTranslation(0, -40);
    } completion:^(BOOL finished) {
        (void)finished;
        NSUInteger index = [_tabs indexOfObjectIdenticalTo:tab];
        if (index != NSNotFound)
            [_delegate tabPager:self didCloseIndex:index];
    }];
}
@end
