#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

/* the drawn pieces of the full screen player, as in exteria: a play button that turns into pause on a spring,
   glyphs drawn in code at the screen scale, a speed list that wobbles open */

/* ink that reads on the color: dark on a light one, white on a dark one */
UIColor *YZPlayerInkOn(UIColor *color);
/* the accent of the theme lifted toward white, so it holds against a picture */
UIColor *YZPlayerAccent(void);

/* seconds back or forward with the ten inside the arrow */
UIImage *YZPlayerSkipImage(BOOL forward, CGFloat side, UIColor *color);
/* the speaker with waves, or with a slash when muted */
UIImage *YZPlayerSpeakerImage(BOOL muted, CGFloat side, UIColor *color);
/* the four corners of the picture: filled out (the video fills the screen) or pulled in (it fits whole) */
UIImage *YZPlayerAspectImage(BOOL filled, CGFloat side, UIColor *color);

/* a black gradient fading to nothing from the top or bottom edge, it holds the white controls against any
   picture */
@interface YZScrimView : UIView
- (id)initWithFrame:(CGRect)frame fromTop:(BOOL)fromTop;
@end

/* the big round button: a play triangle that becomes two bars on the spring while its body rounds into a
   circle. busy draws a ring that turns around the glyph while the stream loads */
@interface YZPlayButton : UIControl {
    CALayer *_body;
    CAShapeLayer *_left;
    CAShapeLayer *_right;
    CAShapeLayer *_ring;
    BOOL _playing;
    BOOL _busy;
}
@property (nonatomic, assign, getter=isPlaying) BOOL playing;
@property (nonatomic, assign, getter=isBusy) BOOL busy;
- (void)setPlaying:(BOOL)playing animated:(BOOL)animated;
/* the colors of the chosen theme and accent */
- (void)applyTheme;
@end

/* a column of speeds in a dark rounded card, the chosen one marked by a pill. choose is called with the speed that was tapped */
@interface YZSpeedMenu : UIView {
    NSMutableArray *_rows;
    double _selected;
    void (^_choose)(double rate);
}
+ (NSArray *)rates;
- (id)initWithSelectedRate:(double)rate choose:(void (^)(double rate))choose;
+ (CGSize)menuSize;
@end
