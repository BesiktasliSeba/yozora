#import "YZDnsPopup.h"
#import "YZCompat.h"
#import "YZDialog.h"
#import "YZMotion.h"
#import "YZStrings.h"
#import "YZTheme.h"
#include "yozora_dns.h"
#import <QuartzCore/QuartzCore.h>

static const CGFloat YZDnsCardWidth = 340;
static const CGFloat YZDnsPad = 22;
static const CGFloat YZDnsFieldHeight = 56;

// why the core refused a text, in the words of the card
static NSString *YZDnsProblem(yozora_dns_status status) {
    switch (status) {
    case YOZORA_DNS_INSECURE: return YZT(@"Адрес без HTTPS отправил бы имена сайтов открытым текстом");
    case YOZORA_DNS_TOO_LONG: return YZT(@"Адрес слишком длинный");
    default: return YZT(@"Нужен адрес сервера: 1.1.1.1, dns.example.org или https://…/dns-query");
    }
}

// the field is as tall as its box and its text stands in the middle of that height, with no placeholder
static UITextField *YZDnsMakeField(id<UITextFieldDelegate> delegate) {
    UITextField *field = [[[UITextField alloc] initWithFrame:CGRectZero] autorelease];
    field.autocapitalizationType = UITextAutocapitalizationTypeNone;
    field.autocorrectionType = UITextAutocorrectionTypeNo;
    field.keyboardType = UIKeyboardTypeURL;
    field.returnKeyType = UIReturnKeyDone;
    field.clearButtonMode = UITextFieldViewModeWhileEditing;
    field.contentVerticalAlignment = UIControlContentVerticalAlignmentCenter;
    field.delegate = delegate;
    return field;
}

@interface YZDnsCard : UIView <UITextFieldDelegate, UIGestureRecognizerDelegate> {
    UIView *_card;
    UIView *_badge;
    UIImageView *_badgeIcon;
    UILabel *_title;
    UILabel *_subtitle;
    UIView *_fieldBox;
    UITextField *_field;
    UILabel *_status;
    UIButton *_cancel;
    UIButton *_done;
    yozora_dns_status _fieldStatus;
    char _fieldUrl[YOZORA_DNS_CAPACITY];
    CGFloat _keyboardHeight;
    BOOL _leaving;
    void (^_doneBlock)(NSString *);
}
- (id)initWithCurrent:(NSString *)current done:(void (^)(NSString *url))done;
- (void)showInView:(UIView *)host;
@end

@implementation YZDnsCard

