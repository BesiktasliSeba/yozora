#import <UIKit/UIKit.h>
#import "YZSettings.h"

/* what a mascot looks like where the app shows it: the start page, an error and a sticker, paged sideways under the list that
   picks the mascot. the modern design stands each picture on a card with a glow of the accent and lets the pages in front
   grow while the others shrink, the classic one shows plain pictures with their captions and the dots of the system */
@interface YZMascotPreview : UIView <UIScrollViewDelegate> {
    UIScrollView *_scroll;
    UIPageControl *_dots;
    NSMutableArray *_plates;
    NSMutableArray *_pictures;
    NSMutableArray *_captions;
    NSMutableArray *_glows;
    CGFloat _builtWidth;
    YZMascot _mascot;
}
- (id)initWithMascot:(YZMascot)mascot;
/* the pictures of the other mascot take the place of these, with a cross fade */
- (void)showMascot:(YZMascot)mascot;
/* the colors of the chosen theme and design. the pictures are not touched, only showMascot: changes them */
- (void)applyTheme;
/* the height the preview wants as the footer of a table */
+ (CGFloat)preferredHeight;
@end
