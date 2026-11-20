#import "YZTranslateSheet.h"
#import "YZCompat.h"
#import "YZControls.h"
#import "YZMotion.h"
#import "YZSettings.h"
#import "YZStrings.h"
#import "YZTheme.h"
#include "yozora_translate.h"
#import <QuartzCore/QuartzCore.h>

static const CGFloat YZFieldHeight = 60;
static const CGFloat YZRowHeight = 52;

static NSString *YZNameOf(NSString *code) {
    int index = yozora_tr_language_find([code UTF8String]);
    return index < 0 ? @"" : [NSString stringWithUTF8String:yozora_tr_language_name((size_t)index)];
}

// the content places its parts whenever it is laid out, the sheet frames it after building and on rotation.
// the sheet is held to 70 percent of the screen, less than the content needs on a small phone, so the content
// scrolls: it was cut at the bottom and the switches under the button could not be reached
@interface YZTranslateSheetContent : YZScrollView {
    void (^_relayout)(void);
}
@property(nonatomic, copy) void (^relayout)(void);
@end

@implementation YZTranslateSheetContent
@synthesize relayout = _relayout;
- (void)layoutSubviews {
    [super layoutSubviews];
    if (_relayout)
        _relayout();
}
- (void)dealloc {
    [_relayout release];
    [super dealloc];
}
@end

@implementation YZTranslateSheet

+ (YZTranslateSheet *)presentInView:(UIView *)host bottomInset:(CGFloat)bottomInset translation:(YZTranslation *)translation
                         completion:(void (^)(NSString *, NSString *, BOOL))completion {
    YZTranslateSheet *sheet = [[YZTranslateSheet alloc] init];
    sheet->_host = host;
    sheet->_bottomInset = bottomInset;
    sheet->_translation = [translation retain];
    sheet->_completion = [completion copy];
    // the person's own choice of the language of the page lasts for this sheet only: what the app found is shown as found
    sheet->_target = [(translation.target ? translation.target : [YZSettings translateTarget]) copy];
    [sheet build];
    // build holds the object until the sheet has answered
    return [sheet autorelease];
}

- (void)dealloc {
    [_sheet release];
    [_translation release];
    [_source release];
    [_target release];
    [_content release];
    [_sourceField release];
    [_targetField release];
    [_sourceName release];
    [_targetName release];
    [_swap release];
    [_primary release];
    [_alwaysLabel release];
    [_neverLabel release];
    [_alwaysToggle release];
    [_neverToggle release];
    [_completion release];
    [super dealloc];
}

