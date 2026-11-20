#import <UIKit/UIKit.h>

/* the sheet of a color field: a card with hex digits and hue, saturation, value tracks, math in
   core/yozora_color.c */
@interface YZColorPicker : UIControl {
    float _hue;
    float _saturation;
    float _value;
    UIView *_card;
    UILabel *_hex;
    NSArray *_tracks;
}
/* "#RRGGBB" */
@property(nonatomic, copy) NSString *hex;
/* the height the picker wants */
+ (CGFloat)preferredHeight;
@end
