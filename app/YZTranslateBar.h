#import <UIKit/UIKit.h>
#import "YZClassic.h"
#import "YZTranslation.h"
#import "YZWaveSeekBar.h"

@class YZTranslateBar;

@protocol YZTranslateBarDelegate <NSObject>
/* the filled button of the offer */
- (void)translateBarDidPressTranslate:(YZTranslateBar *)bar;
/* a tap on the words: the languages and the choices that belong to them */
- (void)translateBarDidPressOptions:(YZTranslateBar *)bar;
- (void)translateBarDidPressOriginal:(YZTranslateBar *)bar;
- (void)translateBarDidPressRetry:(YZTranslateBar *)bar;
/* the cross: the card goes away, and a translation that is running stops */
- (void)translateBarDidPressClose:(YZTranslateBar *)bar;
@end

/* the translation card at the bottom of the page: offers a language pair, shows a progress wave while texts
   are sent, says the page is translated and gives the original back, or says why it could not. four states on
   one card, they change with a crossfade and a wobble */
@interface YZTranslateBar : YZPlateView {
    UIView *_disc;
    UIImageView *_glyph;
    UILabel *_title;
    UILabel *_subtitle;
    YZWaveSeekBar *_wave;
    UIButton *_primary;
    UIButton *_close;
    UIButton *_body;
    YZTranslationPhase _phase;
    id<YZTranslateBarDelegate> _delegate;
}
@property(nonatomic, assign) id<YZTranslateBarDelegate> delegate;
/* the height the card wants, the width is the host's less the margins */
+ (CGFloat)height;
/* shows what the translation is doing, animated makes a crossfade with a wobble of the glyph when the phase
   changed */
- (void)showTranslation:(YZTranslation *)translation animated:(BOOL)animated;
/* the colors and texts of the chosen theme and language */
- (void)applyTheme;
@end
