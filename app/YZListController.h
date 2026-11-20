#import <UIKit/UIKit.h>
#import "YZStore.h"

typedef enum { YZListBookmarks, YZListHistory } YZListKind;

@protocol YZListControllerDelegate;

/* bookmarks or history as a table, swipe to delete */
@interface YZListController : UITableViewController {
    YZStore *_store;
    YZListKind _kind;
    NSMutableArray *_rows;
    id<YZListControllerDelegate> _listDelegate;
    BOOL _exhausted;
}
@property(nonatomic, assign) id<YZListControllerDelegate> listDelegate;
- (id)initWithStore:(YZStore *)store kind:(YZListKind)kind;
@end

@protocol YZListControllerDelegate <NSObject>
- (void)listController:(YZListController *)controller didSelectURL:(NSString *)url;
- (void)listControllerDidFinish:(YZListController *)controller;
@end
