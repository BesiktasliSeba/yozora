#import "YZLog.h"
#import "YZCrash.h"
#include "yozora_log.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <string.h>
#include <unistd.h>

// the buffer keeps the newest lines: when it is over the cap the oldest lines go, whole
static const NSUInteger YZLogByteCap = 256 * 1024;

static NSLock *YZLogLock;
static NSMutableString *YZLogBuffer;
static NSUInteger YZLogRevision;
static BOOL YZLogOn;
// stderr as it was before the capture, the pipe the capture reads and the thread that reads it
static int YZLogSavedFD = -1;
static int YZLogReadFD = -1;
// the file of this run, written by the reader thread alone once capture has started, and the cap it is cut at
static FILE *YZLogFile;
static size_t YZLogFileBytes;
static const size_t YZLogFileCap = 1024 * 1024;
// how much of the file of the run before the screen reads
static const NSUInteger YZLogReadBytes = 256 * 1024;

static NSString *YZLogDirectory(void) {
    NSArray *caches = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
    return [caches count] ? [caches objectAtIndex:0] : NSTemporaryDirectory();
}
static NSString *YZLogPath(void) { return [YZLogDirectory() stringByAppendingPathComponent:@"yozora.log"]; }
static NSString *YZLogPreviousPath(void) { return [YZLogDirectory() stringByAppendingPathComponent:@"yozora-previous.log"]; }

// a line of the log goes to the file as well; a file past the cap starts again with a mark, the older lines are the ones that go
static void YZLogFileAppend(NSString *line) {
    if (!YZLogFile)
        return;
    const char *bytes = [line UTF8String];
    if (!bytes)
        return;
    size_t length = strlen(bytes) + 1;
    if (YZLogFileBytes + length > YZLogFileCap) {
        fclose(YZLogFile);
        YZLogFile = fopen([YZLogPath() fileSystemRepresentation], "w");
        YZLogFileBytes = 0;
        if (!YZLogFile)
            return;
        fputs("# the older lines of this run were cut here\n", YZLogFile);
    }
    fputs(bytes, YZLogFile);
    fputc('\n', YZLogFile);
    YZLogFileBytes += length;
}

@implementation YZLog

+ (void)prepare {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        YZLogLock = [[NSLock alloc] init];
        YZLogBuffer = [[NSMutableString alloc] init];
    });
}

+ (void)appendLine:(NSString *)line {
    [YZLogLock lock];
    [YZLogBuffer appendString:line];
    [YZLogBuffer appendString:@"\n"];
    if ([YZLogBuffer length] > YZLogByteCap) {
        // the oldest half goes, up to the next line break
        NSUInteger cut = [YZLogBuffer length] - YZLogByteCap / 2;
        NSRange newline = [YZLogBuffer rangeOfString:@"\n" options:0 range:NSMakeRange(cut, [YZLogBuffer length] - cut)];
        cut = newline.location == NSNotFound ? cut : newline.location + 1;
        [YZLogBuffer deleteCharactersInRange:NSMakeRange(0, cut)];
    }
    ++YZLogRevision;
    [YZLogLock unlock];
}

// forwarding must never wait: that descriptor may be a pipe nobody reads, a blocked write would stop the
// reader, fill the pipe and stop every thread that logs. what does not go on within a moment is dropped, the
// capture keeps its copy
static void YZForward(int descriptor, const unsigned char *bytes, size_t count) {
    size_t sent = 0;
    while (sent < count) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(descriptor, &writable);
        struct timeval wait = { 0, 20000 };
        int ready = select(descriptor + 1, NULL, &writable, NULL, &wait);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0)
            return;
        ssize_t written = write(descriptor, bytes + sent, count - sent);
        if (written < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (written <= 0)
            return;
        sent += (size_t)written;
    }
}

