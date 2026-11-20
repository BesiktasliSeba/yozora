#import "YZErrorPage.h"
#import "YZCompat.h"
#import "YZStrings.h"
#import "YZTheme.h"
#import <QuartzCore/QuartzCore.h>

@implementation YZErrorPage
- (id)initWithTarget:(id)target action:(SEL)action {
    self = [super initWithFrame:CGRectZero];
    if (!self)
        return nil;
    _picture = [[UIImageView alloc] initWithImage:[UIImage imageNamed:@"yozora-error.png"]];
    _picture.contentMode = UIViewContentModeScaleAspectFit;
    [self addSubview:_picture];
    _heading = [[UILabel alloc] initWithFrame:CGRectZero];
    _detail = [[UILabel alloc] initWithFrame:CGRectZero];
    _address = [[UILabel alloc] initWithFrame:CGRectZero];
    _reason = [[UILabel alloc] initWithFrame:CGRectZero];
    for (UILabel *label in [NSArray arrayWithObjects:_heading, _detail, _address, _reason, nil]) {
        label.backgroundColor = [UIColor clearColor];
        label.textAlignment = YZTextAlignmentCenter;
        label.numberOfLines = 0;
        [self addSubview:label];
    }
    _heading.font = [YZTheme boldSystemFontOfSize:22];
    _detail.font = [YZTheme systemFontOfSize:15];
    _address.font = [YZTheme systemFontOfSize:13];
    _address.numberOfLines = 2;
    _reason.font = [YZTheme systemFontOfSize:12];
    _retry = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    _retry.layer.cornerRadius = 22;
    _retry.titleLabel.font = [YZTheme boldSystemFontOfSize:15];
    [_retry addTarget:target action:action forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:_retry];
    [self applyTheme];
    return self;
}

- (void)showError:(yozora_page_error)error address:(NSString *)address reason:(NSString *)reason {
    _error = error;
    _address.text = address;
    _reason.text = reason;
    [self applyTheme];
    [self setContentOffset:CGPointZero animated:NO];
}

- (void)applyTheme {
    self.backgroundColor = [YZTheme surfaceColor];
    _heading.textColor = [YZTheme textColor];
    _detail.textColor = [YZTheme textColor];
    _address.textColor = [YZTheme subtleTextColor];
    _reason.textColor = [YZTheme subtleTextColor];
    _heading.text = YZT(@"Не удалось открыть страницу");
    switch (_error) {
        case YOZORA_PAGE_ERROR_DNS: _detail.text = YZT(@"Не удалось найти сервер. Проверьте адрес и подключение к интернету."); break;
        case YOZORA_PAGE_ERROR_CONNECTION: _detail.text = YZT(@"Не удалось подключиться к серверу. Проверьте подключение к интернету."); break;
        case YOZORA_PAGE_ERROR_TIMEOUT: _detail.text = YZT(@"Сервер слишком долго не отвечает. Попробуйте ещё раз."); break;
        case YOZORA_PAGE_ERROR_TLS: _detail.text = YZT(@"Не удалось установить защищённое соединение. Проверьте дату и время на устройстве."); break;
        case YOZORA_PAGE_ERROR_INTERRUPTED: _detail.text = YZT(@"Соединение было прервано. Проверьте подключение и попробуйте ещё раз."); break;
        default: _detail.text = YZT(@"Не удалось загрузить страницу. Попробуйте ещё раз."); break;
    }
    _retry.backgroundColor = [YZTheme accentColor];
    [_retry setTitleColor:[YZTheme surfaceColor] forState:UIControlStateNormal];
    [_retry setTitle:YZT(@"Повторить") forState:UIControlStateNormal];
    [self setNeedsLayout];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat width = self.bounds.size.width;
    CGFloat contentWidth = MAX((CGFloat)1, MIN((CGFloat)420, width - 48));
    CGFloat x = (width - contentWidth) / 2;
    CGFloat y = 24;
    _picture.frame = CGRectMake((width - 100) / 2, y, 100, 100);
    y += 112;
    for (UILabel *label in [NSArray arrayWithObjects:_heading, _detail, _address, _reason, nil]) {
        CGSize size = [label sizeThatFits:CGSizeMake(contentWidth, CGFLOAT_MAX)];
        CGFloat height = MIN(size.height, label == _reason ? (CGFloat)60 : (CGFloat)120);
        label.frame = CGRectMake(x, y, contentWidth, height);
        y += height + 12;
    }
    _retry.frame = CGRectMake((width - 160) / 2, y + 4, 160, 44);
    self.contentSize = CGSizeMake(width, y + 72);
}

- (void)dealloc {
    [_picture release];
    [_heading release];
    [_detail release];
    [_address release];
    [_reason release];
    [_retry release];
    [super dealloc];
}
@end
