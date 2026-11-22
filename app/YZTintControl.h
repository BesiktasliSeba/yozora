#import <UIKit/UIKit.h>

/* what the accent screen needs of a color control, so the editor of the modern design and the picker of the classic one
   stand in the same place: UIControlEventTouchUpInside comes once when the person lets go (or finishes the hex digits),
   which is when the app takes the color. the hue is in degrees from 0 to 360, the saturation and the brightness (1 is the
   light accent of the palette, 0 is black) from 0 to 1, setting them moves the control and sends nothing */
@protocol YZTintControl <NSObject>
@property (nonatomic, assign) float hue;
@property (nonatomic, assign) float saturation;
@property (nonatomic, assign) float brightness;
@property (nonatomic, readonly) CGFloat preferredHeight;
/* the part the keyboard must not cover while the hex digits are typed, in the coordinates of the control */
@property (nonatomic, readonly) CGRect editingRect;
/* the disc of the modern editor changes form, the classic picker has none */
@property (nonatomic, assign) BOOL morphing;
/* takes the colors of the theme again, call after the theme changed */
- (void)applyTheme;
@end
