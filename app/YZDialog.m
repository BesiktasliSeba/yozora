#import "YZDialog.h"
#import "YZCompat.h"
#import "YZMotion.h"
#import "YZTheme.h"
#import <QuartzCore/QuartzCore.h>
#import <objc/message.h>

static const CGFloat YZDialogMaxWidth = 320;
static const CGFloat YZDialogSide = 24;
static const CGFloat YZDialogPad = 24;
static const CGFloat YZDialogButtonHeight = 44;
static const CGFloat YZDialogFieldHeight = 44;

@implementation YZDialog
@synthesize messageText = _messageText;
@synthesize isShown = _shown;
@dynamic picture;

+ (YZDialog *)dialogWithTitle:(NSString *)title message:(NSString *)message cancelTitle:(NSString *)cancelTitle confirmTitle:(NSString *)confirmTitle
                  destructive:(BOOL)destructive handler:(void (^)(NSInteger index, NSArray *texts))handler {
    YZDialog *dialog = [[[YZDialog alloc] initWithFrame:CGRectZero] autorelease];
    dialog->_titleText = [title copy];
    dialog->_messageText = [message copy];
    dialog->_cancelTitle = [cancelTitle copy];
    dialog->_confirmTitle = [confirmTitle copy];
    dialog->_destructive = destructive;
    dialog->_handler = [handler copy];
    [dialog buildButtons];
    return dialog;
}

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    _secureIndex = -1;
    self.backgroundColor = [YZTheme scrimColor];
    self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _card = [[UIView alloc] initWithFrame:CGRectZero];
    _card.backgroundColor = [YZTheme surfaceColor];
    _card.layer.cornerRadius = [YZTheme sheetRadius];
    _card.layer.shadowColor = [UIColor blackColor].CGColor;
    _card.layer.shadowOpacity = 0.3f;
    _card.layer.shadowRadius = 18;
    _card.layer.shadowOffset = CGSizeMake(0, 8);
    [self addSubview:_card];
    _title = [[UILabel alloc] initWithFrame:CGRectZero];
    _title.backgroundColor = [UIColor clearColor];
    _title.textColor = [YZTheme textColor];
    _title.font = [YZTheme boldSystemFontOfSize:20];
    _title.numberOfLines = 0;
    [_card addSubview:_title];
    _message = [[UILabel alloc] initWithFrame:CGRectZero];
    _message.backgroundColor = [UIColor clearColor];
    _message.textColor = [YZTheme subtleTextColor];
    _message.font = [YZTheme systemFontOfSize:15];
    _message.numberOfLines = 0;
    [_card addSubview:_message];
    _fields = [[NSMutableArray alloc] init];
    _buttons = [[NSMutableArray alloc] init];
    // a tap on the card or beside it puts the keyboard away, the dialog itself stays
    UITapGestureRecognizer *tap = [[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(tappedAway)] autorelease];
    tap.delegate = self;
    tap.cancelsTouchesInView = NO;
    [self addGestureRecognizer:tap];
    // the card moves up when the keyboard would cover its fields
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(keyboardChanged:) name:UIKeyboardWillShowNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(keyboardChanged:) name:UIKeyboardWillHideNotification object:nil];
    return self;
}

- (UIImage *)picture { return _pictureView.image; }

- (void)setPicture:(UIImage *)picture {
    if (!picture) {
        [_pictureView removeFromSuperview];
        [_pictureView release];
        _pictureView = nil;
        return;
    }
    if (!_pictureView) {
        _pictureView = [[UIImageView alloc] initWithFrame:CGRectZero];
        _pictureView.contentMode = UIViewContentModeScaleAspectFit;
        [_card addSubview:_pictureView];
    }
    _pictureView.image = picture;
    _title.textAlignment = YZTextAlignmentCenter;
    _message.textAlignment = YZTextAlignmentCenter;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_rules release];
    [_pictureView release];
    [_card release];
    [_title release];
    [_message release];
    [_fields release];
    [_buttons release];
    [_titleText release];
    [_messageText release];
    [_placeholders release];
    [_initialTexts release];
    [_cancelTitle release];
    [_confirmTitle release];
    [_answerTexts release];
    [_handler release];
    [super dealloc];
}

