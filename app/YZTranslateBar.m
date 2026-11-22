#import "YZTranslateBar.h"
#import "YZCompat.h"
#import "YZControls.h"
#import "YZMotion.h"
#import "YZStrings.h"
#import "YZTheme.h"
#include "yozora_translate.h"
#import <QuartzCore/QuartzCore.h>

static const CGFloat YZBarHeight = 68;
static const CGFloat YZBarGlyph = 40;
static const CGFloat YZBarButton = 36;

static NSString *YZLanguageNamed(NSString *code) {
    int index = yozora_tr_language_find([code UTF8String]);
    return index < 0 ? @"" : [NSString stringWithUTF8String:yozora_tr_language_name((size_t)index)];
}

@implementation YZTranslateBar
@synthesize delegate = _delegate;

+ (CGFloat)height {
    return YZBarHeight;
}

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.layer.cornerRadius = [YZTheme cardRadius];
    self.layer.shadowColor = [UIColor blackColor].CGColor;
    self.layer.shadowOpacity = 0.28f;
    self.layer.shadowRadius = 12;
    self.layer.shadowOffset = CGSizeMake(0, 4);
    // the card as a whole opens the languages, the buttons on it do what they say
    _body = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    [_body addTarget:self action:@selector(optionsPressed) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:_body];
    _disc = [[UIView alloc] initWithFrame:CGRectZero];
    _disc.layer.cornerRadius = YZBarGlyph / 2;
    _disc.userInteractionEnabled = NO;
    [self addSubview:_disc];
    _glyph = [[UIImageView alloc] initWithFrame:CGRectZero];
    _glyph.contentMode = UIViewContentModeCenter;
    [_disc addSubview:_glyph];
    _title = [[UILabel alloc] initWithFrame:CGRectZero];
    _title.font = [YZTheme boldSystemFontOfSize:15];
    _title.backgroundColor = [UIColor clearColor];
    _title.userInteractionEnabled = NO;
    [self addSubview:_title];
    _subtitle = [[UILabel alloc] initWithFrame:CGRectZero];
    _subtitle.font = [YZTheme systemFontOfSize:13];
    _subtitle.backgroundColor = [UIColor clearColor];
    _subtitle.userInteractionEnabled = NO;
    [self addSubview:_subtitle];
    _wave = [[YZWaveSeekBar alloc] initWithFrame:CGRectZero];
    _wave.userInteractionEnabled = NO;
    _wave.hidden = YES;
    [self addSubview:_wave];
    _primary = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    _primary.titleLabel.font = [YZTheme boldSystemFontOfSize:14];
    _primary.layer.cornerRadius = YZBarButton / 2;
    [_primary addTarget:self action:@selector(primaryPressed) forControlEvents:UIControlEventTouchUpInside];
    YZMotionAttachBounce(_primary, 0.06f);
    [self addSubview:_primary];
    _close = [[YZStateButton buttonWithType:UIButtonTypeCustom] retain];
    [(YZStateButton *)_close setPillSize:CGSizeMake(YZBarButton, YZBarButton)];
    [_close addTarget:self action:@selector(closePressed) forControlEvents:UIControlEventTouchUpInside];
    YZMotionAttachBounce(_close, 0.1f);
    [self addSubview:_close];
    [self applyTheme];
    return self;
}

- (void)dealloc {
    [_disc release];
    [_glyph release];
    [_title release];
    [_subtitle release];
    [_wave release];
    [_primary release];
    [_close release];
    [_body release];
    [super dealloc];
}

