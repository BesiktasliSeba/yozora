#import <UIKit/UIKit.h>

/* a card of rows that grows out of the point where a finger was held, over a light scrim that closes it on a
   tap. rows are a glyph in the accent color and a title, the same look as the main menu */
@interface YZPopupMenu : UIView {
    NSArray *_titles;
    NSArray *_icons;
    void (^_handler)(NSInteger index);
    UIView *_panel;
    CGFloat _bottomInset;
    CGSize _hostSize;
    BOOL _closing;
    // the classic design asks with the action sheet of the system and builds no card
    id _sheet;
}
/* the distance from the bottom of the host to the lowest place the card may reach, the bottom bar and the home indicator */
@property(nonatomic, assign) CGFloat bottomInset;
@property(nonatomic, readonly, getter=isClosing) BOOL closing;
/* icons are YZIcon values boxed in NSNumber. the handler runs once on the main thread after the card has left,
   with the index of the row, or -1 when the menu closed any other way, including dismissAnimated: */
- (id)initWithTitles:(NSArray *)titles icons:(NSArray *)icons handler:(void (^)(NSInteger index))handler;
/* the card grows from the corner nearest to anchor, a point in the coordinates of host */
- (void)showInView:(UIView *)host anchor:(CGPoint)anchor;
- (void)dismissAnimated:(BOOL)animated;
@end