- (UIButton *)makeButton:(NSString *)title filled:(UIColor *)fill text:(UIColor *)text index:(NSInteger)index {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeCustom];
    button.titleLabel.font = [YZTheme boldSystemFontOfSize:16];
    button.backgroundColor = fill;
    if ([YZTheme isFlat]) {
        // the alert of ios 7 to 12: text over hairlines, the accent in the answer that is meant, the other one in plain weight
        button.backgroundColor = [UIColor clearColor];
        text = [YZTheme accentColor];
        button.titleLabel.font = index == 0 && _cancelTitle ? [UIFont systemFontOfSize:17] : [UIFont boldSystemFontOfSize:17];
        if (_destructive && index != 0)
            text = [YZTheme errorColor];
    }
    button.layer.cornerRadius = [YZTheme isFlat] ? 10 : YZDialogButtonHeight / 2;
    button.tag = index;
    [button setTitle:title forState:UIControlStateNormal];
    [button setTitleColor:text forState:UIControlStateNormal];
    [button addTarget:self action:@selector(buttonPressed:) forControlEvents:UIControlEventTouchUpInside];
    YZMotionAttachBounce(button, 0.04f);
    [_card addSubview:button];
    [_buttons addObject:button];
    return button;
}

- (void)buildButtons {
    UIColor *accent = _destructive ? [YZTheme errorColor] : [YZTheme accentColor];
    if (_cancelTitle) {
        [self makeButton:_cancelTitle filled:[YZTheme stateColor] text:[YZTheme textColor] index:0];
        [self makeButton:_confirmTitle filled:accent text:[UIColor whiteColor] index:1];
    } else {
        [self makeButton:_confirmTitle filled:accent text:[UIColor whiteColor] index:0];
    }
}

- (void)setFieldPlaceholders:(NSArray *)placeholders secureIndex:(NSInteger)secureIndex {
    [_placeholders release];
    _placeholders = [placeholders copy];
    _secureIndex = secureIndex;
    for (UITextField *field in _fields)
        [field removeFromSuperview];
    [_fields removeAllObjects];
    for (NSUInteger i = 0; i < [_placeholders count]; ++i) {
        UITextField *field = [[[UITextField alloc] initWithFrame:CGRectZero] autorelease];
        field.backgroundColor = [YZTheme fieldColor];
        field.layer.cornerRadius = 12;
        field.font = [YZTheme systemFontOfSize:16];
        field.textColor = [YZTheme textColor];
        field.autocapitalizationType = UITextAutocapitalizationTypeNone;
        field.autocorrectionType = UITextAutocorrectionTypeNo;
        field.secureTextEntry = (NSInteger)i == secureIndex;
        field.returnKeyType = i + 1 < [_placeholders count] ? UIReturnKeyNext : UIReturnKeyDone;
        field.clearButtonMode = UITextFieldViewModeWhileEditing;
        // the text stands in the middle of the height of the field
        field.contentVerticalAlignment = UIControlContentVerticalAlignmentCenter;
        field.delegate = self;
        field.leftView = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, 14, 10)] autorelease];
        field.leftViewMode = UITextFieldViewModeAlways;
        YZSetPlaceholder(field, [_placeholders objectAtIndex:i], [YZTheme subtleTextColor]);
        [_card addSubview:field];
        [_fields addObject:field];
    }
    [self setNeedsLayout];
}

- (void)setFieldTexts:(NSArray *)texts {
    [_initialTexts release];
    _initialTexts = [texts copy];
    for (NSUInteger i = 0; i < [_fields count] && i < [_initialTexts count]; ++i)
        [[_fields objectAtIndex:i] setText:[_initialTexts objectAtIndex:i]];
}

- (void)setMessageText:(NSString *)text {
    if (text == _messageText || [text isEqualToString:_messageText])
        return;
    [text retain];
    [_messageText release];
    _messageText = text;
    if (_alert)
        ((void (*)(id, SEL, NSString *))objc_msgSend)(_alert, NSSelectorFromString(@"setMessage:"), text);
    [self setNeedsLayout];
}

- (void)tappedAway {
    [self endEditing:YES];
}

// the fields and the buttons keep their own touches: a tap on a field would otherwise end the editing it starts
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)recognizer shouldReceiveTouch:(UITouch *)touch {
    (void)recognizer;
    if ([touch.view isKindOfClass:[UIControl class]])
        return NO;
    for (UITextField *field in _fields) {
        if ([touch.view isDescendantOfView:field])
            return NO;
    }
    return YES;
}

