#import "YZTabStrip.h"
#import "YZCompat.h"
#import "YZControls.h"
#import "YZStrings.h"
#import "YZTab.h"
#import "YZTheme.h"
#include "yozora_address.h"
#import <QuartzCore/QuartzCore.h>

static const CGFloat YZChipMinWidth = 132;
static const CGFloat YZChipMaxWidth = 232;
static const CGFloat YZChipGap = 6;
static const CGFloat YZAddWidth = 48;

@interface YZTabChip : UIView {
@public
    UILabel *title;
    UIButton *close;
    UIButton *hit;
    BOOL selected;
}
@end

@implementation YZTabChip
- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.layer.cornerRadius = 14;
    hit = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    [self addSubview:hit];
    title = [[UILabel alloc] initWithFrame:CGRectZero];
    title.font = [YZTheme systemFontOfSize:14];
    title.backgroundColor = [UIColor clearColor];
    title.userInteractionEnabled = NO;
    [self addSubview:title];
    close = [[YZStateButton buttonWithType:UIButtonTypeCustom] retain];
    [(YZStateButton *)close setPillSize:CGSizeMake(24, 24)];
    [self addSubview:close];
    return self;
}
- (void)dealloc {
    [title release];
    [close release];
    [hit release];
    [super dealloc];
}
- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width, height = self.bounds.size.height;
    hit.frame = self.bounds;
    close.frame = CGRectMake(width - 34, (height - 32) / 2, 32, 32);
    title.frame = CGRectMake(14, 0, MAX((CGFloat)0, width - 14 - 36), height);
}
@end

@implementation YZTabStrip
@synthesize delegate = _delegate;

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    _chips = [[NSMutableArray alloc] init];
    _scroll = [[YZScrollView alloc] initWithFrame:CGRectZero];
    _scroll.showsHorizontalScrollIndicator = NO;
    _scroll.showsVerticalScrollIndicator = NO;
    [self addSubview:_scroll];
    _add = [[YZStateButton buttonWithType:UIButtonTypeCustom] retain];
    [(YZStateButton *)_add setPillSize:CGSizeMake(36, 36)];
    _add.accessibilityLabel = YZT(@"Новая вкладка");
    [_add addTarget:self action:@selector(addPressed) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:_add];
    [self applyTheme];
    return self;
}

- (void)dealloc {
    [_chips release];
    [_scroll release];
    [_add release];
    [super dealloc];
}

- (void)applyTheme {
    BOOL classic = [YZTheme isClassic];
    // the classic strip stands on the toolbar of the system, which already draws the whole top bar: a flat fill here would be a separate band
    self.backgroundColor = classic ? [UIColor clearColor] : [YZTheme toolbarColor];
    [_add setImage:[YZTheme barIcon:YZIconPlus size:24 disabled:NO] forState:UIControlStateNormal];
    for (YZTabChip *chip in _chips) {
        // the classic chip is a lit tab: the chosen one pale, the others sunk into the bar
        chip.layer.cornerRadius = classic ? 7 : [YZTheme isFlat] ? 8 : 14;
        chip.backgroundColor = chip->selected ? ([YZTheme isFlat] ? [YZTheme fieldColor] : [YZTheme cardColor]) : (classic ? [UIColor colorWithWhite:0 alpha:0.22f] : [UIColor clearColor]);
        UIColor *rest = classic ? [YZTheme barSubtleInkColor] : [YZTheme subtleTextColor];
        chip->title.textColor = chip->selected ? [YZTheme textColor] : (classic ? [YZTheme barInkColor] : rest);
        [chip->close setImage:[YZTheme icon:YZIconClose size:16 color:chip->selected ? [YZTheme subtleTextColor] : rest] forState:UIControlStateNormal];
    }
}

static NSString *YZChipTitle(YZTab *tab) {
    if ([tab.title length])
        return tab.title;
    char host[256];
    if ([tab.url length] && yozora_address_host([tab.url UTF8String], host, sizeof host))
        return [NSString stringWithUTF8String:host];
    return tab.isPrivate ? YZT(@"Новая приватная вкладка") : YZT(@"Новая вкладка");
}

- (void)showTabs:(NSArray *)tabs selected:(NSUInteger)selected {
    for (YZTabChip *chip in _chips)
        [chip removeFromSuperview];
    [_chips removeAllObjects];
    _selected = selected;
    for (NSUInteger i = 0; i < [tabs count]; ++i) {
        YZTabChip *chip = [[[YZTabChip alloc] initWithFrame:CGRectZero] autorelease];
        chip->selected = i == selected;
        chip->title.text = YZChipTitle([tabs objectAtIndex:i]);
        chip->hit.tag = (NSInteger)i;
        chip->close.tag = (NSInteger)i;
        chip->close.accessibilityLabel = YZT(@"Закрыть");
        [chip->hit addTarget:self action:@selector(chipPressed:) forControlEvents:UIControlEventTouchUpInside];
        [chip->close addTarget:self action:@selector(closePressed:) forControlEvents:UIControlEventTouchUpInside];
        [_scroll addSubview:chip];
        [_chips addObject:chip];
    }
    [self applyTheme];
    _laidWidth = 0;
    [self setNeedsLayout];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width, height = self.bounds.size.height;
    _add.frame = CGRectMake(width - YZAddWidth - 8, (height - 40) / 2, YZAddWidth, 40);
    _scroll.frame = CGRectMake(0, 0, width - YZAddWidth - 8, height);
    CGFloat room = _scroll.bounds.size.width - 16;
    NSUInteger count = [_chips count];
    CGFloat each = count ? (room - YZChipGap * (count - 1)) / count : YZChipMaxWidth;
    each = MIN(YZChipMaxWidth, MAX(YZChipMinWidth, each));
    CGFloat x = 8;
    for (YZTabChip *chip in _chips) {
        chip.frame = CGRectMake(x, 4, each, height - 8);
        x += each + YZChipGap;
    }
    _scroll.contentSize = CGSizeMake(MAX(x + 2, _scroll.bounds.size.width), height);
    // the open tab stays in view when the chips do not fit
    if (_selected < count && width != _laidWidth) {
        CGRect frame = ((YZTabChip *)[_chips objectAtIndex:_selected]).frame;
        [_scroll scrollRectToVisible:CGRectInset(frame, -16, 0) animated:NO];
    }
    _laidWidth = width;
}

- (void)chipPressed:(UIButton *)button {
    if ((NSUInteger)button.tag != _selected)
        [_delegate tabStrip:self didSelectIndex:(NSUInteger)button.tag];
}
- (void)closePressed:(UIButton *)button { [_delegate tabStrip:self didCloseIndex:(NSUInteger)button.tag]; }
- (void)addPressed { [_delegate tabStripDidRequestNewTab:self]; }
@end