- (void)applyTheme {
    [super applyTheme];
    if ([YZTheme isClassic]) {
        // the glyph stands on a lit rounded tile of the accent, as in the rows of the settings
        _disc.backgroundColor = [UIColor clearColor];
        _disc.layer.cornerRadius = 8;
        [YZClassic lightView:_disc color:[YZClassic barMidColor] radius:8];
        _glyph.image = [YZClassic stampedImage:[YZTheme icon:YZIconTranslate size:22 color:[UIColor whiteColor]]];
    } else {
        [YZClassic unlightView:_disc];
        _disc.layer.contents = nil;
        _disc.layer.cornerRadius = YZBarGlyph / 2;
        _disc.backgroundColor = [YZTheme stateColor];
        _glyph.image = [YZTheme icon:YZIconTranslate size:22 color:[YZTheme accentColor]];
    }
    _title.textColor = [YZTheme textColor];
    _subtitle.textColor = [YZTheme subtleTextColor];
    _wave.playedColor = [YZTheme accentColor];
    _wave.restColor = [YZTheme stateColor];
    [YZClassic styleKey:_primary color:[YZTheme isClassic] ? [YZClassic barMidColor] : [YZTheme accentColor] radius:8 modernRadius:YZBarButton / 2];
    [_primary setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    [_close setImage:[YZTheme icon:YZIconClose size:20 color:[YZTheme subtleTextColor]] forState:UIControlStateNormal];
    _close.accessibilityLabel = YZT(@"Закрыть");
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width, height = self.bounds.size.height;
    _body.frame = self.bounds;
    _disc.frame = CGRectMake(14, (height - YZBarGlyph) / 2, YZBarGlyph, YZBarGlyph);
    _glyph.frame = _disc.bounds;
    _close.frame = CGRectMake(width - YZBarButton - 10, (height - YZBarButton) / 2, YZBarButton, YZBarButton);
    CGFloat right = _close.frame.origin.x - 6;
    CGSize title = _primary.hidden ? CGSizeZero : YZTextSize([_primary titleForState:UIControlStateNormal], _primary.titleLabel.font);
    CGFloat primaryWidth = _primary.hidden ? 0 : MIN(MAX(title.width + 28, (CGFloat)84), (CGFloat)150);
    _primary.frame = CGRectMake(right - primaryWidth, (height - YZBarButton) / 2, primaryWidth, YZBarButton);
    CGFloat textLeft = 14 + YZBarGlyph + 12;
    CGFloat textRight = (_primary.hidden ? right : _primary.frame.origin.x) - 8;
    CGFloat textWidth = MAX((CGFloat)0, textRight - textLeft);
    _title.frame = CGRectMake(textLeft, 13, textWidth, 20);
    _subtitle.frame = CGRectMake(textLeft, 35, textWidth, 18);
    _wave.frame = CGRectMake(textLeft - YZWaveSeekBarInset, 30, textWidth + 2 * YZWaveSeekBarInset, YZWaveSeekBarHeight);
    self.layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:self.bounds cornerRadius:[YZTheme cardRadius]].CGPath;
}

- (void)primaryPressed {
    if (_phase == YZTranslationFailed)
        [_delegate translateBarDidPressRetry:self];
    else if (_phase == YZTranslationDone)
        [_delegate translateBarDidPressOriginal:self];
    else
        [_delegate translateBarDidPressTranslate:self];
}

- (void)optionsPressed {
    [_delegate translateBarDidPressOptions:self];
}

- (void)closePressed {
    [_delegate translateBarDidPressClose:self];
}

- (void)showTranslation:(YZTranslation *)translation animated:(BOOL)animated {
    YZTranslationPhase phase = translation.phase;
    BOOL changed = phase != _phase;
    NSString *pair = [NSString stringWithFormat:@"%@ → %@", YZLanguageNamed(translation.source), YZLanguageNamed(translation.target)];
    NSString *title = nil, *subtitle = nil, *button = nil;
    switch (phase) {
    case YZTranslationOffered:
        title = YZT(@"Перевести эту страницу?");
        subtitle = translation.source ? pair : YZLanguageNamed(translation.target);
        button = YZT(@"Перевести");
        break;
    case YZTranslationWorking:
        title = YZT(@"Перевожу страницу…");
        break;
    case YZTranslationDone:
        title = YZT(@"Страница переведена");
        subtitle = pair;
        button = YZT(@"Оригинал");
        break;
    case YZTranslationFailed: {
        title = YZT(@"Не удалось перевести");
        NSString *failure = translation.failure;
        subtitle = [failure isEqualToString:@"blocked"] ? YZT(@"Сервис перевода сейчас отказывает, попробуйте позже")
                 : [failure isEqualToString:@"network"] ? YZT(@"Нет связи с сервисом перевода")
                 : YZT(@"Сервис вернул неожиданный ответ");
        button = YZT(@"Повторить");
        break;
    }
    case YZTranslationIdle:
        return;
    }
    _phase = phase;
    void (^apply)(void) = ^{
        _title.text = title;
        _subtitle.text = subtitle;
        _subtitle.hidden = phase == YZTranslationWorking;
        _wave.hidden = phase != YZTranslationWorking;
        _wave.waving = phase == YZTranslationWorking;
        [_primary setTitle:button forState:UIControlStateNormal];
        _primary.hidden = !button;
        [self setNeedsLayout];
        [self layoutIfNeeded];
    };
    if (phase == YZTranslationWorking)
        [_wave setValue:MAX(translation.progress, 0.04f) gliding:animated && !changed];
    if (!animated || !self.window) {
        apply();
        return;
    }
    // the words cross over, and the glyph and the button come in past their place when the state is a new one
    [UIView transitionWithView:self duration:0.18 options:UIViewAnimationOptionTransitionCrossDissolve | UIViewAnimationOptionAllowAnimatedContent animations:apply completion:nil];
    if (changed) {
        YZMotionAnimateEnter(_disc, CGAffineTransformMakeScale(0.6f, 0.6f), 1, YOZORA_MOTION_WOBBLE, 0);
        if (!_primary.hidden)
            YZMotionAnimateEnter(_primary, CGAffineTransformMakeScale(0.8f, 0.8f), 0, YOZORA_MOTION_WOBBLE, 0.04);
    }
}

@end
