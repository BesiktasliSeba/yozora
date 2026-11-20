#import <UIKit/UIKit.h>
#include "yozora_core.h"

/* process numbers in a corner for the debug setting, every number is measured, no gpu line because the system
   has no public reading */
@interface YZDebugOverlay : UIView {
    NSMutableArray *_keys;
    NSMutableArray *_values;
    UIView *_dot;
    NSString *_site;
    NSString *_device;
    UILabel *_deviceLabel;
    UILabel *_siteLabel;
    unsigned _frames;
    unsigned _scenes;
    CFTimeInterval _windowStart;
    double _fps;
    double _sceneRate;
    long long _cpuWindowMicros;
    long long _previousPaintMs;
    unsigned long long _previousReceived;
    unsigned long long _previousSent;
    double _receiveRate;
    double _sendRate;
}
/* the host of the page on screen, nil for none */
@property(nonatomic, copy) NSString *site;
+ (CGSize)preferredSize;
/* once for each frame of the display link, and once for each scene put on screen */
- (void)noteFrame;
- (void)noteScene;
/* reads the system and the core and redraws, a few times a second is enough. core may be NULL */
- (void)refreshWithCore:(yozora_core *)core;
@end
