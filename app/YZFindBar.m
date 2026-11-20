#import "YZFindBar.h"
#import "YZCompat.h"
#import "YZControls.h"
#import "YZStrings.h"
#import "YZTheme.h"
#import <QuartzCore/QuartzCore.h>

// typing rests this long before a search, every search marks all matches and a burst of letters would repeat
// that for nothing
static const NSTimeInterval YZFindDelay = 0.25;

@implementation YZFindBar
@synthesize delegate = _delegate;

- (UIButton *)buttonWithIcon:(YZIcon)icon label:(NSString *)label action:(SEL)action {
    YZStateButton *button = [YZStateButton buttonWithType:UIButtonTypeCustom];
    button.pillSize = CGSizeMake(36, 36);
    button.accessibilityLabel = label;
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:button];
    return button;
}

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    _field = [[UIView alloc] initWithFrame:CGRectZero];
    [self addSubview:_field];
    _input = [[UITextField alloc] initWithFrame:CGRectZero];
    _input.delegate = self;
    _input.font = [YZTheme systemFontOfSize:16];
    _input.clearButtonMode = UITextFieldViewModeWhileEditing;
    _input.returnKeyType = UIReturnKeySearch;
    _input.autocorrectionType = UITextAutocorrectionTypeNo;
    _input.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _input.contentVerticalAlignment = UIControlContentVerticalAlignmentCenter;
    [_input addTarget:self action:@selector(textChanged) forControlEvents:UIControlEventEditingChanged];
    [_field addSubview:_input];
    _count = [[UILabel alloc] initWithFrame:CGRectZero];
    _count.font = [YZTheme systemFontOfSize:13];
    _count.backgroundColor = [UIColor clearColor];
    _count.textAlignment = YZTextAlignmentRight;
    [_field addSubview:_count];
    _previous = [[self buttonWithIcon:YZIconArrowUp label:YZT(@"Предыдущее совпадение") action:@selector(previousPressed)] retain];
    _next = [[self buttonWithIcon:YZIconArrowDown label:YZT(@"Следующее совпадение") action:@selector(nextPressed)] retain];
    _close = [[self buttonWithIcon:YZIconClose label:YZT(@"Закрыть поиск") action:@selector(closePressed)] retain];
    [self applyTheme];
    return self;
}

- (void)dealloc {
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    _input.delegate = nil;
    [_field release];
    [_input release];
    [_count release];
    [_previous release];
    [_next release];
    [_close release];
    [_shownText release];
    [super dealloc];
}

- (void)applyTheme {
    self.backgroundColor = [YZTheme toolbarColor];
    _field.backgroundColor = [YZTheme fieldColor];
    _input.textColor = [YZTheme textColor];
    _input.keyboardAppearance = [YZTheme isDark] ? UIKeyboardAppearanceAlert : UIKeyboardAppearanceDefault;
    YZSetPlaceholder(_input, YZT(@"Найти на странице"), [[YZTheme textColor] colorWithAlphaComponent:0.7f]);
    _count.textColor = [YZTheme subtleTextColor];
    [_previous setImage:[YZTheme icon:YZIconArrowUp size:24 color:[YZTheme textColor]] forState:UIControlStateNormal];
    [_next setImage:[YZTheme icon:YZIconArrowDown size:24 color:[YZTheme textColor]] forState:UIControlStateNormal];
    [_close setImage:[YZTheme icon:YZIconClose size:24 color:[YZTheme textColor]] forState:UIControlStateNormal];
}

- (void)layoutInRect:(CGRect)rect topInset:(CGFloat)top barHeight:(CGFloat)bar leftInset:(CGFloat)left rightInset:(CGFloat)right {
    self.frame = rect;
    CGFloat width = rect.size.width, fieldHeight = bar - 16;
    // the buttons sit at the right end, the pill takes the rest
    CGFloat buttons = 3 * 40 + 8;
    _close.frame = CGRectMake(width - right - 44, top + (bar - 40) / 2, 40, 40);
    _next.frame = CGRectMake(CGRectGetMinX(_close.frame) - 40, CGRectGetMinY(_close.frame), 40, 40);
    _previous.frame = CGRectMake(CGRectGetMinX(_next.frame) - 40, CGRectGetMinY(_close.frame), 40, 40);
    _field.frame = CGRectMake(left + 12, top + 8, MAX((CGFloat)0, width - left - right - 12 - buttons - 4), fieldHeight);
    _field.layer.cornerRadius = fieldHeight / 2;
    CGFloat countWidth = 64;
    _count.frame = CGRectMake(_field.bounds.size.width - countWidth - 14, 0, countWidth, fieldHeight);
    _input.frame = CGRectMake(16, 0, MAX((CGFloat)0, _field.bounds.size.width - 16 - countWidth - 18), fieldHeight);
}

- (NSString *)text { return _input.text ? _input.text : @""; }

- (void)showIndex:(int)index count:(int)count searching:(BOOL)searching {
    // called once for each display frame, a change is the only thing worth drawing
    if (_hasShown && index == _shownIndex && count == _shownCount && searching == _shownSearching)
        return;
    _hasShown = YES;
    _shownIndex = index;
    _shownCount = count;
    _shownSearching = searching;
    _count.text = searching ? [NSString stringWithFormat:@"%d/%d", index, count] : @"";
    BOOL has = searching && count > 0;
    _previous.enabled = has;
    _next.enabled = has;
    _previous.alpha = has ? 1 : 0.38f;
    _next.alpha = has ? 1 : 0.38f;
}

- (void)focus { [_input becomeFirstResponder]; }
- (void)blur { [_input resignFirstResponder]; }

- (void)textChanged {
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(runSearch) object:nil];
    if (![_input.text length]) {
        [self runSearch];
        return;
    }
    [self performSelector:@selector(runSearch) withObject:nil afterDelay:YZFindDelay];
}

- (void)runSearch {
    NSString *text = _input.text ? _input.text : @"";
    [_shownText release];
    _shownText = [text copy];
    [_delegate findBar:self didChangeText:text];
}

- (void)previousPressed { [self step:-1]; }
- (void)nextPressed { [self step:1]; }

// a step before the pending search ran would move through the matches of the old text
- (void)step:(int)direction {
    if (![_input.text isEqualToString:_shownText ? _shownText : @""]) {
        [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(runSearch) object:nil];
        [self runSearch];
        return;
    }
    [_delegate findBar:self didStep:direction];
}

- (void)closePressed {
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(runSearch) object:nil];
    [_delegate findBarDidClose:self];
}

- (BOOL)textFieldShouldReturn:(UITextField *)field {
    (void)field;
    [self step:1];
    return NO;
}
@end
