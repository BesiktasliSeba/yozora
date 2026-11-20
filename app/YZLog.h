#import <Foundation/Foundation.h>

/* the app log: while capture is on, stderr (NSLog, core, page console errors) is also kept in a bounded
   buffer and a file and still goes where it went. the run before is kept so a crash can be read after it */
typedef enum {
    YZLogSourceCurrent = 0,
    YZLogSourcePrevious = 1,
    /* what the crash handler wrote (YZCrash) */
    YZLogSourceCrashes = 2
} YZLogSource;

@interface YZLog : NSObject
+ (void)setCapturing:(BOOL)on;
+ (BOOL)capturing;
/* the last `lines` lines of a source at or above minimumLevel (0 debug to 3 error), 0 for all */
+ (NSString *)textForSource:(YZLogSource)source minimumLevel:(int)level lines:(NSUInteger)lines;
/* grows with every new line of this run, so a screen can tell that it has to read again */
/* the level of a line by its own mark when it is a core line, by its words otherwise */
+ (int)levelOfLine:(NSString *)line;
+ (NSUInteger)revision;
+ (void)clearSource:(YZLogSource)source;
@end
