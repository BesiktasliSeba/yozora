#import <UIKit/UIKit.h>

@protocol YZTabPagerDelegate;

/* the pages view of the tab tray: one page of each tab, paged sideways with its neighbours showing at the edges, the title and
   the address of the page in front above it, a close badge on its corner and the dots of the system (UIPageControl) under it, as
   the page switcher of Safari. the classic design draws it the way ios 5 and 6 did, the modern one with flat cards */
@interface YZTabPager : UIView <UIScrollViewDelegate> {
    id<YZTabPagerDelegate> _delegate;
    UILabel *_title;
    UILabel *_address;
    UIScrollView *_scroll;
    UIPageControl *_dots;
    NSArray *_tabs;
    NSMutableArray *_pages;
    NSUInteger _selected;
    NSInteger _current;
    // the page after the last tab: scrolling to it and letting go opens a new tab
    UIView *_plus;
    UIImageView *_plusIcon;
    UILabel *_plusCaption;
    BOOL _requested;
}
@property(nonatomic, assign) id<YZTabPagerDelegate> delegate;
/* the pages of the tabs. the page in front stays where it was when the tab list changes, a first list starts at `selected` */
- (void)showTabs:(NSArray *)tabs selected:(NSUInteger)selected;
/* the page of the tab that is open comes to the front, for the moment the tray opens */
- (void)focusSelected;
/* the colors of the chosen theme and design */
- (void)applyTheme;
/* the pictures the tab model discarded after a memory warning are taken again */
- (void)refreshThumbnails;
@end

@protocol YZTabPagerDelegate <NSObject>
- (void)tabPager:(YZTabPager *)pager didSelectIndex:(NSUInteger)index;
- (void)tabPager:(YZTabPager *)pager didCloseIndex:(NSUInteger)index;
/* the person paged past the last tab, or tapped the page that stands there */
- (void)tabPagerDidRequestNewTab:(YZTabPager *)pager;
@end
