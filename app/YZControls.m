#import "YZControls.h"
#import <objc/message.h>
#import "YZCompat.h"
#import "YZStrings.h"
#import "YZMotion.h"
#import "YZTheme.h"
#import "YZClassic.h"
#import <QuartzCore/QuartzCore.h>

@implementation YZScrollView
- (BOOL)touchesShouldCancelInContentView:(UIView *)view {
    (void)view;
    return YES;
}
@end

@implementation YZStateButton
@synthesize pillSize = _pillSize;
@synthesize pillInsets = _pillInsets;

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    _pill = [[UIView alloc] initWithFrame:CGRectZero];
    _pill.userInteractionEnabled = NO;
    _pill.alpha = 0;
    [self insertSubview:_pill atIndex:0];
    return self;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect bounds = self.bounds;
    CGRect frame;
    if (_pillSize.width > 0 && _pillSize.height > 0)
        frame = CGRectMake((bounds.size.width - _pillSize.width) / 2, (bounds.size.height - _pillSize.height) / 2, _pillSize.width, _pillSize.height);
    else
        frame = UIEdgeInsetsInsetRect(bounds, _pillInsets);
    // bounds and center stay defined while a motion scales the pill
    _pill.bounds = CGRectMake(0, 0, frame.size.width, frame.size.height);
    _pill.center = CGPointMake(CGRectGetMidX(frame), CGRectGetMidY(frame));
    // the classic design presses a button as a dark rounded square, not a pill of the accent
    _pill.layer.cornerRadius = [YZTheme isClassic] ? MIN(frame.size.height / 2, (CGFloat)7) : frame.size.height / 2;
    [self sendSubviewToBack:_pill];
}

- (void)setHighlighted:(BOOL)highlighted {
    BOOL changed = highlighted != self.highlighted;
    [super setHighlighted:highlighted];
    if (!changed)
        return;
    if ([YZTheme isClassic]) {
        // lit from above: pressed is darker, with no bounce
        _pill.backgroundColor = [UIColor colorWithWhite:0 alpha:0.28f];
        _pill.transform = CGAffineTransformIdentity;
        [UIView animateWithDuration:[YZTheme animationsEnabled] ? 0.08 : 0 animations:^{ _pill.alpha = highlighted ? 1 : 0; }];
        return;
    }
    _pill.backgroundColor = [YZTheme stateColor];
    if (highlighted) {
        if (_pill.alpha == 0)
            _pill.transform = CGAffineTransformMakeScale(0.7f, 0.7f);
        YZMotionAnimateView(_pill, CGAffineTransformIdentity, 1, YOZORA_MOTION_WOBBLE, 0, nil);
    } else {
        YZMotionAnimateView(_pill, CGAffineTransformMakeScale(0.92f, 0.92f), 0, YOZORA_MOTION_DISMISS, 0, nil);
    }
}

- (void)applyTheme { _pill.backgroundColor = [YZTheme isClassic] ? [UIColor colorWithWhite:0 alpha:0.28f] : [YZTheme stateColor]; }

- (void)dealloc {
    [_pill release];
    [super dealloc];
}
@end

static const CGFloat YZSegmentHeight = 36;

@implementation YZSegmentedPill

- (id)initWithTitles:(NSArray *)titles icons:(NSArray *)icons {
    self = [super initWithFrame:CGRectZero];
    if (!self)
        return nil;
    _titles = [titles copy];
    _icons = [icons copy];
    _buttons = [[NSMutableArray alloc] init];
    _indicator = [[UIView alloc] initWithFrame:CGRectZero];
    _indicator.userInteractionEnabled = NO;
    [self addSubview:_indicator];
    for (NSUInteger i = 0; i < [_titles count]; ++i) {
        UIButton *button = [UIButton buttonWithType:UIButtonTypeCustom];
        button.tag = (NSInteger)i;
        button.titleLabel.font = [YZTheme boldSystemFontOfSize:13];
        button.titleEdgeInsets = UIEdgeInsetsMake(0, 6, 0, 0);
        [button addTarget:self action:@selector(segmentPressed:) forControlEvents:UIControlEventTouchUpInside];
        [self addSubview:button];
        [_buttons addObject:button];
    }
    [self applyTheme];
    return self;
}

