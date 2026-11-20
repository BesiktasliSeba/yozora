#import <UIKit/UIKit.h>

/* an error as a card over the page, the backdrop or the button closes it */
@interface YZErrorPopup : UIView {
    UIView *_card;
    UIImageView *_picture;
    UILabel *_title;
    UILabel *_message;
    UIButton *_button;
    NSString *_titleText;
    NSString *_messageText;
    void (^_dismissHandler)(void);
    BOOL _shown;
    BOOL _leaving;
}
@property(nonatomic, copy) NSString *titleText;
/* changing the words of a card that is up fits it again in place */
@property(nonatomic, copy) NSString *messageText;
/* runs once, after the card has left */
@property(nonatomic, copy) void (^dismissHandler)(void);
@property(nonatomic, readonly) BOOL isShown;

- (void)showInView:(UIView *)host;
- (void)dismiss;
@end