// reads the pipe until its write end closes, which is when the capture is switched off
+ (void)readLoop:(NSArray *)descriptors {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    int fd = [[descriptors objectAtIndex:0] intValue];
    // the reader's own copy of the old stderr: switching capture off closes the app's copy, and a closed
    // number may already belong to something else
    int forward = [[descriptors objectAtIndex:1] intValue];
    NSMutableData *pending = [NSMutableData data];
    unsigned char chunk[4096];
    for (;;) {
        ssize_t count = read(fd, chunk, sizeof chunk);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        // the text still goes where it went before
        if (forward >= 0)
            YZForward(forward, chunk, (size_t)count);
        [pending appendBytes:chunk length:(NSUInteger)count];
        // only whole lines are decoded, so a character cut by the end of a read is not lost
        const unsigned char *bytes = [pending bytes];
        NSUInteger length = [pending length], start = 0;
        for (NSUInteger i = 0; i < length; ++i) {
            if (bytes[i] != '\n')
                continue;
            NSString *line = [[NSString alloc] initWithBytes:bytes + start length:i - start encoding:NSUTF8StringEncoding];
            NSString *shown = line ? line : @"(bytes that are not text)";
            [self appendLine:shown];
            YZLogFileAppend(shown);
            [line release];
            start = i + 1;
        }
        // a line with no end that grows past the cap is cut, nothing waits for a newline forever
        if (length - start > 8192) {
            NSString *line = [[NSString alloc] initWithBytes:bytes + start length:8192 encoding:NSUTF8StringEncoding];
            NSString *shown = line ? line : @"(bytes that are not text)";
            [self appendLine:shown];
            YZLogFileAppend(shown);
            [line release];
            start += 8192;
        }
        if (YZLogFile)
            fflush(YZLogFile);
        [pending replaceBytesInRange:NSMakeRange(0, start) withBytes:NULL length:0];
    }
    close(fd);
    if (forward >= 0)
        close(forward);
    if (YZLogFile) {
        fclose(YZLogFile);
        YZLogFile = NULL;
    }
    [pool drain];
}

+ (void)setCapturing:(BOOL)on {
    [self prepare];
    @synchronized(self) {
        if (on == YZLogOn)
            return;
        if (on) {
            int fds[2];
            if (pipe(fds) != 0) {
                NSLog(@"yozora: the log capture could not make a pipe: %s", strerror(errno));
                return;
            }
            // a writer never waits for the reader, a blocking pipe would stop the main thread in its next
            // NSLog once the reader falls behind. a full pipe loses the line
            fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) | O_NONBLOCK);
            fflush(stderr);
            YZLogSavedFD = dup(STDERR_FILENO);
            if (YZLogSavedFD < 0 || dup2(fds[1], STDERR_FILENO) < 0) {
                NSLog(@"yozora: the log capture could not take stderr: %s", strerror(errno));
                if (YZLogSavedFD >= 0)
                    close(YZLogSavedFD);
                YZLogSavedFD = -1;
                close(fds[0]);
                close(fds[1]);
                return;
            }
            // stderr now holds the write end, the copy made for the capture can go
            close(fds[1]);
            YZLogReadFD = fds[0];
            YZLogOn = YES;
            // the file of the run before becomes the previous log, once for each run of the app, and this run writes a new one
            static BOOL rotated;
            NSFileManager *files = [NSFileManager defaultManager];
            if (!rotated) {
                rotated = YES;
                [files removeItemAtPath:YZLogPreviousPath() error:NULL];
                [files moveItemAtPath:YZLogPath() toPath:YZLogPreviousPath() error:NULL];
            }
            YZLogFile = fopen([YZLogPath() fileSystemRepresentation], "a");
            YZLogFileBytes = 0;
            if (YZLogFile) {
                NSDateFormatter *format = [[[NSDateFormatter alloc] init] autorelease];
                [format setDateFormat:@"yyyy-MM-dd HH:mm:ss"];
                fprintf(YZLogFile, "# log of this run, started %s\n", [[format stringFromDate:[NSDate date]] UTF8String]);
            } else
                NSLog(@"yozora: the log file cannot be opened: %s", strerror(errno));
            // while capture is on the core writes its debug lines too, off they are not made
            yozora_log_set_minimum(YOZORA_LOG_DEBUG);
            [NSThread detachNewThreadSelector:@selector(readLoop:) toTarget:self
                                   withObject:[NSArray arrayWithObjects:[NSNumber numberWithInt:YZLogReadFD], [NSNumber numberWithInt:dup(YZLogSavedFD)], nil]];
            NSLog(@"yozora: log capture on");
        } else {
            NSLog(@"yozora: log capture off");
            yozora_log_set_minimum(YOZORA_LOG_INFO);
            fflush(stderr);
            // stderr goes back, the last write end closes and the reader sees the end of the pipe
            dup2(YZLogSavedFD, STDERR_FILENO);
            close(YZLogSavedFD);
            YZLogSavedFD = -1;
            YZLogReadFD = -1;
            YZLogOn = NO;
        }
    }
}

