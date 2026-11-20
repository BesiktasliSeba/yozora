#import <UIKit/UIKit.h>
#include "yozora_core.h"

@interface YZErrorPage : UIScrollView {
    UIImageView *_picture;
    UILabel *_heading;
    UILabel *_detail;
    UILabel *_address;
    UILabel *_reason;
    UIButton *_retry;
    yozora_page_error _error;
}
- (id)initWithTarget:(id)target action:(SEL)action;
- (void)showError:(yozora_page_error)error address:(NSString *)address reason:(NSString *)reason;
- (void)applyTheme;
@end