- (NSInteger)selectedSegment { return _selected; }
- (void)setSelectedSegment:(NSInteger)index { [self setSelectedSegment:index animated:NO]; }

- (void)setSelectedSegment:(NSInteger)index animated:(BOOL)animated {
    if (index < 0 || index >= (NSInteger)[_buttons count])
        return;
    _selected = index;
    [self applyTheme];
    UIButton *chosen = [_buttons objectAtIndex:(NSUInteger)index];
    CGRect target = chosen.frame;
    if (animated && self.window && target.size.width > 0) {
        // the indicator keeps its size and travels, with the wobble it passes the segment and swings back
        CGPoint from = _indicator.center;
        _indicator.bounds = CGRectMake(0, 0, target.size.width, target.size.height);
        _indicator.center = CGPointMake(CGRectGetMidX(target), CGRectGetMidY(target));
        YZMotionAnimateEnter(_indicator, CGAffineTransformMakeTranslation(from.x - _indicator.center.x, 0), 1, YOZORA_MOTION_WOBBLE, 0);
        YZMotionAnimateEnter(chosen.imageView, CGAffineTransformMakeScale(0.6f, 0.6f), 1, YOZORA_MOTION_WOBBLE, 0.04);
    } else {
        _indicator.bounds = CGRectMake(0, 0, target.size.width, target.size.height);
        _indicator.center = CGPointMake(CGRectGetMidX(target), CGRectGetMidY(target));
    }
}

- (void)setTitles:(NSArray *)titles {
    [titles retain];
    [_titles release];
    _titles = titles;
    [self applyTheme];
}

- (void)applyTheme {
    BOOL classic = [YZTheme isClassic];
    if (classic) {
        // a segmented control of the old system: a sunk grey well and a lit glossy key of the accent under the chosen segment
        self.backgroundColor = [YZTheme pressedColor];
        self.layer.borderWidth = 1;
        self.layer.borderColor = [[UIColor colorWithWhite:0 alpha:0.45f] CGColor];
        _indicator.backgroundColor = [UIColor clearColor];
        [YZTheme stretchImage:[YZClassic keyImageWithColor:[YZClassic barMidColor] radius:7 pressed:NO] caps:7 onView:_indicator];
    } else {
        self.backgroundColor = [YZTheme fieldColor];
        self.layer.borderWidth = 0;
        _indicator.layer.contents = nil;
        _indicator.backgroundColor = [YZTheme stateColor];
    }
    for (NSUInteger i = 0; i < [_buttons count]; ++i) {
        UIButton *button = [_buttons objectAtIndex:i];
        BOOL chosen = (NSInteger)i == _selected;
        UIColor *color = chosen ? (classic ? [UIColor whiteColor] : [YZTheme accentColor]) : [YZTheme subtleTextColor];
        [button setTitle:[_titles objectAtIndex:i] forState:UIControlStateNormal];
        [button setTitleColor:color forState:UIControlStateNormal];
        // a segment without a glyph is only its title
        NSNumber *glyph = i < [_icons count] ? [_icons objectAtIndex:i] : nil;
        [button setImage:glyph ? [YZTheme icon:(YZIcon)[glyph intValue] size:18 color:color] : nil forState:UIControlStateNormal];
        button.titleEdgeInsets = glyph ? UIEdgeInsetsMake(0, 6, 0, 0) : UIEdgeInsetsZero;
        button.accessibilityTraits = chosen ? (UIAccessibilityTraitButton | UIAccessibilityTraitSelected) : UIAccessibilityTraitButton;
    }
}

