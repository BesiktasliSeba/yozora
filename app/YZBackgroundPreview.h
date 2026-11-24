#import <UIKit/UIKit.h>

@class YZHomeView;

/* what the start page looks like with the chosen photo behind it, at the size of the screen: the bars of the browser (the strip under the status line,
   the address bar, the bottom bar) around the real start page (`YZHomeView`) fed with the real sites and bookmarks, so the tiles carry the names of
   the person's own pages. it is laid out like the browser (bar heights, field, keys of the chosen design) in the colors and corners of the chosen theme */
@interface YZBackgroundPreview : UIView {
    YZHomeView *_home;
    UIView *_topBar;
    UIView *_strip;
    UIView *_topLine;
    UIView *_field;
    UIImageView *_lock;
    UILabel *_address;
    UIView *_bottomBar;
    UIView *_bottomLine;
    NSMutableArray *_toolIcons;
    UIView *_hint;
    UIImageView *_hintIcon;
    UILabel *_hintText;
    UIEdgeInsets _insets;
    BOOL _hasPicture;
    void (^_tapped)(void);
}
/* runs when the page is touched while it has no photo, the screen uses it to open the photo library */
@property(nonatomic, copy) void (^tapped)(void);
/* the room the status line and the home indicator take, the bars grow by it as they do in the browser */
@property(nonatomic, assign) UIEdgeInsets insets;
/* the left end of the address bar, which the address field leaves free for a key the screen puts there, in the view's coordinates */
- (CGRect)backKeyFrame;
/* where the page stands between the bars: the foot of the address bar and the top of the bottom bar, in the view's coordinates */
- (CGFloat)pageTop;
- (CGFloat)pageBottom;
/* the sites and bookmarks the start page shows, YZRecord arrays as the start page takes them */
- (void)showSites:(NSArray *)sites bookmarks:(NSArray *)bookmarks;
/* the colors of the chosen theme and design, the mascot and the texts of the language */
- (void)restyle;
/* the photo as it is shown (blurred already, nil for none). dissolve fades the old photo into the new one */
- (void)showPicture:(UIImage *)picture dissolve:(BOOL)dissolve;
@end
