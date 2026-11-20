#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import "yozora_core.h"

/* the player of one media element: the system decodes, the page sees the events this object sends to its
   core. AVPlayer exists from iOS 4, so one class serves every system. a video has a layer the page view puts
   under the tiles */
@interface YZMediaPlayer : NSObject
@property (nonatomic, readonly) unsigned mediaID;
@property (nonatomic, readonly) NSString *title;
@property (nonatomic, readonly) BOOL playing;
@property (nonatomic, readonly) BOOL failed;
@property (nonatomic, readonly) double position;
@property (nonatomic, readonly) double duration;
/* the end of the loaded part around the position, in seconds, 0 before anything is loaded */
@property (nonatomic, readonly) double bufferedEnd;
/* the player was asked to play and the picture does not move: the stream is still opening or ran dry */
@property (nonatomic, readonly) BOOL buffering;
/* what the person set on this player, the page does not hear of it and the next volume or rate of the element
   replaces it */
@property (nonatomic, assign) BOOL muted;
@property (nonatomic, assign) double playbackRate;
- (void)play;
- (void)pause;
- (void)seekTo:(double)seconds;
/* a seek by the given seconds from the position, held to the stream */
- (void)skipBy:(double)seconds;
/* the picture of a video, nil for sound. its frame is set by whoever shows it */
@property (nonatomic, readonly) CALayer *videoLayer;
- (id)initWithRequest:(const yozora_media_request *)request core:(yozora_core *)core;
/* play, pause, seek, volume, rate and close of the element */
- (void)handleRequest:(const yozora_media_request *)request;
/* stops the stream without telling the page, for a core that goes away or a page that closed the player itself */
- (void)shutDown;
@end
