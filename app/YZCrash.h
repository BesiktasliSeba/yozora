#import <Foundation/Foundation.h>

/* own crash record: system reports do not arrive for exceptions or signals, only the watchdog ones. it writes
   name, reason and addresses with the uuid and slide of the binary, kept between runs and cut past 64 KiB */
@interface YZCrash : NSObject
/* installs the handlers, once, at the start of the app */
+ (void)install;
/* a note of what the app does, a ring of the last sixteen, written with a crash so "somewhere in UIKit" still
   says what was on screen. any thread, two writers may mix a line */
+ (void)note:(NSString *)text;
/* a note of "event Class 0xaddress" for a screen or a view coming or going, so the record says which objects were alive */
+ (void)noteLifecycle:(NSString *)event object:(id)object;
/* keeps freed objects as zombies for the rest of the run: a message to one is logged with the class and the address and
   stops the app there, instead of a crash somewhere later in a pool. needs more memory. returns NO when this system
   has no way to do it */
+ (BOOL)enableZombies;
/* the path of the record */
+ (NSString *)path;
/* what the previous runs wrote, nil when nothing was written */
+ (NSString *)report;
+ (void)clearReport;
@end
