#import <UIKit/UIKit.h>

@protocol YZDownloadsControllerDelegate;

/* downloads as material 3 cards: running ones with a wave bar, size, speed, time left and cancel, then what
   the folder holds. a swipe removes a finished one */
@interface YZDownloadsController : UITableViewController <UIDocumentInteractionControllerDelegate> {
    NSMutableArray *_rows;
    // what the visible cells were built from, and the last byte count of each running download for its speed
    NSArray *_signature;
    NSMutableDictionary *_samples;
    NSTimer *_timer;
    UIDocumentInteractionController *_preview;
    id<YZDownloadsControllerDelegate> _downloadsDelegate;
}
@property(nonatomic, assign) id<YZDownloadsControllerDelegate> downloadsDelegate;
/* where the files go: Documents/Downloads of the app */
+ (NSString *)directory;
@end

@protocol YZDownloadsControllerDelegate <NSObject>
- (void)downloadsControllerDidFinish:(YZDownloadsController *)controller;
@end
