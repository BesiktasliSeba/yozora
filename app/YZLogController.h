#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#import "YZControls.h"

/* the log screen: a terminal card in the theme colors with the newest lines of YZLog in JetBrains Mono. it
   follows the end until the person scrolls away */
@interface YZLogController : UIViewController <UITableViewDataSource, UITableViewDelegate> {
    UIView *_card;
    UIView *_bar;
    UIView *_badge;
    UIImageView *_badgeIcon;
    UILabel *_titleLabel;
    UILabel *_subtitleLabel;
    UIButton *_copy;
    UIButton *_clear;
    // one row per line: a table builds only the rows on screen, a text view lays out all the text and keeps a
    // layer as tall as the whole
    UITableView *_table;
    NSArray *_rows;
    NSArray *_rowLevels;
    // the height of each row, made with the rows: a table asks for all of them on every reload
    NSArray *_rowHeights;
    // the lines are read, split and measured off the main thread. a reload that is wanted while one runs waits for
    // the next tick, a change of what is shown (filter, clear, width) bumps the generation and the result of the
    // run before is dropped
    NSUInteger _generation;
    BOOL _loading;
    // what the screen shows: the source (this run, the run before, the crash record) and the lowest level of the lines
    YZSegmentedPill *_sources;
    YZSegmentedPill *_levels;
    NSInteger _source;
    NSInteger _level;
    // behind the bar, which carries the name of the screen next to the back arrow
    YZBarFill *_fill;
    NSString *_heading;
    NSTimer *_timer;
    NSUInteger _shownRevision;
    BOOL _pinned;
}
@end
