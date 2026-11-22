#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import "YZTheme.h"

/* the classic design: the bars, switches, segmented controls and fields of ios 5 to 7, drawn by the system itself
   (UIToolbar, UISwitch, UISegmentedControl, the rounded text field), tinted with the accent. it exists on those
   systems only, the modern design never calls into this file */

@interface YZClassic : NSObject
/* the tint of the bars and keys comes from the accent. the dark themes take a deeper accent, so white ink keeps its
   contrast on it */
+ (UIColor *)barMidColor;
/* what stands on a bar: white, with a dark stamp under it */
+ (UIColor *)inkColor;
+ (UIColor *)subtleInkColor;
+ (UIColor *)stampColor;
/* a glyph with the stamp under it, white on a bar */
+ (UIImage *)stampedImage:(UIImage *)glyph;
/* a label that has a dark stamp under its text */
+ (void)stampLabel:(UILabel *)label;
/* the lit surface of the classic design: a toolbar of the system tinted with `color` lies under the content of `view`, with
   the corners of `radius`. ios 5 and 6 draw its gradient and gloss themselves */
+ (void)lightView:(UIView *)view color:(UIColor *)color radius:(CGFloat)radius;
/* takes the lit surface away, the modern design draws flat fills */
+ (void)unlightView:(UIView *)view;
/* a button that is a lit key in the classic design and a flat rounded fill of `color` in the modern one (modernRadius is its corner there).
   the title is white with a stamp on a key */
+ (void)styleKey:(UIButton *)button color:(UIColor *)color radius:(CGFloat)radius modernRadius:(CGFloat)modernRadius;
/* the round tonal button of a glyph in the modern design (the accent glyph on a tint of it), a glossy key with a white stamped glyph in the classic one */
+ (void)styleGlyphKey:(UIButton *)button icon:(YZIcon)icon size:(CGFloat)size modernRadius:(CGFloat)modernRadius;
@end

/* a view whose layer is a gradient, which follows its bounds and carries its own corners, outline and shadow */
@interface YZGradientView : UIView
- (void)setTop:(UIColor *)top bottom:(UIColor *)bottom;
@end

/* a bar of the app. the classic design lays a system toolbar under whatever stands on it, so the gradient, the gloss and
   the edge lines are the ones of the system. the modern design is the flat toolbar color */
@interface YZSystemBarView : YZGradientView {
    UIToolbar *_system;
}
/* takes the colors of the theme and the accent again */
- (void)applyTheme;
@end

/* the field that is set into a bar. the classic design draws a pill of brushed metal, rounded all the way: pale on the light theme,
   charcoal on the dark ones, a hard edged gloss over its upper half, a dark outline and a light
   line along the foot, all as layers so no picture is stretched. the modern design is the flat fill of the theme */
@interface YZWellView : UIView {
    CAGradientLayer *_body;
    CAGradientLayer *_gloss;
    CALayer *_rim;
}
/* the fill and the border follow the design in use */
- (void)applyTheme;
@end

/* a plate for cards, sheets and popups: the classic design lays a toolbar of the system tinted for the theme under the content (pale
   glass on the light themes, charcoal on the dark ones), with a lit outline and a gloss over its upper half that ends in a hard
   edge. in the modern design it is the flat card color */
@interface YZPlateView : YZGradientView {
    BOOL _surface;
    BOOL _sheetCorners;
    CAGradientLayer *_shine;
    CAShapeLayer *_shineMask;
}
/* the modern design fills a plate with the card color and the corners of a card; these ask for the surface color and the larger corners of a sheet
   (a dialog is one). the classic plate is the same either way */
@property(nonatomic, assign) BOOL surfaceInModern;
@property(nonatomic, assign) BOOL sheetCornersInModern;
- (void)applyTheme;
@end
