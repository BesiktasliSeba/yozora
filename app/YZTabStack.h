#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>

@protocol YZTabStackDelegate;

/* the tab tray of Safari on ios 7: the pages hang one under another in a stack that tilts back toward the top, each with its
   close cross and its title over a picture of the page. a page opens by growing to the screen, a swipe to the side or the cross
   closes it and the pages below rise into its place */
@interface YZTabStack : UIView <UIScrollViewDelegate, UIGestureRecognizerDelegate> {
    id<YZTabStackDelegate> _delegate;
    UIScrollView *_scroll;
    NSMutableArray *_cards;
    NSArray *_tabs;
    NSUInteger _selected;
    BOOL _closing;
    // the page under the finger: the layout leaves it alone while it is dragged and while it settles back
    id _panned;
    // the pages draw apart a little while the stack moves and settle when it rests
    CGFloat _spread;
    CADisplayLink *_relax;
    CFTimeInterval _lastScrollTime;
    CGFloat _lastScrollOffset;
}
@property(nonatomic, assign) id<YZTabStackDelegate> delegate;
- (void)showTabs:(NSArray *)tabs selected:(NSUInteger)selected;
/* the pages fall into the stack from the place of the open one */
- (void)present;
/* the open page stands up and grows to the screen, as when it is tapped, and then the delegate is told it was chosen: the way out of the tray
   for Done, so the tray does not fade over a ground that is not the page */
- (void)openSelected;
/* a new last page was added to the list the stack shows: it comes in at the foot on its own wobble and then opens to the browser, so the tray leaves
   through the page and not as a fade over the ground */
- (void)presentNewPage;
- (void)applyTheme;
- (void)refreshThumbnails;
/* the ground behind the stack, dark whatever the theme */
+ (UIColor *)groundColor;
@end

@protocol YZTabStackDelegate <NSObject>
- (void)tabStack:(YZTabStack *)stack didSelectIndex:(NSUInteger)index;
- (void)tabStack:(YZTabStack *)stack didCloseIndex:(NSUInteger)index;
@optional
/* a sideways swipe on the ground of the stack, off every page (a swipe on a page closes it): toward is -1 for a finger moving left and 1 for right */
- (void)tabStack:(YZTabStack *)stack didSwipeToward:(NSInteger)direction;
/* where the page of the browser stands, in the coordinates of the stack: an opened page lands on it, so the bars around it are the real
   ones and not the stack's. CGRectNull, or no answer, grows the page over the whole stack */
- (CGRect)tabStackPageFrame:(YZTabStack *)stack;
/* called inside the animation of an opening page, for what has to change with it: the bar of the stack gives way to the bars of the browser */
- (void)tabStackWillOpenPage:(YZTabStack *)stack;
@end