- (BOOL)textFieldShouldReturn:(UITextField *)field {
    NSUInteger index = [_fields indexOfObject:field];
    if (index != NSNotFound && index + 1 < [_fields count])
        [[_fields objectAtIndex:index + 1] becomeFirstResponder];
    else
        [self buttonPressed:[_buttons lastObject]];
    return NO;
}

- (void)keyboardChanged:(NSNotification *)note {
    if (!_shown || !self.window)
        return;
    CGRect end = [[[note userInfo] objectForKey:UIKeyboardFrameEndUserInfoKey] CGRectValue];
    CGRect local = [self convertRect:[self.window convertRect:end fromWindow:nil] fromView:self.window];
    BOOL hiding = [[note name] isEqualToString:UIKeyboardWillHideNotification];
    _keyboardHeight = hiding ? 0 : MAX((CGFloat)0, CGRectGetMaxY(self.bounds) - local.origin.y);
    [self setNeedsLayout];
    [UIView animateWithDuration:0.25 animations:^{ [self layoutIfNeeded]; }];
}

- (void)layoutFlat {
    CGFloat width = MIN(self.bounds.size.width - 2 * YZDialogSide, (CGFloat)270);
    CGFloat inner = MAX((CGFloat)0, width - 32);
    _card.layer.cornerRadius = 13;
    _card.backgroundColor = [YZTheme cardColor];
    _title.text = _titleText;
    _message.text = _messageText;
    _title.textAlignment = YZTextAlignmentCenter;
    _message.textAlignment = YZTextAlignmentCenter;
    _title.font = [UIFont boldSystemFontOfSize:17];
    _message.font = [UIFont systemFontOfSize:13];
    CGFloat y = 20;
    if (_pictureView) {
        _pictureView.frame = CGRectMake(floor((width - 96) / 2), y, 96, 96);
        y += 96 + 8;
    }
    CGSize title = [_title sizeThatFits:CGSizeMake(inner, 400)];
    _title.frame = CGRectMake(16, y, inner, ceil(title.height));
    y += ceil(title.height);
    if ([_messageText length]) {
        CGSize message = [_message sizeThatFits:CGSizeMake(inner, 400)];
        y += 4;
        _message.frame = CGRectMake(16, y, inner, ceil(message.height));
        y += ceil(message.height);
    } else
        _message.frame = CGRectZero;
    for (UITextField *field in _fields) {
        y += 12;
        field.frame = CGRectMake(16, y, inner, 30);
        y += 30;
    }
    y += 20;
    if (!_rules)
        _rules = [[NSMutableArray alloc] init];
    for (UIView *rule in _rules)
        [rule removeFromSuperview];
    [_rules removeAllObjects];
    CGFloat line = 1.0f / [[UIScreen mainScreen] scale];
    void (^rule)(CGRect) = ^(CGRect frame) {
        UIView *view = [[[UIView alloc] initWithFrame:frame] autorelease];
        view.backgroundColor = [YZTheme separatorColor];
        [_card addSubview:view];
        [_rules addObject:view];
    };
    rule(CGRectMake(0, y, width, line));
    if ([_buttons count] == 2) {
        CGFloat half = floor(width / 2);
        [[_buttons objectAtIndex:0] setFrame:CGRectMake(0, y, half, 44)];
        [[_buttons objectAtIndex:1] setFrame:CGRectMake(half, y, width - half, 44)];
        rule(CGRectMake(half, y, line, 44));
        y += 44;
    } else {
        for (NSInteger i = (NSInteger)[_buttons count] - 1; i >= 0; --i) {
            [[_buttons objectAtIndex:i] setFrame:CGRectMake(0, y, width, 44)];
            y += 44;
            if (i)
                rule(CGRectMake(0, y, width, line));
        }
    }
    CGFloat room = self.bounds.size.height - _keyboardHeight;
    CGAffineTransform held = _card.transform;
    _card.transform = CGAffineTransformIdentity;
    _card.frame = CGRectMake(floor((self.bounds.size.width - width) / 2), MAX((CGFloat)8, floor((room - y) / 2)), width, y);
    _card.layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:_card.bounds cornerRadius:13].CGPath;
    _card.transform = held;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    // a layout while the card leaves would fit it again under its own fade
    if (_leaving)
        return;
    if ([YZTheme isFlat]) {
        [self layoutFlat];
        return;
    }
    CGFloat width = MIN(self.bounds.size.width - 2 * YZDialogSide, YZDialogMaxWidth);
    CGFloat inner = MAX((CGFloat)0, width - 2 * YZDialogPad);
    _title.text = _titleText;
    _message.text = _messageText;
    CGSize title = [_title sizeThatFits:CGSizeMake(inner, 400)];
    CGSize message = [_messageText length] ? [_message sizeThatFits:CGSizeMake(inner, 400)] : CGSizeZero;
    CGFloat y = YZDialogPad;
    if (_pictureView) {
        CGFloat side = 112;
        _pictureView.frame = CGRectMake(floor((width - side) / 2), y, side, side);
        y += side + 12;
    }
    _title.frame = CGRectMake(YZDialogPad, y, inner, ceil(title.height));
    y += ceil(title.height);
    if ([_messageText length]) {
        y += 10;
        _message.frame = CGRectMake(YZDialogPad, y, inner, ceil(message.height));
        y += ceil(message.height);
    } else
        _message.frame = CGRectZero;
    for (UITextField *field in _fields) {
        y += 12;
        field.frame = CGRectMake(YZDialogPad, y, inner, YZDialogFieldHeight);
        y += YZDialogFieldHeight;
    }
    y += 22;
    if ([_buttons count] == 2 && inner >= 200) {
        CGFloat half = floor((inner - 10) / 2);
        [[_buttons objectAtIndex:0] setFrame:CGRectMake(YZDialogPad, y, half, YZDialogButtonHeight)];
        [[_buttons objectAtIndex:1] setFrame:CGRectMake(YZDialogPad + half + 10, y, half, YZDialogButtonHeight)];
        y += YZDialogButtonHeight;
    } else {
        // the confirm button goes first when they stack
        for (NSInteger i = (NSInteger)[_buttons count] - 1; i >= 0; --i) {
            [[_buttons objectAtIndex:i] setFrame:CGRectMake(YZDialogPad, y, inner, YZDialogButtonHeight)];
            y += YZDialogButtonHeight + (i ? 8 : 0);
        }
    }
    y += YZDialogPad;
    CGFloat room = self.bounds.size.height - _keyboardHeight;
    CGAffineTransform held = _card.transform;
    _card.transform = CGAffineTransformIdentity;
    _card.frame = CGRectMake(floor((self.bounds.size.width - width) / 2), MAX((CGFloat)8, floor((room - y) / 2)), width, y);
    _card.layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:_card.bounds cornerRadius:[YZTheme sheetRadius]].CGPath;
    _card.transform = held;
}

