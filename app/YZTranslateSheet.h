#import <UIKit/UIKit.h>
#import "YZSheets.h"
#import "YZTranslation.h"

@class YZToggle;

/* the translation language sheet: page language (found or chosen) and target, each opening a list, a button
   that translates or restores the original, and the page language's choices to always translate or never
   offer. the object keeps itself alive until the sheet answered */
@interface YZTranslateSheet : NSObject {
    YZSheet *_sheet;
    YZTranslation *_translation;
    UIView *_host;
    CGFloat _bottomInset;
    NSString *_source;
    NSString *_target;
    UIView *_content;
    UIControl *_sourceField;
    UIControl *_targetField;
    UILabel *_sourceName;
    UILabel *_targetName;
    UIButton *_swap;
    UIButton *_primary;
    UILabel *_alwaysLabel;
    UILabel *_neverLabel;
    YZToggle *_alwaysToggle;
    YZToggle *_neverToggle;
    void (^_completion)(NSString *source, NSString *target, BOOL showOriginal);
    YZSheet *_list;
    BOOL _done;
}
/* source is nil for the language the service finds, showOriginal is YES when the person restored the
   original. the block does not run when the sheet is dismissed without a choice */
+ (YZTranslateSheet *)presentInView:(UIView *)host bottomInset:(CGFloat)bottomInset translation:(YZTranslation *)translation
                         completion:(void (^)(NSString *source, NSString *target, BOOL showOriginal))completion;
/* the page went away: the sheet closes with no answer */
- (void)dismiss;
@end
