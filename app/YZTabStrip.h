#import <UIKit/UIKit.h>

@protocol YZTabStripDelegate;

/* the tab strip above the toolbar on an ipad: a chip with title and close for each tab, the open one on a
   tonal pill, a button for a new tab. it scrolls sideways when chips do not fit and keeps the open one in
   view */
@interface YZTabStrip : UIView {
    id<YZTabStripDelegate> _delegate;
    UIScrollView *_scroll;
    UIButton *_add;
    NSMutableArray *_chips;
    NSUInteger _selected;
    CGFloat _laidWidth;
}
@property(nonatomic, assign) id<YZTabStripDelegate> delegate;
/* the tabs as the controller holds them (YZTab) and the open one */
- (void)showTabs:(NSArray *)tabs selected:(NSUInteger)selected;
/* the colors of the chosen theme */
- (void)applyTheme;
@end

@protocol YZTabStripDelegate <NSObject>
- (void)tabStrip:(YZTabStrip *)strip didSelectIndex:(NSUInteger)index;
- (void)tabStrip:(YZTabStrip *)strip didCloseIndex:(NSUInteger)index;
- (void)tabStripDidRequestNewTab:(YZTabStrip *)strip;
@end
