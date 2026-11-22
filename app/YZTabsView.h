#import <UIKit/UIKit.h>
#import "YZTabPager.h"

@class CAGradientLayer;

@protocol YZTabsViewDelegate;

/* the tab tray: a switch between the normal tabs and the private ones, one card per tab, a button for a new tab and a done button */
@interface YZTabsView : UIView <YZTabPagerDelegate> {
    id<YZTabsViewDelegate> _delegate;
    UIScrollView *_scroll;
    // the other way to show the tabs: pages paged sideways, with the toolbar of the system under them in the classic design
    YZTabPager *_pager;
    UIToolbar *_toolbar;
    UIBarButtonItem *_toolbarDone;
    // the modern design's bar under the pages: a new tab on the left, done on the right
    UIView *_pagesBar;
    UIView *_pagesBarLine;
    UIButton *_pagesDone;
    CAGradientLayer *_ground;
    UIView *_header;
    UIControl *_modes;
    UIButton *_newTab;
    UIButton *_done;
    NSArray *_tabs;
    NSArray *_cards;
    NSUInteger _selected;
    UIEdgeInsets _edgeInsets;
    BOOL _incognito;
    // a closed card moves the remaining cards into its place
    BOOL _animateNextLayout;
}
@property(nonatomic, assign) id<YZTabsViewDelegate> delegate;
/* the status bar, notch and home indicator the view must keep clear of */
@property(nonatomic, assign) UIEdgeInsets edgeInsets;
/* the tray shows the private tabs: the switch marks them, the colors follow the theme */
@property(nonatomic, assign) BOOL incognito;
/* where the switch between the normal and the private tabs is, in the coordinates of the view, for an animation that opens from it */
- (CGPoint)modeSwitchCenterInView:(UIView *)view;
- (void)showTabs:(NSArray *)tabs selected:(NSUInteger)selected;
/* visible cards rise into the tray, offscreen cards do not animate */
- (void)present;
/* the colors and the texts of the chosen theme and language */
- (void)applyTheme;
/* releases images the tab model discarded after a memory warning */
- (void)refreshThumbnails;
@end

@protocol YZTabsViewDelegate <NSObject>
- (void)tabsView:(YZTabsView *)view didSelectIndex:(NSUInteger)index;
- (void)tabsView:(YZTabsView *)view didCloseIndex:(NSUInteger)index;
- (void)tabsViewDidRequestNewTab:(YZTabsView *)view;
- (void)tabsViewDidFinish:(YZTabsView *)view;
@optional
/* the switch was moved: the tray shows the private tabs or the normal ones, the delegate swaps the tab list and calls showTabs again */
- (void)tabsView:(YZTabsView *)view didChooseIncognito:(BOOL)incognito;
@end
