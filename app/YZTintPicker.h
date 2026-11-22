#import <UIKit/UIKit.h>
#import "YZTintControl.h"

/* the accent picker of the classic design, as in exteria: a square of colors (the hue runs left to right and the
   saturation from the bottom up, over the value of the light accent of the palette) with a disc of metal for a thumb,
   and a disc of the same metal beside the hex digits under the square */
@interface YZTintPicker : UIControl <YZTintControl, UITextFieldDelegate> {
    float _hue;
    float _saturation;
    float _brightness;
    UIView *_ground;
    UIImageView *_thumb;
    UIImageView *_swatch;
    UIView *_plate;
    UILabel *_hash;
    UITextField *_field;
    CGSize _renderedSize;
    BOOL _morphing;
}
@end
