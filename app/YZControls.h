#import <UIKit/UIKit.h>

/* a button with a material 3 state layer: an accent pill grows while pressed, sized and centered, or the
   bounds inset by pillInsets when the size is zero */
@interface YZStateButton : UIButton {
    UIView *_pill;
    CGSize _pillSize;
    UIEdgeInsets _pillInsets;
}
@property(nonatomic, assign) CGSize pillSize;
@property(nonatomic, assign) UIEdgeInsets pillInsets;
/* the pill takes the colors of the chosen theme */
- (void)applyTheme;
@end

/* a scroll view that scrolls when a drag starts on a button. UIScrollView keeps the touch for a UIControl for
   good, so a list of full width button rows never scrolled */
@interface YZScrollView : UIScrollView
@end

/* two segments in one pill, the chosen one is marked by a pill that wobbles over to it. the segments are a title and a glyph.
   the classic design shows the segmented control of the system instead and uses the titles only */
@interface YZSegmentedPill : UIControl {
    NSArray *_titles;
    NSArray *_icons;
    NSMutableArray *_buttons;
    UIView *_indicator;
    UISegmentedControl *_system;
    NSInteger _selected;
}
@property(nonatomic, assign) NSInteger selectedSegment;
- (id)initWithTitles:(NSArray *)titles icons:(NSArray *)icons;
/* the colors and texts of the chosen theme and language */
- (void)applyTheme;
- (void)setTitles:(NSArray *)titles;
/* the segment is chosen without the control event, the indicator moves with a wobble when animated */
- (void)setSelectedSegment:(NSInteger)index animated:(BOOL)animated;
@end

/* material 3 switch: a small thumb left when off, large right when on, travelling on the wobble curve */
@interface YZToggle : UIControl {
    UIView *_track;
    UIView *_thumb;
    // the classic design shows the switch of the system and the two views above stay hidden
    UISwitch *_system;
    BOOL _on;
}
@property(nonatomic, assign, getter=isOn) BOOL on;
/* the size the switch has in the chosen design: the system switch of ios 5 to 7 in the classic one */
+ (CGSize)controlSize;
- (void)setOn:(BOOL)on animated:(BOOL)animated;
/* the colors of the chosen theme */
- (void)applyTheme;
@end

/* the back arrow and the title share one row of the navigation bar, these screens have no bar title and no
   large title */
@interface YZBarTitleView : UIView {
    YZStateButton *_arrow;
    UILabel *_label;
    NSString *_title;
    NSString *_appliedKey;
}
@property(nonatomic, readonly) NSString *title;
- (id)initWithTitle:(NSString *)title width:(CGFloat)width target:(id)target action:(SEL)action;
/* the colors and the text size of the chosen theme */
- (void)applyTheme;
/* the same, but nothing happens when the text color and the text scale are the ones it was last made with */
- (void)applyThemeIfChanged;
@end

/* the views behind the screens take the theme ground, they show while a screen slides and are black otherwise */
void YZPaintBehindScreens(UINavigationController *navigation);

/* puts the title in the left item, made again only when the title or the bar width changed. call it where the
   bar has its size */
void YZInstallBarTitle(UIViewController *controller, NSString *title, id target, SEL action);

/* what stands behind a bar over scrolling content: the ground at the top, a blur once content has gone by. it
   lives in the scroll view so it moves with the screen during push and pop */
@interface YZBarFill : UIView {
    UIView *_solid;
    UIView *_blur;
    BOOL _blurred;
}
- (void)applyTheme;
/* places the fill over the top of the scroll view under the bar of the controller and picks its look from the offset */
- (void)attachToScrollView:(UIScrollView *)scroll controller:(UIViewController *)controller;
/* a screen that does not scroll keeps the solid ground */
- (void)placeInView:(UIView *)view controller:(UIViewController *)controller;
@end