- (void)segmentPressed:(UIButton *)button {
    NSInteger index = button.tag;
    if (index == _selected)
        return;
    [self setSelectedSegment:index animated:YES];
    [self sendActionsForControlEvents:UIControlEventValueChanged];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect bounds = self.bounds;
    CGFloat each = [_buttons count] ? bounds.size.width / [_buttons count] : 0;
    for (NSUInteger i = 0; i < [_buttons count]; ++i)
        [[_buttons objectAtIndex:i] setFrame:CGRectMake(each * i, 0, each, bounds.size.height)];
    BOOL classicShape = [YZTheme isClassic];
    self.layer.cornerRadius = classicShape ? 8 : bounds.size.height / 2;
    _indicator.layer.cornerRadius = classicShape ? 7 : bounds.size.height / 2;
    if (![[_indicator.layer animationKeys] count] && _selected < (NSInteger)[_buttons count]) {
        CGRect target = [[_buttons objectAtIndex:(NSUInteger)_selected] frame];
        _indicator.bounds = CGRectMake(0, 0, target.size.width, target.size.height);
        _indicator.center = CGPointMake(CGRectGetMidX(target), CGRectGetMidY(target));
    }
}

- (CGSize)sizeThatFits:(CGSize)size {
    (void)size;
    return CGSizeMake(200, YZSegmentHeight);
}

- (void)dealloc {
    [_titles release];
    [_icons release];
    [_buttons release];
    [_indicator release];
    [super dealloc];
}
@end

static const CGFloat YZToggleWidth = 52;
static const CGFloat YZToggleHeight = 32;
static const CGFloat YZToggleThumb = 24;

@implementation YZToggle
@synthesize on = _on;

- (id)initWithFrame:(CGRect)frame {
    (void)frame;
    self = [super initWithFrame:CGRectMake(0, 0, YZToggleWidth, YZToggleHeight)];
    if (!self)
        return nil;
    _track = [[YZGradientView alloc] initWithFrame:self.bounds];
    _track.userInteractionEnabled = NO;
    _track.layer.cornerRadius = YZToggleHeight / 2;
    [self addSubview:_track];
    _thumb = [[YZGradientView alloc] initWithFrame:CGRectMake(4, (YZToggleHeight - YZToggleThumb) / 2, YZToggleThumb, YZToggleThumb)];
    _thumb.userInteractionEnabled = NO;
    _thumb.layer.cornerRadius = YZToggleThumb / 2;
    [self addSubview:_thumb];
    self.isAccessibilityElement = YES;
    self.accessibilityTraits = UIAccessibilityTraitButton;
    [self addTarget:self action:@selector(tapped) forControlEvents:UIControlEventTouchUpInside];
    [self applyTheme];
    return self;
}

- (CGSize)sizeThatFits:(CGSize)size {
    (void)size;
    return CGSizeMake(YZToggleWidth, YZToggleHeight);
}

// the thumb is placed on the left and moved by its transform: small when off, full size and 20 points to the right when on
- (CGAffineTransform)thumbTransform {
    // the classic knob keeps its size when the switch is off
    if ([YZTheme isClassic])
        return _on ? CGAffineTransformMakeTranslation(YZToggleWidth - YZToggleHeight, 0) : CGAffineTransformIdentity;
    return _on ? CGAffineTransformMakeTranslation(YZToggleWidth - YZToggleHeight, 0) : CGAffineTransformMakeScale(0.667f, 0.667f);
}