- (NSArray *)fieldTexts {
    NSMutableArray *texts = [NSMutableArray array];
    for (UITextField *field in _fields)
        [texts addObject:field.text ? field.text : @""];
    return texts;
}

- (void)buttonPressed:(UIButton *)button {
    if (!button || _leaving)
        return;
    NSInteger index = button.tag;
    NSArray *texts = [self fieldTexts];
    // the answer is looked up when the card has left, not now: an owner that goes away meanwhile calls
    // dismissSilently, which drops the handler, and its block must not run against a freed owner
    [self leave:^{
        void (^handler)(NSInteger, NSArray *) = [_handler retain];
        if (handler)
            handler(index, texts);
        [handler release];
    }];
}

// ios 5 to 7 ask with the alert of the system. UIAlertView is called by name and selector: the sdk that builds the arm64 slice
// marks it deprecated, and no later system runs this path
- (void)showSystemAlert {
    Class alertClass = NSClassFromString(@"UIAlertView");
    SEL initializer = NSSelectorFromString(@"initWithTitle:message:delegate:cancelButtonTitle:otherButtonTitles:");
    // the only button of a card with one is its cancel button, index 0, as the answer of the custom card has it
    NSString *cancel = _cancelTitle ? _cancelTitle : _confirmTitle;
    id alert = ((id (*)(id, SEL, NSString *, NSString *, id, NSString *, NSString *))objc_msgSend)([alertClass alloc], initializer, _titleText, _messageText, self, cancel, nil);
    if (_cancelTitle)
        ((NSInteger (*)(id, SEL, NSString *))objc_msgSend)(alert, NSSelectorFromString(@"addButtonWithTitle:"), _confirmTitle);
    NSUInteger fields = [_placeholders count];
    if (fields) {
        // alert views take one field, or a name with a password. the field at secureIndex hides its text
        NSInteger style = fields == 1 ? (_secureIndex == 0 ? 2 : 1) : 3;
        ((void (*)(id, SEL, NSInteger))objc_msgSend)(alert, NSSelectorFromString(@"setAlertViewStyle:"), style);
        for (NSUInteger i = 0; i < fields && i < 2; ++i) {
            UITextField *field = ((UITextField *(*)(id, SEL, NSInteger))objc_msgSend)(alert, NSSelectorFromString(@"textFieldAtIndex:"), (NSInteger)i);
            field.placeholder = [_placeholders objectAtIndex:i];
            if (i < [_initialTexts count])
                field.text = [_initialTexts objectAtIndex:i];
            field.autocapitalizationType = UITextAutocapitalizationTypeNone;
            field.autocorrectionType = UITextAutocorrectionTypeNo;
            field.secureTextEntry = (NSInteger)i == _secureIndex;
        }
    }
    _alert = alert;
    // the alert does not retain its delegate and the answer comes after the owner may have let go of the dialog
    [self retain];
    _shown = YES;
    ((void (*)(id, SEL))objc_msgSend)(alert, NSSelectorFromString(@"show"));
}

