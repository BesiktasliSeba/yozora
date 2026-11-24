#import <UIKit/UIKit.h>

@protocol YZHomeViewDelegate;

/* the page of a blank tab: the most visited sites and the bookmarks as tiles, or in the private session a note about what it keeps */
@interface YZHomeView : UIScrollView {
    id<YZHomeViewDelegate> _homeDelegate;
    NSArray *_sites;
    NSArray *_bookmarks;
    CGFloat _builtWidth;
    BOOL _contentDirty;
    NSMutableArray *_siteTiles;
    NSMutableArray *_bookmarkTiles;
    UILabel *_sitesHeading;
    UILabel *_bookmarksHeading;
    // the photo of the person behind the tiles, held to the visible rectangle while the page scrolls
    UIImageView *_backdrop;
    // lettering over a photo: 0 the colors of the theme, 1 dark over a light photo, 2 light over a dark one
    int _photoInk;
    // a page shown as a model, in the settings: the photo is given (blurred already) and not read from the settings, and the private session does not replace it
    BOOL _previewing;
    // the picture and the name at the top of the start page
    UIImageView *_brandImage;
    UIImageView *_brandGlow;
    UILabel *_brandName;
    // the page of a private tab: a glyph, a title and what the session keeps and drops
    UIView *_heroDisc;
    UIImageView *_heroIcon;
    UILabel *_heroTitle;
    UILabel *_heroText;
}
@property(nonatomic, assign) id<YZHomeViewDelegate> homeDelegate;
/* sites and bookmarks are YZRecord arrays */
- (void)showSites:(NSArray *)sites bookmarks:(NSArray *)bookmarks;
- (void)animateContents;
/* turns the page into a model for the settings: it takes no touches, shows the normal page even in the private session and shows this photo (nil for none)
   until the next call. dissolve fades the old picture into the new one, a change of the blur under a moving finger is not dissolved */
- (void)showModelWithPicture:(UIImage *)picture dissolve:(BOOL)dissolve;
/* updates the colors and texts of the chosen theme and language */
- (void)applyTheme;
@end

@protocol YZHomeViewDelegate <NSObject>
- (void)homeView:(YZHomeView *)view didSelectURL:(NSString *)url;
@end