+ (BOOL)capturing { return YZLogOn; }

// a core line has its own level, else the words (NSLog and page console lines say an error in words)
static int YZLogLevelOfLine(NSString *line) {
    const char *bytes = [line UTF8String];
    yozora_log_level level;
    if (bytes && yozora_log_parse(bytes, strlen(bytes), &level, NULL, 0, NULL))
        return (int)level;
    NSString *lower = [line lowercaseString];
    static NSString *const words[] = { @"error", @"fail", @"cannot", @"could not", @"refus", @"crash", @"abort", @"denied" };
    for (size_t i = 0; i < sizeof words / sizeof words[0]; ++i) {
        if ([lower rangeOfString:words[i]].location != NSNotFound)
            return YOZORA_LOG_WARN;
    }
    return YOZORA_LOG_INFO;
}

// the last `lines` lines at or above the level, in order. the look runs from the end and stops after a
// bounded number of lines, so a screen that asks every half second does not read a log nobody scrolls that
// far back in
+ (NSString *)filteredText:(NSString *)text minimumLevel:(int)level lines:(NSUInteger)lines {
    static const NSUInteger scanLimit = 6000;
    NSUInteger position = [text length];
    if (!position)
        return @"";
    NSMutableArray *kept = [NSMutableArray array];
    NSUInteger scanned = 0;
    while (position > 0 && scanned < scanLimit && (!lines || [kept count] < lines)) {
        NSRange newline = [text rangeOfString:@"\n" options:NSBackwardsSearch range:NSMakeRange(0, position)];
        NSUInteger start = newline.location == NSNotFound ? 0 : newline.location + 1;
        if (position > start) {
            NSString *line = [text substringWithRange:NSMakeRange(start, position - start)];
            if ([line length] && (level <= 0 || YZLogLevelOfLine(line) >= level))
                [kept addObject:line];
            ++scanned;
        }
        if (newline.location == NSNotFound)
            break;
        position = newline.location;
    }
    NSMutableString *result = [NSMutableString string];
    for (NSUInteger i = [kept count]; i > 0; --i) {
        [result appendString:[kept objectAtIndex:i - 1]];
        [result appendString:@"\n"];
    }
    return result;
}

// the end of the file of the run before, from the start of a line
+ (NSString *)previousText {
    NSData *data = [NSData dataWithContentsOfFile:YZLogPreviousPath()];
    if (![data length])
        return @"";
    if ([data length] > YZLogReadBytes) {
        NSData *tail = [data subdataWithRange:NSMakeRange([data length] - YZLogReadBytes, YZLogReadBytes)];
        const unsigned char *bytes = [tail bytes];
        NSUInteger skip = 0;
        while (skip < [tail length] && bytes[skip] != '\n')
            ++skip;
        data = [tail subdataWithRange:NSMakeRange(MIN(skip + 1, [tail length]), [tail length] - MIN(skip + 1, [tail length]))];
    }
    NSString *text = [[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] autorelease];
    return text ? text : @"";
}

+ (int)levelOfLine:(NSString *)line {
    return YZLogLevelOfLine(line);
}

+ (NSString *)textForSource:(YZLogSource)source minimumLevel:(int)level lines:(NSUInteger)lines {
    [self prepare];
    NSString *text;
    switch (source) {
    case YZLogSourcePrevious:
        text = [self previousText];
        break;
    case YZLogSourceCrashes: {
        NSString *report = [YZCrash report];
        // the crash record is read whole, its lines are not in the form of the log
        return report ? report : @"";
    }
    default:
        [YZLogLock lock];
        text = [[YZLogBuffer copy] autorelease];
        [YZLogLock unlock];
        break;
    }
    return [self filteredText:text minimumLevel:level lines:lines];
}

+ (NSUInteger)revision {
    [self prepare];
    [YZLogLock lock];
    NSUInteger revision = YZLogRevision;
    [YZLogLock unlock];
    return revision;
}

+ (void)clearSource:(YZLogSource)source {
    [self prepare];
    switch (source) {
    case YZLogSourcePrevious:
        [[NSFileManager defaultManager] removeItemAtPath:YZLogPreviousPath() error:NULL];
        break;
    case YZLogSourceCrashes:
        [YZCrash clearReport];
        break;
    default:
        [YZLogLock lock];
        [YZLogBuffer setString:@""];
        ++YZLogRevision;
        [YZLogLock unlock];
        break;
    }
}

@end