- (id)initWithCurrent:(NSString *)current done:(void (^)(NSString *url))done {
    self = [super initWithFrame:CGRectZero];
    if (!self)
        return nil;
    _doneBlock = [done copy];
    self.backgroundColor = [YZTheme scrimColor];
    self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _card = [[UIView alloc] initWithFrame:CGRectZero];
    _card.layer.cornerRadius = 28;
    _card.layer.shadowColor = [[UIColor blackColor] CGColor];
    _card.layer.shadowOpacity = 0.32f;
    _card.layer.shadowRadius = 22;
    _card.layer.shadowOffset = CGSizeMake(0, 10);
    [self addSubview:_card];
    _badge = [[UIView alloc] initWithFrame:CGRectZero];
    _badge.layer.cornerRadius = 22;
    [_card addSubview:_badge];
    _badgeIcon = [[UIImageView alloc] initWithFrame:CGRectZero];
    _badgeIcon.contentMode = UIViewContentModeCenter;
    [_badge addSubview:_badgeIcon];
    _title = [[UILabel alloc] initWithFrame:CGRectZero];
    _title.backgroundColor = [UIColor clearColor];
    _title.text = @"DNS";
    [_card addSubview:_title];
    _subtitle = [[UILabel alloc] initWithFrame:CGRectZero];
    _subtitle.backgroundColor = [UIColor clearColor];
    _subtitle.numberOfLines = 3;
    _subtitle.text = YZT(@"Через какой сервер находятся адреса сайтов. Запросы к нему идут по HTTPS (DoH)");
    [_card addSubview:_subtitle];
    _fieldBox = [[UIView alloc] initWithFrame:CGRectZero];
    _fieldBox.layer.cornerRadius = 18;
    [_card addSubview:_fieldBox];
    _field = [YZDnsMakeField(self) retain];
    [_field addTarget:self action:@selector(checkField) forControlEvents:UIControlEventEditingChanged];
    _field.text = current ? current : @"";
    [_fieldBox addSubview:_field];
    _status = [[UILabel alloc] initWithFrame:CGRectZero];
    _status.backgroundColor = [UIColor clearColor];
    _status.numberOfLines = 2;
    [_card addSubview:_status];
    _cancel = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    [_cancel setTitle:YZT(@"Отмена") forState:UIControlStateNormal];
    [_cancel addTarget:self action:@selector(cancelPressed) forControlEvents:UIControlEventTouchUpInside];
    YZMotionAttachBounce(_cancel, 0.05f);
    [_card addSubview:_cancel];
    _done = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    _done.layer.cornerRadius = 24;
    [_done setTitle:YZT(@"Готово") forState:UIControlStateNormal];
    [_done addTarget:self action:@selector(donePressed) forControlEvents:UIControlEventTouchUpInside];
    YZMotionAttachBounce(_done, 0.05f);
    [_card addSubview:_done];
    // a tap on the card or beside it puts the keyboard away, the card itself stays
    UITapGestureRecognizer *tap = [[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(tappedAway)] autorelease];
    tap.delegate = self;
    tap.cancelsTouchesInView = NO;
    [self addGestureRecognizer:tap];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(keyboardChanged:) name:UIKeyboardWillShowNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(keyboardChanged:) name:UIKeyboardWillHideNotification object:nil];
    [self restyle];
    [self checkField];
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _field.delegate = nil;
    [_card release];
    [_badge release];
    [_badgeIcon release];
    [_title release];
    [_subtitle release];
    [_fieldBox release];
    [_field release];
    [_status release];
    [_cancel release];
    [_done release];
    [_doneBlock release];
    [super dealloc];
}

- (void)restyle {
    _card.backgroundColor = [YZTheme surfaceColor];
    _badge.backgroundColor = [[YZTheme accentColor] colorWithAlphaComponent:0.16f];
    _badgeIcon.image = [YZTheme icon:YZIconShield size:26 color:[YZTheme accentColor]];
    _title.font = [YZTheme boldSystemFontOfSize:24];
    _title.textColor = [YZTheme textColor];
    _subtitle.font = [YZTheme systemFontOfSize:13];
    _subtitle.textColor = [YZTheme subtleTextColor];
    _fieldBox.backgroundColor = [YZTheme fieldColor];
    _field.font = [YZTheme systemFontOfSize:17];
    _field.textColor = [YZTheme textColor];
    _status.font = [YZTheme systemFontOfSize:12];
    _cancel.titleLabel.font = [YZTheme boldSystemFontOfSize:16];
    [_cancel setTitleColor:[YZTheme subtleTextColor] forState:UIControlStateNormal];
    _done.titleLabel.font = [YZTheme boldSystemFontOfSize:16];
}

// what is typed is checked as it is typed: the address the core will ask, the system when the field is empty, or why the text is refused
- (void)checkField {
    NSString *text = [_field.text length] ? _field.text : @"";
    _fieldStatus = yozora_dns_normalize([text UTF8String], _fieldUrl, sizeof _fieldUrl);
    BOOL valid = _fieldStatus == YOZORA_DNS_OK || _fieldStatus == YOZORA_DNS_EMPTY;
    if (_fieldStatus == YOZORA_DNS_OK) {
        _status.textColor = [YZTheme accentColor];
        _status.text = [NSString stringWithFormat:@"%@ %s", YZT(@"Спросим"), _fieldUrl];
    } else if (_fieldStatus == YOZORA_DNS_EMPTY) {
        _status.textColor = [YZTheme subtleTextColor];
        _status.text = YZT(@"Пустое поле: системный DNS");
    } else {
        _status.textColor = [UIColor colorWithRed:0.86f green:0.22f blue:0.22f alpha:1];
        _status.text = YZDnsProblem(_fieldStatus);
    }
    _fieldBox.layer.borderWidth = valid ? 0 : 2;
    _fieldBox.layer.borderColor = [[UIColor colorWithRed:0.86f green:0.22f blue:0.22f alpha:1] CGColor];
    _done.enabled = valid;
    _done.backgroundColor = valid ? [YZTheme accentColor] : [[YZTheme accentColor] colorWithAlphaComponent:0.35f];
    [_done setTitleColor:[YZTheme surfaceColor] forState:UIControlStateNormal];
}