- (UIControl *)fieldWithCaption:(NSString *)caption name:(UILabel **)name action:(SEL)action {
    UIControl *field = [[[UIControl alloc] initWithFrame:CGRectZero] autorelease];
    field.backgroundColor = [YZTheme cardColor];
    field.layer.cornerRadius = 18;
    [field addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    YZMotionAttachBounce(field, 0.03f);
    UILabel *small = [[[UILabel alloc] initWithFrame:CGRectMake(18, 9, 240, 16)] autorelease];
    small.text = caption;
    small.font = [YZTheme systemFontOfSize:12];
    small.textColor = [YZTheme subtleTextColor];
    small.backgroundColor = [UIColor clearColor];
    small.userInteractionEnabled = NO;
    small.tag = 1;
    [field addSubview:small];
    UILabel *label = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    label.font = [YZTheme boldSystemFontOfSize:17];
    label.textColor = [YZTheme textColor];
    label.backgroundColor = [UIColor clearColor];
    label.userInteractionEnabled = NO;
    [field addSubview:label];
    UIImageView *chevron = [[[UIImageView alloc] initWithImage:[YZTheme icon:YZIconChevron size:20 color:[YZTheme subtleTextColor]]] autorelease];
    chevron.userInteractionEnabled = NO;
    chevron.tag = 2;
    [field addSubview:chevron];
    *name = label;
    return field;
}

- (UILabel *)toggleLabel {
    UILabel *label = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    label.font = [YZTheme systemFontOfSize:15];
    label.textColor = [YZTheme textColor];
    label.backgroundColor = [UIColor clearColor];
    label.numberOfLines = 2;
    return label;
}

- (BOOL)hasDetectedLanguage {
    return [_translation.source length] > 0;
}

- (void)build {
    _sheet = [[YZSheet alloc] initWithFrame:_host.bounds];
    _sheet.bottomInset = MAX(_bottomInset, (CGFloat)8);
    [_sheet setTitle:YZT(@"Перевод страницы") doneTitle:nil];
    YZTranslateSheetContent *content = [[YZTranslateSheetContent alloc] initWithFrame:CGRectZero];
    // the content must not keep this object alive: the object holds the content, and the block is dropped when the sheet is finished
    __block YZTranslateSheet *weakSelf = self;
    content.relayout = ^{ [weakSelf layoutContent]; };
    content.alwaysBounceVertical = NO;
    content.showsHorizontalScrollIndicator = NO;
    _content = content;
    UILabel *name = nil;
    _sourceField = [[self fieldWithCaption:YZT(@"Язык страницы") name:&name action:@selector(pickSource)] retain];
    _sourceName = [name retain];
    [_content addSubview:_sourceField];
    _targetField = [[self fieldWithCaption:YZT(@"Перевести на") name:&name action:@selector(pickTarget)] retain];
    _targetName = [name retain];
    [_content addSubview:_targetField];
    _swap = [[YZStateButton buttonWithType:UIButtonTypeCustom] retain];
    [(YZStateButton *)_swap setPillSize:CGSizeMake(40, 40)];
    [_swap setImage:[YZTheme icon:YZIconSwap size:22 color:[YZTheme accentColor]] forState:UIControlStateNormal];
    [_swap addTarget:self action:@selector(swapPressed) forControlEvents:UIControlEventTouchUpInside];
    YZMotionAttachBounce(_swap, 0.1f);
    [_content addSubview:_swap];
    _primary = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    _primary.titleLabel.font = [YZTheme boldSystemFontOfSize:16];
    _primary.layer.cornerRadius = 24;
    _primary.backgroundColor = [YZTheme accentColor];
    [_primary setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    [_primary addTarget:self action:@selector(primaryPressed) forControlEvents:UIControlEventTouchUpInside];
    YZMotionAttachBounce(_primary, 0.04f);
    [_content addSubview:_primary];
    CGFloat height = 2 * YZFieldHeight + 12 + 16 + 48;
    if ([self hasDetectedLanguage]) {
        _alwaysLabel = [[self toggleLabel] retain];
        _neverLabel = [[self toggleLabel] retain];
        _alwaysToggle = [[YZToggle alloc] initWithFrame:CGRectZero];
        _neverToggle = [[YZToggle alloc] initWithFrame:CGRectZero];
        [_alwaysToggle setOn:[YZSettings translatesAtOnce:_translation.source] animated:NO];
        [_neverToggle setOn:[YZSettings translateNeverOffers:_translation.source] animated:NO];
        [_alwaysToggle addTarget:self action:@selector(alwaysChanged) forControlEvents:UIControlEventValueChanged];
        [_neverToggle addTarget:self action:@selector(neverChanged) forControlEvents:UIControlEventValueChanged];
        [_content addSubview:_alwaysLabel];
        [_content addSubview:_neverLabel];
        [_content addSubview:_alwaysToggle];
        [_content addSubview:_neverToggle];
        height += 16 + 2 * YZRowHeight;
    }
    _sheet.content = _content;
    _sheet.contentHeight = height + 16;
    __block YZTranslateSheet *me = self;
    _sheet.onCancel = ^{
        [me finish];
    };
    [self refresh];
    [self retain];
    [_sheet presentInView:_host];
}

- (void)layoutContent {
    CGFloat width = _content.bounds.size.width;
    CGFloat side = 16;
    CGFloat inner = MAX((CGFloat)0, width - 2 * side);
    CGFloat y = 4;
    _sourceField.frame = CGRectMake(side, y, inner, YZFieldHeight);
    y += YZFieldHeight + 12;
    _targetField.frame = CGRectMake(side, y, inner, YZFieldHeight);
    // the swap button sits on the line between the two fields, at the right
    _swap.frame = CGRectMake(width - side - 64, YZFieldHeight + 4 + 6 - 14 + 6, 40, 40);
    for (UIControl *field in [NSArray arrayWithObjects:_sourceField, _targetField, nil]) {
        UILabel *name = field == _sourceField ? _sourceName : _targetName;
        name.frame = CGRectMake(18, 28, MAX((CGFloat)0, field.bounds.size.width - 60), 24);
        [field viewWithTag:2].frame = CGRectMake(field.bounds.size.width - 40, (YZFieldHeight - 20) / 2, 20, 20);
        [field viewWithTag:1].frame = CGRectMake(18, 9, MAX((CGFloat)0, field.bounds.size.width - 60), 16);
    }
    y += YZFieldHeight + 16;
    _primary.frame = CGRectMake(side, y, inner, 48);
    y += 48 + 16;
    if (_alwaysLabel) {
        _alwaysLabel.frame = CGRectMake(side + 4, y, MAX((CGFloat)0, inner - 72), YZRowHeight);
        _alwaysToggle.frame = CGRectMake(width - side - 52, y + (YZRowHeight - 32) / 2, 52, 32);
        y += YZRowHeight;
        _neverLabel.frame = CGRectMake(side + 4, y, MAX((CGFloat)0, inner - 72), YZRowHeight);
        _neverToggle.frame = CGRectMake(width - side - 52, y + (YZRowHeight - 32) / 2, 52, 32);
        y += YZRowHeight;
    }
    // the height the sheet asked for: the content scrolls only when the sheet had to be made shorter than that
    [(UIScrollView *)_content setContentSize:CGSizeMake(width, _sheet.contentHeight)];
}

// the names on the fields, the state of the button and of the swap, after every change
- (void)refresh {
    NSString *detected = [self hasDetectedLanguage] ? YZNameOf(_translation.source) : nil;
    _sourceName.text = _source ? YZNameOf(_source) : (detected ? [NSString stringWithFormat:@"%@ · %@", YZT(@"Определить"), detected] : YZT(@"Определить автоматически"));
    _targetName.text = YZNameOf(_target);
    BOOL same = _translation.phase == YZTranslationDone && [_target isEqualToString:_translation.target]
        && (_source ? [_source isEqualToString:_translation.source] : YES);
    [_primary setTitle:same ? YZT(@"Показать оригинал") : YZT(@"Перевести") forState:UIControlStateNormal];
    _swap.enabled = _source != nil || detected != nil;
    _swap.alpha = _swap.enabled ? 1 : 0.35f;
    if (_alwaysLabel) {
        _alwaysLabel.text = [NSString stringWithFormat:YZT(@"Всегда переводить с языка: %@"), detected];
        _neverLabel.text = [NSString stringWithFormat:YZT(@"Не предлагать перевод с языка: %@"), detected];
    }
    [_content setNeedsLayout];
    [self layoutContent];
}

- (void)pickLanguageWithTitle:(NSString *)title includeAuto:(BOOL)auto_ current:(NSString *)current done:(void (^)(NSString *code))done {
    NSMutableArray *texts = [NSMutableArray array], *codes = [NSMutableArray array], *flags = [NSMutableArray array];
    NSInteger selected = -1;
    if (auto_) {
        [texts addObject:YZT(@"Определить автоматически")];
        [codes addObject:[NSNull null]];
        [flags addObject:[NSNumber numberWithInt:0]];
        if (!current)
            selected = 0;
    }
    for (size_t i = 0; i < yozora_tr_language_count(); ++i) {
        NSString *code = [NSString stringWithUTF8String:yozora_tr_language_code(i)];
        [texts addObject:[NSString stringWithUTF8String:yozora_tr_language_name(i)]];
        [codes addObject:code];
        [flags addObject:[NSNumber numberWithInt:0]];
        if (current && [code isEqualToString:current])
            selected = (NSInteger)[texts count] - 1;
    }
    void (^answer)(NSString *) = [[done copy] autorelease];
    YZSheet *list = [YZPickers presentChoicesInView:_host title:title texts:texts flags:flags selected:selected bottomInset:_bottomInset completion:^(NSInteger index) {
        _list = nil;
        if (index < 0 || (NSUInteger)index >= [codes count])
            return;
        id code = [codes objectAtIndex:(NSUInteger)index];
        answer([code isKindOfClass:[NSString class]] ? code : nil);
    }];
    _list = list;
}

- (void)pickSource {
    [self pickLanguageWithTitle:YZT(@"Язык страницы") includeAuto:YES current:_source done:^(NSString *code) {
        [_source release];
        _source = [code copy];
        [self refresh];
    }];
}

- (void)pickTarget {
    [self pickLanguageWithTitle:YZT(@"Перевести на") includeAuto:NO current:_target done:^(NSString *code) {
        if (!code)
            return;
        [_target release];
        _target = [code copy];
        [YZSettings setTranslateTarget:code];
        [self refresh];
    }];
}

// the pair turns around: the language of the page becomes the language of the translation. only a known language can
- (void)swapPressed {
    NSString *from = _source ? _source : ([self hasDetectedLanguage] ? _translation.source : nil);
    if (!from)
        return;
    NSString *to = [[_target copy] autorelease];
    [_source release];
    _source = [to copy];
    [_target release];
    _target = [from copy];
    [YZSettings setTranslateTarget:_target];
    [self refresh];
    YZMotionAnimateEnter(_sourceField, CGAffineTransformMakeScale(0.96f, 0.96f), 1, YOZORA_MOTION_WOBBLE, 0);
    YZMotionAnimateEnter(_targetField, CGAffineTransformMakeScale(0.96f, 0.96f), 1, YOZORA_MOTION_WOBBLE, 0.03);
}

- (void)alwaysChanged {
    BOOL on = [_alwaysToggle isOn];
    [YZSettings setTranslatesAtOnce:on language:_translation.source];
    if (on)
        [_neverToggle setOn:NO animated:YES];
}

- (void)neverChanged {
    BOOL on = [_neverToggle isOn];
    [YZSettings setTranslateNeverOffers:on language:_translation.source];
    if (on)
        [_alwaysToggle setOn:NO animated:YES];
}

- (void)primaryPressed {
    BOOL same = _translation.phase == YZTranslationDone && [_target isEqualToString:_translation.target]
        && (_source ? [_source isEqualToString:_translation.source] : YES);
    void (^completion)(NSString *, NSString *, BOOL) = [_completion retain];
    NSString *source = [[_source copy] autorelease], *target = [[_target copy] autorelease];
    [_sheet dismissAnimated:YES];
    [self finish];
    if (completion)
        completion(source, target, same);
    [completion release];
}

// the sheet has no more to say: the list over it closes, and the object lets go of itself once
- (void)finish {
    if (_done)
        return;
    _done = YES;
    [_list dismissAnimated:NO];
    _list = nil;
    [_alwaysToggle removeTarget:self action:NULL forControlEvents:UIControlEventAllEvents];
    [_neverToggle removeTarget:self action:NULL forControlEvents:UIControlEventAllEvents];
    [_completion release];
    _completion = nil;
    _sheet.onCancel = nil;
    [(YZTranslateSheetContent *)_content setRelayout:nil];
    [self release];
}

- (void)dismiss {
    [_sheet dismissAnimated:YES];
    [self finish];
}

@end