// the colors of the track and the knob for the state: flat in the modern design, a lit gradient with an outline and a shadow in the classic one
- (void)paintParts {
    YZGradientView *track = (YZGradientView *)_track, *thumb = (YZGradientView *)_thumb;
    if ([YZTheme isClassic]) {
        if (_on)
            [track setTop:[YZClassic barBottomColor] bottom:[YZClassic barTopColor]];
        else
            [track setTop:[UIColor colorWithWhite:0.72f alpha:1] bottom:[UIColor colorWithWhite:0.95f alpha:1]];
        track.layer.cornerRadius = YZToggleHeight / 2;
        track.layer.borderWidth = 1;
        track.layer.borderColor = [[UIColor colorWithWhite:0 alpha:0.5f] CGColor];
        [thumb setTop:[UIColor colorWithWhite:1 alpha:1] bottom:[UIColor colorWithWhite:0.82f alpha:1]];
        thumb.layer.cornerRadius = YZToggleThumb / 2;
        thumb.layer.borderWidth = 1;
        thumb.layer.borderColor = [[UIColor colorWithWhite:0 alpha:0.45f] CGColor];
        thumb.layer.shadowColor = [[UIColor blackColor] CGColor];
        thumb.layer.shadowOpacity = 0.35f;
        thumb.layer.shadowRadius = 1.5f;
        thumb.layer.shadowOffset = CGSizeMake(0, 1);
        return;
    }
    UIColor *trackColor = _on ? [YZTheme accentColor] : [YZTheme fieldColor];
    [track setTop:trackColor bottom:trackColor];
    track.layer.cornerRadius = YZToggleHeight / 2;
    track.layer.borderWidth = _on ? 0 : 2;
    track.layer.borderColor = [[YZTheme subtleTextColor] CGColor];
    UIColor *thumbColor = _on ? [YZTheme surfaceColor] : [YZTheme subtleTextColor];
    [thumb setTop:thumbColor bottom:thumbColor];
    thumb.layer.cornerRadius = YZToggleThumb / 2;
    thumb.layer.borderWidth = 0;
    thumb.layer.shadowOpacity = 0;
}

- (void)applyTheme {
    [self paintParts];
    self.accessibilityValue = _on ? @"1" : @"0";
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (![[_thumb.layer animationKeys] count])
        _thumb.transform = [self thumbTransform];
    [CATransaction commit];
}

- (BOOL)isOn { return _on; }
- (void)setOn:(BOOL)on { [self setOn:on animated:NO]; }

- (void)setOn:(BOOL)on animated:(BOOL)animated {
    if (on == _on && !animated) {
        [self applyTheme];
        return;
    }
    _on = on;
    self.accessibilityValue = on ? @"1" : @"0";
    if (animated && self.window) {
        YZMotionAnimateView(_thumb, [self thumbTransform], 1, [YZTheme isClassic] ? YOZORA_MOTION_ENTER : YOZORA_MOTION_WOBBLE, 0, nil);
        [UIView animateWithDuration:0.2 animations:^{ [self paintParts]; }];
    } else {
        [self applyTheme];
    }
}

- (void)tapped {
    [self setOn:!_on animated:YES];
    [self sendActionsForControlEvents:UIControlEventValueChanged];
}

- (void)dealloc {
    [_track release];
    [_thumb release];
    [super dealloc];
}
@end

@implementation YZBarTitleView
@synthesize title = _title;

- (id)initWithTitle:(NSString *)title width:(CGFloat)width target:(id)target action:(SEL)action {
    self = [super initWithFrame:CGRectMake(0, 0, width, 44)];
    if (!self)
        return nil;
    _title = [title copy];
    _arrow = [[YZStateButton alloc] initWithFrame:CGRectMake(0, 2, 48, 40)];
    _arrow.pillSize = CGSizeMake(44, 32);
    _arrow.accessibilityLabel = YZT(@"Назад");
    [_arrow addTarget:target action:action forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:_arrow];
    _label = [[UILabel alloc] initWithFrame:CGRectZero];
    _label.backgroundColor = [UIColor clearColor];
    _label.numberOfLines = 1;
    _label.text = title;
    [self addSubview:_label];
    [self applyTheme];
    return self;
}

- (void)dealloc {
    [_arrow release];
    [_label release];
    [_title release];
    [_appliedKey release];
    [super dealloc];
}