- (BOOL)textFieldShouldReturn:(UITextField *)field {
    [field resignFirstResponder];
    return NO;
}

- (void)tappedAway {
    [self endEditing:YES];
}

// the field and the keys keep their own touches: a tap on the field would otherwise end the editing it starts
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)recognizer shouldReceiveTouch:(UITouch *)touch {
    (void)recognizer;
    return ![touch.view isDescendantOfView:_fieldBox] && ![touch.view isKindOfClass:[UIControl class]];
}

- (void)keyboardChanged:(NSNotification *)note {
    if (!self.window)
        return;
    CGRect end = [[[note userInfo] objectForKey:UIKeyboardFrameEndUserInfoKey] CGRectValue];
    CGRect local = [self convertRect:[self.window convertRect:end fromWindow:nil] fromView:self.window];
    BOOL hiding = [[note name] isEqualToString:UIKeyboardWillHideNotification];
    _keyboardHeight = hiding ? 0 : MAX((CGFloat)0, CGRectGetMaxY(self.bounds) - local.origin.y);
    [self setNeedsLayout];
    [UIView animateWithDuration:0.25 animations:^{ [self layoutIfNeeded]; }];
}

- (CGFloat)cardHeight {
    return YZDnsPad + 44 + 12 + 50 + 16 + YZDnsFieldHeight + 8 + 32 + 14 + 48 + YZDnsPad;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = MIN(YZDnsCardWidth, self.bounds.size.width - 32);
    CGFloat height = MIN([self cardHeight], self.bounds.size.height - 24);
    CGFloat top = MAX((CGFloat)12, floor((self.bounds.size.height - _keyboardHeight - height) / 2));
    _card.frame = CGRectMake(floor((self.bounds.size.width - width) / 2), top, width, height);
    _card.layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:_card.bounds cornerRadius:28].CGPath;
    CGFloat inner = width - 2 * YZDnsPad, y = YZDnsPad;
    _badge.frame = CGRectMake(YZDnsPad, y, 44, 44);
    _badgeIcon.frame = _badge.bounds;
    _title.frame = CGRectMake(YZDnsPad + 44 + 14, y, inner - 58, 44);
    y += 44 + 12;
    _subtitle.frame = CGRectMake(YZDnsPad, y, inner, 50);
    y += 50 + 16;
    _fieldBox.frame = CGRectMake(YZDnsPad, y, inner, YZDnsFieldHeight);
    _field.frame = CGRectMake(18, 0, inner - 36, YZDnsFieldHeight);
    y += YZDnsFieldHeight + 8;
    _status.frame = CGRectMake(YZDnsPad + 4, y, inner - 8, 32);
    y += 32 + 14;
    CGFloat half = floor((inner - 12) / 2);
    _cancel.frame = CGRectMake(YZDnsPad, y, half, 48);
    _done.frame = CGRectMake(YZDnsPad + half + 12, y, half, 48);
}

