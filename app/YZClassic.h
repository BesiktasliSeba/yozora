#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import "YZTheme.h"

/* the classic design: surfaces lit from above, a hard edged gloss over the upper half, dark outlines and stamped
   text, as the system drew them on ios 5 and 6. everything is drawn with CAGradientLayer and Core Graphics, so no
   picture ships and the colors follow the accent and the theme. the modern design never calls into this file */

@interface YZClassic : NSObject
/* the bar colors come from the accent: lit at the top, darker at the bottom. the dark themes take a deeper
   accent, so the bar keeps white ink on it */
+ (UIColor *)barTopColor;
+ (UIColor *)barMidColor;
+ (UIColor *)barBottomColor;
/* what stands on a bar: white, with a dark stamp under it */
+ (UIColor *)inkColor;
+ (UIColor *)subtleInkColor;
+ (UIColor *)stampColor;
/* the gloss of a key or a bar: white over the upper half */
+ (UIColor *)glossTopColor;
+ (UIColor *)glossBottomColor;
/* a stretchable bar picture for the system navigation bar, lit and glossy like the bars of the app */
+ (UIImage *)barImage;
/* a glyph with the stamp under it, white on a bar */
+ (UIImage *)stampedImage:(UIImage *)glyph;
/* a label that has a dark stamp under its text */
+ (void)stampLabel:(UILabel *)label;
/* a stretchable key picture for bar buttons and plain keys: color lit from above and darker below, a hard gloss over the upper
   half, a dark outline. radius is the corner. pressed is the darker one */
+ (UIImage *)keyImageWithColor:(UIColor *)color radius:(CGFloat)radius pressed:(BOOL)pressed;
/* a button that is a glossy key in the classic design and a flat rounded fill of `color` in the modern one (modernRadius is its corner there).
   the title is white with a stamp on a key */
+ (void)styleKey:(UIButton *)button color:(UIColor *)color radius:(CGFloat)radius modernRadius:(CGFloat)modernRadius;
/* the round tonal button of a glyph in the modern design (the accent glyph on a tint of it), a glossy key with a white stamped glyph in the classic one */
+ (void)styleGlyphKey:(UIButton *)button icon:(YZIcon)icon size:(CGFloat)size modernRadius:(CGFloat)modernRadius;
/* a stretchable white well with a shadow along its upper edge, for fields that are set into a bar */
+ (UIImage *)wellImageWithRadius:(CGFloat)radius;
@end

/* a view whose layer is a gradient, which follows its bounds and carries its own corners, outline and shadow */
@interface YZGradientView : UIView
- (void)setTop:(UIColor *)top bottom:(UIColor *)bottom;
@end

/* a bar: the gradient of the accent, a gloss that ends in a hard edge at the middle, a light line under the top edge
   and a dark one along the bottom (or the top for a bar that stands at the foot of the screen) */
@interface YZGlossBarView : YZGradientView {
    CAGradientLayer *_gloss;
    CALayer *_light;
    CALayer *_dark;
    BOOL _darkLineAtTop;
}
@property(nonatomic, assign) BOOL darkLineAtTop;
/* takes the colors of the theme and the accent again */
- (void)applyTheme;
@end

/* a plate for cards, menus, sheets and dialogs: a pale or charcoal gradient with a lit rim, an outline and a
   shadow. in the modern design it is the flat card color */
@interface YZPlateView : YZGradientView {
    BOOL _surface;
    BOOL _sheetCorners;
}
/* the modern design fills a plate with the card color and the corners of a card; these ask for the surface color and the larger corners of a sheet
   (a dialog is one). the classic plate is the same either way */
@property(nonatomic, assign) BOOL surfaceInModern;
@property(nonatomic, assign) BOOL sheetCornersInModern;
- (void)applyTheme;
@end
