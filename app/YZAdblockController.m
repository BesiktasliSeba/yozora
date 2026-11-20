#import "YZAdblockController.h"
#import "YZCompat.h"
#import "YZCrash.h"
#import "YZSettings.h"
#import "YZStrings.h"
#import "YZTheme.h"
#include "yozora_core.h"

static const CGFloat YZAdblockStatusHeight = 52;

static UIFont *YZAdblockFont(CGFloat size) {
    UIFont *font = [UIFont fontWithName:@"JetBrainsMono-Regular" size:size];
    return font ? font : [UIFont fontWithName:@"Courier" size:size];
}

@implementation YZAdblockController

- (void)dealloc {
    [YZCrash noteLifecycle:@"dealloc" object:self];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _text.delegate = nil;
    [_card release];
    [_hint release];
    [_text release];
    [_status release];
    [_apply release];
    [_fill removeFromSuperview];
    [_fill release];
    [super dealloc];
}

- (void)loadView {
    UIView *root = [[UIView alloc] initWithFrame:[[UIScreen mainScreen] applicationFrame]];
    self.view = root;
    [root release];
    _card = [[UIView alloc] initWithFrame:CGRectZero];
    _card.layer.cornerRadius = [YZTheme sheetRadius];
    _card.clipsToBounds = YES;
    [self.view addSubview:_card];
    _hint = [[UILabel alloc] initWithFrame:CGRectZero];
    _hint.backgroundColor = [UIColor clearColor];
    _hint.font = [YZTheme systemFontOfSize:12];
    _hint.numberOfLines = 0;
    _hint.lineBreakMode = YZLineBreakWordWrapping;
    _hint.text = YZT(@"По правилу на строку, как в списках Adblock Plus: ||example.com^ блокирует сайт, @@||example.com^ разрешает, example.com##.banner прячет блок. ! начинает комментарий.");
    [_card addSubview:_hint];
    _text = [[UITextView alloc] initWithFrame:CGRectZero];
    _text.backgroundColor = [UIColor clearColor];
    _text.font = YZAdblockFont(12);
    _text.autocorrectionType = UITextAutocorrectionTypeNo;
    _text.autocapitalizationType = UITextAutocapitalizationTypeNone;
    _text.delegate = self;
    [_card addSubview:_text];
    _status = [[UILabel alloc] initWithFrame:CGRectZero];
    _status.backgroundColor = [UIColor clearColor];
    _status.font = [YZTheme systemFontOfSize:12];
    _status.numberOfLines = 2;
    [_card addSubview:_status];
    _apply = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    _apply.frame = CGRectMake(0, 0, 44, 44);
    _apply.layer.cornerRadius = 22;
    _apply.accessibilityLabel = YZT(@"Применить правила");
    [_apply addTarget:self action:@selector(applyPressed) forControlEvents:UIControlEventTouchUpInside];
    [_card addSubview:_apply];
    _text.text = [YZSettings adblockRules];
}

- (void)restyle {
    self.view.backgroundColor = [YZTheme surfaceColor];
    YZPaintBehindScreens(self.navigationController);
    [YZTheme styleTransparentNavigationBar:self.navigationController.navigationBar];
    if (!_fill)
        _fill = [[YZBarFill alloc] initWithFrame:CGRectZero];
    [_fill applyTheme];
    YZInstallBarTitle(self, YZT(@"Правила блокировщика"), self, @selector(back));
    _card.backgroundColor = [YZTheme cardColor];
    _hint.textColor = [YZTheme subtleTextColor];
    _text.textColor = [YZTheme textColor];
    _status.textColor = [YZTheme subtleTextColor];
    _apply.backgroundColor = [YZTheme stateColor];
    [_apply setImage:[YZTheme icon:YZIconCheck size:22 color:[YZTheme accentColor]] forState:UIControlStateNormal];
    [self.view setNeedsLayout];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [YZCrash note:@"screen YZAdblockController"];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(keyboardChanged:) name:UIKeyboardWillShowNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(keyboardChanged:) name:UIKeyboardWillHideNotification object:nil];
    [self restyle];
    [self showStatusForText:nil report:NULL];
}

// the text goes to the core when the screen is left, so a person who only goes back has not lost the rules they typed
- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_text resignFirstResponder];
    if (_edited)
        [self save];
}

- (void)back { [self.navigationController popViewControllerAnimated:YES]; }

- (void)keyboardChanged:(NSNotification *)note {
    BOOL showing = [[note name] isEqualToString:UIKeyboardWillShowNotification];
    CGRect frame = [[[note userInfo] objectForKey:UIKeyboardFrameEndUserInfoKey] CGRectValue];
    CGRect inView = [self.view convertRect:[self.view.window convertRect:frame fromWindow:nil] fromView:nil];
    _keyboardHeight = showing ? MAX((CGFloat)0, CGRectGetMaxY(self.view.bounds) - CGRectGetMinY(inView)) : 0;
    [self.view setNeedsLayout];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGFloat width = self.view.bounds.size.width, height = self.view.bounds.size.height;
    CGFloat top = CGRectGetMaxY(self.navigationController.navigationBar.frame);
    YZInstallBarTitle(self, YZT(@"Правила блокировщика"), self, @selector(back));
    [_fill placeInView:self.view controller:self];
    CGFloat side = 12;
    CGFloat bottom = MAX((CGFloat)12, _keyboardHeight + 6);
    _card.frame = CGRectMake(side, top + 4, width - 2 * side, MAX((CGFloat)0, height - top - 4 - bottom));
    CGFloat cw = _card.bounds.size.width;
    CGFloat hintHeight = ceil([_hint sizeThatFits:CGSizeMake(cw - 32, 10000)].height);
    _hint.frame = CGRectMake(16, 14, cw - 32, hintHeight);
    CGFloat textTop = 14 + hintHeight + 8;
    CGFloat statusTop = MAX(textTop, _card.bounds.size.height - YZAdblockStatusHeight);
    _text.frame = CGRectMake(8, textTop, cw - 16, MAX((CGFloat)0, statusTop - textTop));
    _apply.center = CGPointMake(cw - 16 - 22, statusTop + YZAdblockStatusHeight / 2);
    _status.frame = CGRectMake(16, statusTop, MAX((CGFloat)0, CGRectGetMinX(_apply.frame) - 8 - 16), YZAdblockStatusHeight);
}

- (void)textViewDidChange:(UITextView *)view {
    (void)view;
    _edited = YES;
}

- (void)showStatusForText:(NSString *)error report:(const yozora_adblock_report *)report {
    if (error)
        _status.text = error;
    else if (report)
        _status.text = [NSString stringWithFormat:@"%@: %lu, %@: %lu, %@: %lu%@", YZT(@"правил"), (unsigned long)report->added,
                        YZT(@"не поддерживается"), (unsigned long)report->unsupported, YZT(@"с ошибкой"), (unsigned long)report->invalid,
                        report->dropped ? [NSString stringWithFormat:@", %@: %lu", YZT(@"не влезло"), (unsigned long)report->dropped] : @""];
    else
        _status.text = YZT(@"Правила применяются при выходе с экрана");
}

- (void)save {
    yozora_adblock_report report;
    memset(&report, 0, sizeof report);
    NSString *error = nil;
    if ([YZSettings setAdblockRules:_text.text report:&report error:&error]) {
        _edited = NO;
        [self showStatusForText:nil report:&report];
    } else
        [self showStatusForText:error report:NULL];
}

- (void)applyPressed {
    [_text resignFirstResponder];
    [self save];
}

@end
