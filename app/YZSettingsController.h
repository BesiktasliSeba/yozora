#import <UIKit/UIKit.h>
#import "YZStore.h"
#import "YZControls.h"

@protocol YZSettingsControllerDelegate;

/* the settings sheet the menu opens, in the manner of Firefox: groups of rows that switch, choose or clear */
@interface YZSettingsController : UITableViewController {
    id<YZSettingsControllerDelegate> _settingsDelegate;
    YZStore *_store;
    NSArray *_sections;
    // what stands behind the bar: solid at the top of the list, a blur of the list once it has gone under
    YZBarFill *_barFill;
    // the jit row line, read from the core once per refresh, the table asks for a row's text many times while
    // it lays out and scrolls
    NSString *_jitText;
    CGFloat _layoutWidth;
}
@property(nonatomic, assign) id<YZSettingsControllerDelegate> settingsDelegate;
/* the store is where history is cleared, it may be nil when the profile did not open */
- (id)initWithStore:(YZStore *)store;
@end

@protocol YZSettingsControllerDelegate <NSObject>
- (void)settingsControllerDidFinish:(YZSettingsController *)controller;
/* a setting changed that the open screens show: the theme, the language or the text of a page */
- (void)settingsControllerDidChange:(YZSettingsController *)controller;
@end

/* options for a setting with a few values. the chosen one is a plate in its own color and choosing leaves the
   screen in place so a new theme or language shows at once. a mark stands before each option when pictures
   are given, a line under the name when details are */
@interface YZChoiceController : UITableViewController {
    // the name of the screen, set in the bar next to the back arrow
    NSString *_heading;
    NSArray *_titles;
    NSArray *_details;
    NSArray *_pictures;
    // the color of the plate of each option, the accent where it has none
    NSArray *_tints;
    NSInteger _selected;
    BOOL _bareMarks;
    YZBarFill *_barFill;
    void (^_choose)(NSInteger index);
    void (^_refill)(YZChoiceController *screen);
}
@property(nonatomic, copy) NSString *heading;
@property(nonatomic, retain) NSArray *titles;
@property(nonatomic, retain) NSArray *details;
/* marks (UIImage) before the options, one for each, nil for plain rows */
@property(nonatomic, retain) NSArray *pictures;
@property(nonatomic, retain) NSArray *tints;
/* the marks stand without a white disc behind them: a flag or a preview in a Material shape */
@property(nonatomic, assign) BOOL bareMarks;
@property(nonatomic, assign) NSInteger selected;
/* called after a choice, sets the screen's texts and marks again from the settings */
@property(nonatomic, copy) void (^refill)(YZChoiceController *screen);
- (id)initWithTitle:(NSString *)title options:(NSArray *)titles selected:(NSInteger)selected choose:(void (^)(NSInteger index))choose;
@end
