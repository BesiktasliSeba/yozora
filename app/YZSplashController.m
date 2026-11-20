#import "YZSplashController.h"
#import "YZMotion.h"
#import "YZTheme.h"

@implementation YZSplashController

+ (UIColor *)launchColor {
    return [UIColor colorWithRed:88.0f / 255 green:30.0f / 255 blue:118.0f / 255 alpha:1];
}

- (void)loadView {
    UIView *root = [[UIView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    root.backgroundColor = [YZSplashController launchColor];
    self.view = root;
    [root release];
    _hero = [[UIImageView alloc] initWithImage:[UIImage imageNamed:@"yozora-hero.png"]];
    _hero.contentMode = UIViewContentModeScaleAspectFit;
    [root addSubview:_hero];
    _name = [[UIImageView alloc] initWithImage:[YZTheme wordmarkWithHeight:30 color:[UIColor whiteColor]]];
    [root addSubview:_name];
}

- (void)dealloc {
    [_hero release];
    [_name release];
    [super dealloc];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGSize size = self.view.bounds.size;
    CGFloat side = MIN(MAX(MIN(size.width, size.height) * 0.34f, (CGFloat)96), (CGFloat)200);
    _hero.bounds = CGRectMake(0, 0, side, side);
    _hero.center = CGPointMake(size.width / 2, size.height / 2 - 28);
    _name.center = CGPointMake(size.width / 2, CGRectGetMaxY(_hero.frame) + 16 + _name.bounds.size.height / 2);
    _shownFrame = self.view.frame;
}

// the sticker wobbles in and the name rises behind it, once
- (void)play {
    if (_played)
        return;
    _played = YES;
    YZMotionAnimateEnter(_hero, CGAffineTransformMakeScale(0.5f, 0.5f), 0, YOZORA_MOTION_WOBBLE, 0);
    [YZTheme animateEntranceOfView:_name offset:CGPointMake(0, 14) delay:0.08];
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [self play];
}

- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)orientation {
    (void)orientation;
    return YES;
}

- (void)fadeOutOverWindow:(UIWindow *)window completion:(void (^)(void))completion {
    UIView *cover = [self.view retain];
    // the root view controller laid the view out 20 points down and shorter, under the status bar. the taller
    // window bounds would lift the sticker when the cover is added, a jump at the start of the fade, so the
    // cover keeps its shown frame unless the window turned
    BOOL sameSize = fabs(_shownFrame.size.width - window.bounds.size.width) < 1 && _shownFrame.size.height > 0 && _shownFrame.size.height <= window.bounds.size.height;
    cover.frame = sameSize ? _shownFrame : window.bounds;
    cover.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [window addSubview:cover];
    [YZTheme animate:^{ cover.alpha = 0; } completion:^(BOOL finished) {
        (void)finished;
        [cover removeFromSuperview];
        [cover release];
        if (completion)
            completion();
    }];
}
@end