// what the title looks like depends on the text color and the interface text scale: the same ones need no new pass
- (void)applyThemeIfChanged {
    NSString *key = [NSString stringWithFormat:@"%@|%d|%.1f", [YZTheme barInkColor], [YZTheme isClassic] ? 1 : 0, [YZTheme boldSystemFontOfSize:22].pointSize];
    if ([key isEqualToString:_appliedKey])
        return;
    [self applyTheme];
}

- (void)applyTheme {
    [_appliedKey release];
    _appliedKey = [[NSString stringWithFormat:@"%@|%d|%.1f", [YZTheme barInkColor], [YZTheme isClassic] ? 1 : 0, [YZTheme boldSystemFontOfSize:22].pointSize] retain];
    [_arrow setImage:[YZTheme barIcon:YZIconBack size:24 disabled:NO] forState:UIControlStateNormal];
    [_arrow applyTheme];
    _label.font = [YZTheme boldSystemFontOfSize:22];
    _label.textColor = [YZTheme barInkColor];
    if ([YZTheme isClassic])
        [YZClassic stampLabel:_label];
    else
        _label.shadowColor = nil;
    // a long name shrinks a little before it is cut, the bar has one row
    YZShrinkToFit(_label);
    [self setNeedsLayout];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat height = self.bounds.size.height;
    _arrow.frame = CGRectMake(0, (height - 40) / 2, 48, 40);
    _label.frame = CGRectMake(52, 0, MAX((CGFloat)0, self.bounds.size.width - 52 - 8), height);
}
@end

void YZPaintBehindScreens(UINavigationController *navigation) {
    if (!navigation)
        return;
    UIColor *ground = [YZTheme surfaceColor];
    navigation.view.backgroundColor = ground;
    for (UIView *view in navigation.view.subviews) {
        if ([view isKindOfClass:[UINavigationBar class]] || [view isKindOfClass:[UIToolbar class]])
            continue;
        NSString *name = NSStringFromClass([view class]);
        if ([name hasPrefix:@"UINavigationTransition"] || [name hasPrefix:@"UILayoutContainer"] || [name hasPrefix:@"UIViewControllerWrapper"])
            view.backgroundColor = ground;
    }
}

void YZInstallBarTitle(UIViewController *controller, NSString *title, id target, SEL action) {
    UINavigationBar *bar = controller.navigationController.navigationBar;
    CGFloat width = bar ? bar.bounds.size.width : [[UIScreen mainScreen] bounds].size.width;
    // the item sits 8 points in from the edge on each side and the bar adds its own margin
    width = MAX((CGFloat)120, floor(width - 24));
    UIBarButtonItem *item = controller.navigationItem.leftBarButtonItem;
    YZBarTitleView *view = [item.customView isKindOfClass:[YZBarTitleView class]] ? (YZBarTitleView *)item.customView : nil;
    // lists lay out on every scroll step and any touch of the item or title view lays the bar out, a title
    // that still fits is left alone unless the theme changed
    if (view && [view.title isEqualToString:title] && fabs(view.bounds.size.width - width) < 1) {
        [view applyThemeIfChanged];
        return;
    }
    controller.navigationItem.hidesBackButton = YES;
    controller.title = nil;
    view = [[[YZBarTitleView alloc] initWithTitle:title width:width target:target action:action] autorelease];
    controller.navigationItem.leftBarButtonItem = [[[UIBarButtonItem alloc] initWithCustomView:view] autorelease];
}

@implementation YZBarFill

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.userInteractionEnabled = NO;
    self.clipsToBounds = YES;
    // above the cells that the table adds later, whatever the order of the subviews
    self.layer.zPosition = 100;
    _solid = [[UIView alloc] initWithFrame:self.bounds];
    _solid.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self addSubview:_solid];
    [self applyTheme];
    return self;
}

- (void)dealloc {
    [_solid release];
    [_blur release];
    [super dealloc];
}

