#import "YZMascotPreview.h"
#import "YZClassic.h"
#import "YZCompat.h"
#import "YZStrings.h"
#import "YZTheme.h"
#import <QuartzCore/QuartzCore.h>

static const CGFloat YZPreviewHeight = 268;
static const CGFloat YZPreviewGap = 16;
static const CGFloat YZPreviewDots = 24;

// the places of the app that show the mascot, in the order of the pages
static NSArray *YZPreviewPlaces(void) {
    return [NSArray arrayWithObjects:@"hero", @"error", @"sticker", nil];
}

static NSString *YZPreviewCaption(NSUInteger index) {
    switch (index) {
    case 0: return YZT(@"Главная");
    case 1: return YZT(@"Ошибка");
    default: return YZT(@"Стикер");
    }
}

@implementation YZMascotPreview

+ (CGFloat)preferredHeight { return YZPreviewHeight; }

- (id)initWithMascot:(YZMascot)mascot {
    self = [super initWithFrame:CGRectMake(0, 0, 320, YZPreviewHeight)];
    if (!self)
        return nil;
    _mascot = mascot;
    _plates = [[NSMutableArray alloc] init];
    _pictures = [[NSMutableArray alloc] init];
    _captions = [[NSMutableArray alloc] init];
    _glows = [[NSMutableArray alloc] init];
    self.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    _scroll = [[UIScrollView alloc] initWithFrame:CGRectZero];
    _scroll.pagingEnabled = YES;
    _scroll.clipsToBounds = NO;
    _scroll.showsHorizontalScrollIndicator = NO;
    _scroll.delegate = self;
    [self addSubview:_scroll];
    for (NSUInteger i = 0; i < [YZPreviewPlaces() count]; ++i) {
        UIView *plate = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
        [_scroll addSubview:plate];
        UIImageView *glow = [[[UIImageView alloc] initWithFrame:CGRectZero] autorelease];
        glow.userInteractionEnabled = NO;
        [plate addSubview:glow];
        UIImageView *picture = [[[UIImageView alloc] initWithFrame:CGRectZero] autorelease];
        picture.contentMode = UIViewContentModeScaleAspectFit;
        [plate addSubview:picture];
        UILabel *caption = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
        caption.textAlignment = YZTextAlignmentCenter;
        caption.backgroundColor = [UIColor clearColor];
        [plate addSubview:caption];
        [_plates addObject:plate];
        [_pictures addObject:picture];
        [_captions addObject:caption];
        [_glows addObject:glow];
    }
    _dots = [[UIPageControl alloc] initWithFrame:CGRectZero];
    _dots.numberOfPages = (NSInteger)[YZPreviewPlaces() count];
    [_dots addTarget:self action:@selector(dotsChanged:) forControlEvents:UIControlEventValueChanged];
    [self addSubview:_dots];
    [self loadPictures];
    [self applyTheme];
    return self;
}

- (void)dealloc {
    _scroll.delegate = nil;
    [_scroll release];
    [_dots release];
    [_plates release];
    [_pictures release];
    [_captions release];
    [_glows release];
    [super dealloc];
}

- (void)loadPictures {
    NSArray *places = YZPreviewPlaces();
    for (NSUInteger i = 0; i < [places count]; ++i)
        [[_pictures objectAtIndex:i] setImage:[YZTheme mascotImage:[places objectAtIndex:i] mascot:_mascot]];
}

- (void)applyTheme {
    BOOL classic = [YZTheme isClassic];
    for (NSUInteger i = 0; i < [_plates count]; ++i) {
        UIView *plate = [_plates objectAtIndex:i];
        UILabel *caption = [_captions objectAtIndex:i];
        caption.text = YZPreviewCaption(i);
        UIImageView *glow = [_glows objectAtIndex:i];
        if (classic) {
            // plain: the picture and its caption on the ground of the screen, the font and the dots of the system
            plate.backgroundColor = [UIColor clearColor];
            plate.layer.cornerRadius = 0;
            plate.layer.shouldRasterize = NO;
            caption.font = [UIFont boldSystemFontOfSize:15];
            caption.textColor = [YZTheme subtleTextColor];
            glow.hidden = YES;
        } else {
            plate.backgroundColor = [YZTheme cardColor];
            plate.layer.cornerRadius = [YZTheme sheetRadius];
            caption.font = [YZTheme boldSystemFontOfSize:14];
            caption.textColor = [YZTheme textColor];
            glow.image = [YZTheme glowImageWithColor:[YZTheme accentColor]];
            glow.hidden = ![YZSettings mascotGlow];
            // a plate does not change while the pages move, only its scale and opacity do: it is drawn once and the render server only composes it
            plate.layer.shouldRasterize = YES;
            plate.layer.rasterizationScale = [[UIScreen mainScreen] scale];
        }
    }
    if ([_dots respondsToSelector:NSSelectorFromString(@"setPageIndicatorTintColor:")]) {
        [_dots setValue:[[YZTheme subtleTextColor] colorWithAlphaComponent:0.4f] forKey:@"pageIndicatorTintColor"];
        [_dots setValue:classic ? [YZTheme subtleTextColor] : [YZTheme accentColor] forKey:@"currentPageIndicatorTintColor"];
    }
    [self setNeedsLayout];
    [self updatePageLook];
}

