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
    // the picture and the name at the top of the start page
    UIImageView *_brandImage;
    UIImageView *_brandName;
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
/* updates the colors and texts of the chosen theme and language */
- (void)applyTheme;
@end

@protocol YZHomeViewDelegate <NSObject>
- (void)homeView:(YZHomeView *)view didSelectURL:(NSString *)url;
@end
