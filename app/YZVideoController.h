#import <UIKit/UIKit.h>
#import "yozora_core.h"

@class YZVideoController;
@class YZMediaPlayer;

@protocol YZVideoControllerDelegate <NSObject>
/* the user closed the player, the stream failed or the page asked for the close; the owner dismisses the controller */
- (void)videoControllerFinished:(YZVideoController *)controller failed:(BOOL)failed;
@end

/* the full screen player of a page's video or audio: the system decodes, the page sees an ordinary media
   element through the events sent to its core. AVPlayer exists from iOS 4, so one class serves every system */
@interface YZVideoController : UIViewController
@property (nonatomic, assign) id<YZVideoControllerDelegate> delegate;
@property (nonatomic, readonly) unsigned mediaID;
- (id)initWithPlayer:(YZMediaPlayer *)player title:(NSString *)title;
- (void)play;
- (void)pause;
- (void)seekTo:(double)seconds;
/* stops the stream without telling the page, for a page that closed the player itself or a core that goes away */
- (void)shutDown;
@end
