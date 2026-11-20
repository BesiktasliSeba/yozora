#import <Foundation/Foundation.h>
#include "yozora_core.h"

// the system location manager for pages that ask where the device is. it runs only while the core of the tab
// on screen says a page watches, and its fixes go to that core
@interface YZLocation : NSObject
// called every display frame with what the core says. best accuracy is asked only when the page wants it
- (void)updateForCore:(yozora_core *)core wanted:(BOOL)wanted highAccuracy:(BOOL)highAccuracy;
// the core goes away (the tab closes), the manager stops and does not touch it again
- (void)detachCore:(yozora_core *)core;
@end
