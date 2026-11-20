#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#import "YZControls.h"
#import "YZDialog.h"

/* what the person decided for sites: geolocation, notifications and the sites where the blocker is paused. a
   swipe takes back a row, the bin takes back the shown list. the core keeps the decisions (yozora_site_*) */
@interface YZSitesController : UIViewController <UITableViewDataSource, UITableViewDelegate> {
    UIView *_card;
    UIView *_bar;
    UILabel *_titleLabel;
    UILabel *_subtitleLabel;
    UIButton *_clear;
    YZSegmentedPill *_kinds;
    UITableView *_table;
    // the rows of the kind that is shown: the site and whether it is allowed
    NSMutableArray *_keys;
    NSMutableArray *_allowed;
    YZBarFill *_fill;
    YZDialog *_dialog;
}
@end