- (void)showMascot:(YZMascot)mascot {
    if (mascot == _mascot)
        return;
    _mascot = mascot;
    // each picture dissolves into the other mascot's through a fade on its own layer: the plate holds its scale and its place, a
    // transition of the plate would snapshot it and show it unscaled for a moment
    if (self.window && [YZTheme animationsEnabled]) {
        for (UIImageView *picture in _pictures) {
            CATransition *fade = [CATransition animation];
            fade.type = kCATransitionFade;
            fade.duration = 0.28;
            fade.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
            [picture.layer addAnimation:fade forKey:@"yz.mascot"];
        }
    }
    [self loadPictures];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width, height = self.bounds.size.height;
    BOOL classic = [YZTheme isClassic];
    // the modern pages stand a little apart and the neighbours show at the edges, the classic ones fill the width
    CGFloat pageWidth = classic ? width : MIN(width - 72, (CGFloat)360);
    CGFloat gap = classic ? 0 : YZPreviewGap;
    CGFloat stride = pageWidth + gap;
    CGFloat plateHeight = height - YZPreviewDots - 14 - (classic ? 4 : 0);
    _scroll.clipsToBounds = classic;
    _scroll.frame = CGRectMake(floor((width - stride) / 2), classic ? 4 : 8, stride, plateHeight);
    _scroll.contentSize = CGSizeMake(stride * (CGFloat)[_plates count], plateHeight);
    for (NSUInteger i = 0; i < [_plates count]; ++i) {
        UIView *plate = [_plates objectAtIndex:i];
        // bounds and center stay defined while the page look scales the plate
        plate.bounds = CGRectMake(0, 0, pageWidth, plateHeight);
        plate.center = CGPointMake(stride * (CGFloat)i + gap / 2 + pageWidth / 2, plateHeight / 2);
        CGFloat captionHeight = 22;
        CGFloat side = MIN(plateHeight - captionHeight - 28, (CGFloat)170);
        CGRect pictureFrame = CGRectMake((pageWidth - side) / 2, 14, side, side);
        [[_pictures objectAtIndex:i] setFrame:pictureFrame];
        [[_glows objectAtIndex:i] setFrame:CGRectInset(pictureFrame, -side * 0.35f, -side * 0.35f)];
        [[_captions objectAtIndex:i] setFrame:CGRectMake(8, plateHeight - captionHeight - 12, pageWidth - 16, captionHeight)];
    }
    _dots.frame = CGRectMake(0, height - YZPreviewDots - 4, width, YZPreviewDots);
    [self updatePageLook];
}

// the page in front is full size, the others shrink and dim as they leave: the modern design only
- (void)updatePageLook {
    BOOL classic = [YZTheme isClassic];
    CGFloat stride = _scroll.bounds.size.width;
    if (stride <= 0)
        return;
    CGFloat position = _scroll.contentOffset.x / stride;
    for (NSUInteger i = 0; i < [_plates count]; ++i) {
        UIView *plate = [_plates objectAtIndex:i];
        CGFloat distance = MIN((CGFloat)1, (CGFloat)fabs(position - (CGFloat)i));
        plate.transform = classic ? CGAffineTransformIdentity : CGAffineTransformMakeScale(1 - 0.1f * distance, 1 - 0.1f * distance);
        plate.alpha = classic ? 1 : 1 - 0.5f * distance;
    }
}

- (void)scrollViewDidScroll:(UIScrollView *)scroll {
    CGFloat stride = scroll.bounds.size.width;
    if (stride > 0)
        _dots.currentPage = MAX((NSInteger)0, MIN((NSInteger)floor(scroll.contentOffset.x / stride + 0.5f), (NSInteger)[_plates count] - 1));
    [self updatePageLook];
}

- (void)dotsChanged:(UIPageControl *)dots {
    [_scroll setContentOffset:CGPointMake(_scroll.bounds.size.width * (CGFloat)dots.currentPage, 0) animated:YES];
}

// a touch beside the middle page pages the view as well, and none of a view that is hidden or takes no touches
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    UIView *hit = [super hitTest:point withEvent:event];
    if (hit == self && point.y >= _scroll.frame.origin.y && point.y <= CGRectGetMaxY(_scroll.frame))
        return _scroll;
    return hit;
}
@end
