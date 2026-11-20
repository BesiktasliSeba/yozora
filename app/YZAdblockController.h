#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#import "YZControls.h"

/* rules in adblock plus syntax (limits in core/yozora_filter.h), applied on leaving or on the check mark */
@interface YZAdblockController : UIViewController <UITextViewDelegate> {
    UIView *_card;
    UILabel *_hint;
    UITextView *_text;
    UILabel *_status;
    UIButton *_apply;
    YZBarFill *_fill;
    CGFloat _keyboardHeight;
    BOOL _edited;
}
@end