- (void)applyTheme {
    [_blur removeFromSuperview];
    [_blur release];
    // a real blur from ios 8, where UIVisualEffectView exists; the older systems have a translucent tint and nothing to blur with
    Class effectView = NSClassFromString(@"UIVisualEffectView");
    Class blurEffect = NSClassFromString(@"UIBlurEffect");
    if (effectView && blurEffect) {
        // the styles are extra light 0, light 1 and dark 2
        id effect = ((id (*)(id, SEL, NSInteger))objc_msgSend)(blurEffect, NSSelectorFromString(@"effectWithStyle:"), [YZTheme isDark] ? 2 : 1);
        _blur = [[effectView alloc] performSelector:NSSelectorFromString(@"initWithEffect:") withObject:effect];
        UIView *tint = [[_blur valueForKey:@"contentView"] retain];
        tint.backgroundColor = [[YZTheme surfaceColor] colorWithAlphaComponent:0.35f];
        [tint release];
    } else {
        _blur = [[UIView alloc] initWithFrame:CGRectZero];
        _blur.backgroundColor = [[YZTheme surfaceColor] colorWithAlphaComponent:0.86f];
    }
    _blur.frame = self.bounds;
    _blur.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self addSubview:_blur];
    _solid.backgroundColor = [YZTheme surfaceColor];
    _solid.alpha = _blurred ? 0 : 1;
    _blur.alpha = _blurred ? 1 : 0;
}

- (void)setBlurred:(BOOL)blurred animated:(BOOL)animated {
    if (blurred == _blurred)
        return;
    _blurred = blurred;
    [UIView animateWithDuration:(animated && [YZTheme animationsEnabled]) ? 0.2 : 0 delay:0
                        options:UIViewAnimationOptionCurveEaseOut | UIViewAnimationOptionBeginFromCurrentState
                     animations:^{
        _solid.alpha = blurred ? 0 : 1;
        _blur.alpha = blurred ? 1 : 0;
    } completion:nil];
}

// ios 11 and later keep the system inset apart from contentInset
static CGFloat YZRestingOffset(UIScrollView *scroll) {
    UIEdgeInsets inset = scroll.contentInset;
    if ([scroll respondsToSelector:NSSelectorFromString(@"adjustedContentInset")])
        inset = [[scroll valueForKey:@"adjustedContentInset"] UIEdgeInsetsValue];
    return -inset.top;
}

- (void)attachToScrollView:(UIScrollView *)scroll controller:(UIViewController *)controller {
    // the classic bar is opaque and the content starts below it, so a fill of the ground over the top of the list would only cover its first rows
    if ([YZTheme isClassic]) {
        self.hidden = YES;
        return;
    }
    self.hidden = NO;
    UINavigationBar *bar = controller.navigationController.navigationBar;
    CGFloat height = bar ? CGRectGetMaxY(bar.frame) : 0;
    if (self.superview != scroll)
        [scroll addSubview:self];
    self.layer.zPosition = 100;
    CGRect place = CGRectMake(0, scroll.contentOffset.y, scroll.bounds.size.width, height);
    if (!CGRectEqualToRect(self.frame, place)) {
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        self.frame = place;
        [CATransaction commit];
    }
    [self setBlurred:scroll.contentOffset.y - YZRestingOffset(scroll) > 8 animated:YES];
}

- (void)placeInView:(UIView *)view controller:(UIViewController *)controller {
    if ([YZTheme isClassic]) {
        self.hidden = YES;
        return;
    }
    self.hidden = NO;
    UINavigationBar *bar = controller.navigationController.navigationBar;
    CGFloat height = bar ? CGRectGetMaxY(bar.frame) : 0;
    if (self.superview != view)
        [view addSubview:self];
    self.frame = CGRectMake(0, 0, view.bounds.size.width, height);
    [view bringSubviewToFront:self];
}
@end
