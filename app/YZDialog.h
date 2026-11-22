#import <UIKit/UIKit.h>

/* a question as a card over the page, in place of the system alert, which looks like another app on ios 15
   and a gray box on ios 5. the answer is the button index (0 is cancel of two, or the only button) and the
   field texts */
@interface YZDialog : UIView <UITextFieldDelegate> {
    UIView *_card;
    UILabel *_title;
    UILabel *_message;
    NSMutableArray *_fields;
    NSMutableArray *_buttons;
    NSString *_titleText;
    NSString *_messageText;
    NSArray *_placeholders;
    NSInteger _secureIndex;
    NSString *_cancelTitle;
    NSString *_confirmTitle;
    BOOL _destructive;
    void (^_handler)(NSInteger index, NSArray *texts);
    CGFloat _keyboardHeight;
    BOOL _shown;
    BOOL _leaving;
    // ios 5 to 7: the alert of the system in place of the card, and what its button answered
    id _alert;
    NSInteger _answerIndex;
    NSArray *_answerTexts;
}
/* a card with a cancel button and a confirm button, confirm is red when destructive. cancelTitle is nil for a card with one button */
+ (YZDialog *)dialogWithTitle:(NSString *)title message:(NSString *)message cancelTitle:(NSString *)cancelTitle confirmTitle:(NSString *)confirmTitle
                  destructive:(BOOL)destructive handler:(void (^)(NSInteger index, NSArray *texts))handler;
/* the texts of the empty fields, one field for each; the field at secureIndex hides its text (-1 for none) */
- (void)setFieldPlaceholders:(NSArray *)placeholders secureIndex:(NSInteger)secureIndex;
@property(nonatomic, copy) NSString *messageText;
@property(nonatomic, readonly) BOOL isShown;
- (void)showInView:(UIView *)host;
/* closes the card as if nothing was answered: the handler does not run */
- (void)dismissSilently;
@end
