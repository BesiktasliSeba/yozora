#import <UIKit/UIKit.h>

@protocol YZFindBarDelegate;

/* the find bar replaces the address bar: text, the place of the match ("3/12"), previous and next, close.
   typing searches after a pause, return goes to the next match */
@interface YZFindBar : UIView <UITextFieldDelegate> {
    id<YZFindBarDelegate> _delegate;
    UIView *_field;
    UITextField *_input;
    UILabel *_count;
    UIButton *_previous;
    UIButton *_next;
    UIButton *_close;
    NSString *_shownText;
    int _shownIndex;
    int _shownCount;
    BOOL _shownSearching;
    BOOL _hasShown;
}
@property(nonatomic, assign) id<YZFindBarDelegate> delegate;
/* the height of the bar without the strip under the status bar */
- (void)layoutInRect:(CGRect)rect topInset:(CGFloat)top barHeight:(CGFloat)bar leftInset:(CGFloat)left rightInset:(CGFloat)right;
/* the colors and texts of the chosen theme and language */
- (void)applyTheme;
/* the place of the current match and how many there are, with no match 0 of 0; found is NO while the text is empty */
- (void)showIndex:(int)index count:(int)count searching:(BOOL)searching;
/* the text in the field */
- (NSString *)text;
- (void)focus;
- (void)blur;
@end

@protocol YZFindBarDelegate <NSObject>
/* the text to look for changed: a search from the top. an empty text clears the marks */
- (void)findBar:(YZFindBar *)bar didChangeText:(NSString *)text;
/* the next match (1) or the previous one (-1) */
- (void)findBar:(YZFindBar *)bar didStep:(int)direction;
- (void)findBarDidClose:(YZFindBar *)bar;
@end
