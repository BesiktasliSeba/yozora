#import "YZDialog.h"
#import "YZCompat.h"
#import "YZMotion.h"
#import "YZTheme.h"
#import <QuartzCore/QuartzCore.h>

static const CGFloat YZDialogMaxWidth = 320;
static const CGFloat YZDialogSide = 24;
static const CGFloat YZDialogPad = 24;
static const CGFloat YZDialogButtonHeight = 44;
static const CGFloat YZDialogFieldHeight = 44;

@implementation YZDialog
@synthesize messageText = _messageText;
@synthesize isShown = _shown;

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
    // the card moves up when the keyboard would cover its fields
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(keyboardChanged:) name:UIKeyboardWillShowNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(keyboardChanged:) name:UIKeyboardWillHideNotification object:nil];
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_card release];
    [_title release];
    [_message release];
    [_fields release];
    [_buttons release];
    [_titleText release];
    [_messageText release];
    [_placeholders release];
    [_cancelTitle release];
    [_confirmTitle release];
    [_handler release];
    [super dealloc];
}

- (UIButton *)makeButton:(NSString *)title filled:(UIColor *)fill text:(UIColor *)text index:(NSInteger)index {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeCustom];
    button.titleLabel.font = [YZTheme boldSystemFontOfSize:16];
    button.backgroundColor = fill;
    button.layer.cornerRadius = YZDialogButtonHeight / 2;
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
        field.delegate = self;
        field.leftView = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, 14, 10)] autorelease];
        field.leftViewMode = UITextFieldViewModeAlways;
        YZSetPlaceholder(field, [_placeholders objectAtIndex:i], [YZTheme subtleTextColor]);
        [_card addSubview:field];
        [_fields addObject:field];
    }
    [self setNeedsLayout];
}

- (void)setMessageText:(NSString *)text {
    if (text == _messageText || [text isEqualToString:_messageText])
        return;
    [text retain];
    [_messageText release];
    _messageText = text;
    [self setNeedsLayout];
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

- (void)layoutSubviews {
    [super layoutSubviews];
    // a layout while the card leaves would fit it again under its own fade
    if (_leaving)
        return;
    CGFloat width = MIN(self.bounds.size.width - 2 * YZDialogSide, YZDialogMaxWidth);
    CGFloat inner = MAX((CGFloat)0, width - 2 * YZDialogPad);
    _title.text = _titleText;
    _message.text = _messageText;
    CGSize title = [_title sizeThatFits:CGSizeMake(inner, 400)];
    CGSize message = [_messageText length] ? [_message sizeThatFits:CGSizeMake(inner, 400)] : CGSizeZero;
    CGFloat y = YZDialogPad;
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

- (void)showInView:(UIView *)host {
    if (_shown)
        return;
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
    [self leave:nil];
}

@end