- (void)showInView:(UIView *)host {
    self.frame = host.bounds;
    [host addSubview:self];
    [self layoutIfNeeded];
    self.alpha = 0;
    YZMotionAnimateView(self, CGAffineTransformIdentity, 1, YOZORA_MOTION_ENTER, 0, nil);
    YZMotionAnimateEnter(_card, CGAffineTransformConcat(CGAffineTransformMakeScale(0.88f, 0.88f), CGAffineTransformMakeTranslation(0, 24)), 0, YOZORA_MOTION_WOBBLE, 0);
    YZMotionAnimateEnter(_badge, CGAffineTransformMakeScale(0.4f, 0.4f), 0, YOZORA_MOTION_WOBBLE, 0.04);
    YZMotionAnimateEnter(_fieldBox, CGAffineTransformMakeTranslation(0, 14), 0, YOZORA_MOTION_WOBBLE, 0.08);
    [_field becomeFirstResponder];
}

- (void)leaveThen:(void (^)(void))after {
    if (_leaving)
        return;
    _leaving = YES;
    self.userInteractionEnabled = NO;
    [self endEditing:YES];
    [self retain];
    void (^finish)(void) = [after copy];
    YZMotionAnimateView(_card, CGAffineTransformConcat(CGAffineTransformMakeScale(0.94f, 0.94f), CGAffineTransformMakeTranslation(0, 10)), 0, YOZORA_MOTION_DISMISS, 0, nil);
    YZMotionAnimateView(self, CGAffineTransformIdentity, 0, YOZORA_MOTION_DISMISS, 0, ^(BOOL finished) {
        (void)finished;
        [self removeFromSuperview];
        if (finish)
            finish();
        [finish release];
        [self autorelease];
    });
}

- (void)cancelPressed {
    [self leaveThen:nil];
}

- (void)donePressed {
    if (!_done.enabled)
        return;
    // the address the core made of the text, or the system when the field is empty
    NSString *url = _fieldStatus == YOZORA_DNS_OK ? [NSString stringWithUTF8String:_fieldUrl] : @"";
    void (^handler)(NSString *) = [_doneBlock retain];
    [self leaveThen:^{
        if (handler)
            handler(url);
        [handler release];
    }];
}

@end

@implementation YZDnsPopup

// the flat and the classic design ask the way their other questions are asked: a card with one field. an empty field is the system, the field starts
// with the server in use, and a text the core refuses is explained and asked again with what was typed
+ (void)askInView:(UIView *)host text:(NSString *)text done:(void (^)(NSString *url))done {
    YZDialog *dialog = [YZDialog dialogWithTitle:@"DNS" message:YZT(@"Запросы идут по HTTPS. Пустое поле возвращает системный DNS.") cancelTitle:YZT(@"Отмена")
                                    confirmTitle:YZT(@"Готово") destructive:NO handler:^(NSInteger index, NSArray *texts) {
        if (index != 1)
            return;
        NSString *typed = [texts count] ? [texts objectAtIndex:0] : @"";
        char url[YOZORA_DNS_CAPACITY];
        yozora_dns_status status = yozora_dns_normalize([typed UTF8String], url, sizeof url);
        if (status == YOZORA_DNS_OK || status == YOZORA_DNS_EMPTY) {
            if (done)
                done(status == YOZORA_DNS_OK ? [NSString stringWithUTF8String:url] : @"");
            return;
        }
        YZDialog *problem = [YZDialog dialogWithTitle:YZT(@"Адрес не подходит") message:YZDnsProblem(status) cancelTitle:nil confirmTitle:YZT(@"Исправить")
                                          destructive:NO handler:^(NSInteger answer, NSArray *unused) {
            (void)answer; (void)unused;
            [YZDnsPopup askInView:host text:typed done:done];
        }];
        [problem showInView:host];
    }];
    // no placeholder: the field is an empty line until something is typed
    [dialog setFieldPlaceholders:[NSArray arrayWithObject:@""] secureIndex:-1];
    [dialog setFieldTexts:[NSArray arrayWithObject:text ? text : @""]];
    [dialog showInView:host];
}

+ (void)presentInView:(UIView *)host current:(NSString *)current done:(void (^)(NSString *url))done {
    if ([YZTheme isClassic] || [YZTheme isFlat]) {
        [self askInView:host text:current done:done];
        return;
    }
    YZDnsCard *card = [[[YZDnsCard alloc] initWithCurrent:current done:done] autorelease];
    [card showInView:host];
}

@end