- (void)alertView:(id)alert clickedButtonAtIndex:(NSInteger)index {
    _answerIndex = index;
    NSMutableArray *texts = [NSMutableArray array];
    NSUInteger fields = [_placeholders count];
    for (NSUInteger i = 0; i < fields && i < 2; ++i) {
        UITextField *field = ((UITextField *(*)(id, SEL, NSInteger))objc_msgSend)(alert, NSSelectorFromString(@"textFieldAtIndex:"), (NSInteger)i);
        [texts addObject:field.text ? field.text : @""];
    }
    [_answerTexts release];
    _answerTexts = [texts retain];
}

// the answer is looked up when the alert has left, as the card does it: an owner that went away meanwhile called
// dismissSilently, which dropped the handler
- (void)alertView:(id)alert didDismissWithButtonIndex:(NSInteger)index {
    (void)index;
    ((void (*)(id, SEL, id))objc_msgSend)(alert, NSSelectorFromString(@"setDelegate:"), nil);
    NSInteger answer = _answerIndex;
    NSArray *texts = [[_answerTexts retain] autorelease];
    [_answerTexts release];
    _answerTexts = nil;
    [_alert release];
    _alert = nil;
    _shown = NO;
    void (^handler)(NSInteger, NSArray *) = [_handler retain];
    if (handler && texts)
        handler(answer, texts);
    [handler release];
    [self release];
}

- (void)showInView:(UIView *)host {
    if (_shown)
        return;
    if ([YZTheme isClassic] && NSClassFromString(@"UIAlertView") && !_pictureView) {
        [self showSystemAlert];
        return;
    }
    _shown = YES;
    self.frame = host.bounds;
    [host addSubview:self];
    [self layoutIfNeeded];
    CGAffineTransform from = CGAffineTransformConcat(CGAffineTransformMakeScale(0.86f, 0.86f), CGAffineTransformMakeTranslation(0, -16));
    self.alpha = 0;
    YZMotionAnimateView(self, CGAffineTransformIdentity, 1, YOZORA_MOTION_ENTER, 0, nil);
    YZMotionAnimateEnter(_card, from, 0, YOZORA_MOTION_WOBBLE, 0);
    if ([_fields count])
        [[_fields objectAtIndex:0] becomeFirstResponder];
}

- (void)leave:(void (^)(void))after {
    if (!_shown || _leaving)
        return;
    _leaving = YES;
    self.userInteractionEnabled = NO;
    [self endEditing:YES];
    [self retain];
    void (^finish)(void) = [after copy];
    CGAffineTransform away = CGAffineTransformConcat(CGAffineTransformMakeScale(0.92f, 0.92f), CGAffineTransformMakeTranslation(0, -12));
    YZMotionAnimateView(_card, away, 0, YOZORA_MOTION_DISMISS, 0, nil);
    YZMotionAnimateView(self, CGAffineTransformIdentity, 0, YOZORA_MOTION_DISMISS, 0, ^(BOOL finished) {
        (void)finished;
        [self removeFromSuperview];
        _shown = NO;
        if (finish)
            finish();
        [finish release];
        [self autorelease];
    });
}

- (void)dismissSilently {
    // the handler belongs to an owner that may be gone by the time a card on its way out finishes
    [_handler release];
    _handler = nil;
    if (_alert) {
        ((void (*)(id, SEL, NSInteger, BOOL))objc_msgSend)(_alert, NSSelectorFromString(@"dismissWithClickedButtonIndex:animated:"), 0, NO);
        return;
    }
    [self leave:nil];
}

@end
