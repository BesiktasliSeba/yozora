#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

/* the accent editor: hue, saturation and brightness tracks, hex digits, ready colors. value changed fires
   while editing, touch up inside when done, which is when the app takes the color */
@interface YZAccentEditor : UIControl <UITextFieldDelegate> {
    float _hue;
    float _saturation;
    float _brightness;
    UIView *_card;
    // the card of the private accent is a gradient of the chosen color, not a flat one
    CAGradientLayer *_cardGradient;
    CAShapeLayer *_cookie;
    UILabel *_hash;
    UILabel *_note;
    UITextField *_field;
    UILabel *_hueName;
    UILabel *_saturationName;
    UILabel *_brightnessName;
    UIControl *_hueTrack;
    UIControl *_saturationTrack;
    UIControl *_brightnessTrack;
    UIScrollView *_chips;
    NSMutableArray *_chipButtons;
    NSInteger _chosenChip;
    BOOL _morphing;
}
/* the hue in degrees from 0 to 360, the saturation and the brightness (1 is the light accent of the palette, 0 is black) from 0 to 1 */
@property (nonatomic, assign) float hue;
@property (nonatomic, assign) float saturation;
@property (nonatomic, assign) float brightness;
@property (nonatomic, readonly) CGFloat preferredHeight;
/* the disc changes form and turns like the avatar in exteria, for the private accent */
@property (nonatomic, assign) BOOL morphing;
/* the part the keyboard must not cover while the hex digits are typed, in the coordinates of the editor */
@property (nonatomic, readonly) CGRect editingRect;
/* takes the colors of the theme again, call after the theme changed */
- (void)applyTheme;
@end

// a track of one value: a gradient bar with a round handle that swells while held
@interface YZAccentTrack : UIControl {
    CAGradientLayer *_track;
    CALayer *_handle;
    float _fraction;
    NSString *_spokenName;
}
@property (nonatomic, assign) float fraction;
@property (nonatomic, copy) NSString *spokenName;
- (void)setColors:(NSArray *)colors;
- (void)setHandleColor:(UIColor *)color;
@end
