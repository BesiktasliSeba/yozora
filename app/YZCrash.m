#import "YZCrash.h"

#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ucontext.h>
#include <time.h>
#include <unistd.h>

static const off_t YZCrashFileCap = 64 * 1024;
static const int YZCrashSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS };

// everything the handlers need is made at install: a handler cannot allocate
static int YZCrashFD = -1;
static char YZCrashIdentity[160];
static NSUncaughtExceptionHandler *YZCrashPreviousHandler;
static struct sigaction YZCrashPrevious[sizeof YZCrashSignals / sizeof YZCrashSignals[0]];
static char YZCrashStack[64 * 1024];

// the ring of notes, written by the app and read by the handlers
// a screen of notes is not enough to see what led to a crash in a pool: the last forty eight
#define YZCrashNoteCount 48
#define YZCrashNoteLength 120
static char YZCrashNotes[YZCrashNoteCount][YZCrashNoteLength];
static volatile unsigned YZCrashNoteTotal;
static time_t YZCrashStarted;

// the crash record also goes to stderr so the log holds it
static volatile sig_atomic_t YZCrashEcho;

static void YZCrashWrite(const char *text) {
    size_t length = strlen(text);
    if (YZCrashFD >= 0)
        write(YZCrashFD, text, length);
    if (YZCrashEcho)
        write(STDERR_FILENO, text, length);
}

// decimal and hexadecimal without printf, which is not safe inside a signal handler
static void YZCrashNumber(unsigned long long value, unsigned base, char *out) {
    char digits[24];
    size_t n = 0;
    do {
        digits[n++] = "0123456789abcdef"[value % base];
        value /= base;
    } while (value && n < sizeof digits);
    size_t at = 0;
    if (base == 16) {
        out[at++] = '0';
        out[at++] = 'x';
    }
    while (n)
        out[at++] = digits[--n];
    out[at] = '\0';
}

static void YZCrashField(const char *name, unsigned long long value, unsigned base) {
    char number[32];
    YZCrashNumber(value, base, number);
    YZCrashWrite(name);
    YZCrashWrite(number);
}

static void YZCrashNotesWrite(void) {
    unsigned total = YZCrashNoteTotal;
    if (!total)
        return;
    YZCrashWrite("notes, oldest first:\n");
    for (unsigned i = total > YZCrashNoteCount ? total - YZCrashNoteCount : 0; i < total; ++i) {
        YZCrashWrite("  ");
        YZCrashWrite(YZCrashNotes[i % YZCrashNoteCount]);
        YZCrashWrite("\n");
    }
}

static void YZCrashHeader(const char *kind) {
    YZCrashWrite("\n=== ");
    YZCrashWrite(kind);
    YZCrashField(" time ", (unsigned long long)time(NULL), 10);
    YZCrashWrite(" ");
    YZCrashWrite(YZCrashIdentity);
    YZCrashWrite("\n");
}

// the registers of the stopped thread: r0 is the object a message went to, pc and lr say where it was, the rest
// are what the code held. not in the system's reports, which never arrive for a signal of this app
static void YZCrashRegisters(void *context) {
    if (!context)
        return;
    ucontext_t *thread = (ucontext_t *)context;
    if (!thread->uc_mcontext)
        return;
#if defined(__arm__)
    const unsigned count = 13;
    YZCrashWrite("registers:");
    for (unsigned i = 0; i < count; ++i) {
        char name[12] = " r";
        YZCrashNumber(i, 10, name + 2);
        strlcat(name, "=", sizeof name);
        YZCrashField(name, thread->uc_mcontext->__ss.__r[i], 16);
    }
    YZCrashField(" sp=", thread->uc_mcontext->__ss.__sp, 16);
    YZCrashField(" lr=", thread->uc_mcontext->__ss.__lr, 16);
    YZCrashField(" pc=", thread->uc_mcontext->__ss.__pc, 16);
    YZCrashWrite("\n");
#elif defined(__arm64__)
    YZCrashWrite("registers:");
    for (unsigned i = 0; i < 29; ++i) {
        char name[12] = " x";
        YZCrashNumber(i, 10, name + 2);
        strlcat(name, "=", sizeof name);
        YZCrashField(name, thread->uc_mcontext->__ss.__x[i], 16);
    }
    YZCrashField(" fp=", thread->uc_mcontext->__ss.__fp, 16);
    YZCrashField(" lr=", thread->uc_mcontext->__ss.__lr, 16);
    YZCrashField(" sp=", thread->uc_mcontext->__ss.__sp, 16);
    YZCrashField(" pc=", thread->uc_mcontext->__ss.__pc, 16);
    YZCrashWrite("\n");
#endif
}

static uintptr_t YZCrashFaultPC(void *context) {
    ucontext_t *thread = (ucontext_t *)context;
    if (!thread || !thread->uc_mcontext)
        return 1;
#if defined(__arm__)
    return thread->uc_mcontext->__ss.__pc;
#elif defined(__arm64__)
    return (uintptr_t)thread->uc_mcontext->__ss.__pc;
#else
    return 1;
#endif
}

// the code of an image is in its segments, and generated code (the jit) is in none: a jump to such an address that ends in a fault
// means the jit made something that was not code. no malloc and no lock here, dyld is only read
static int YZCrashInImage(uintptr_t pc) {
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        const struct mach_header *header = _dyld_get_image_header(i);
        if (!header)
            continue;
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);
#ifdef __LP64__
        const struct load_command *command = (const struct load_command *)((const char *)header + sizeof(struct mach_header_64));
#else
        const struct load_command *command = (const struct load_command *)((const char *)header + sizeof(struct mach_header));
#endif
        for (uint32_t c = 0; c < header->ncmds; ++c) {
#ifdef __LP64__
            if (command->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *segment = (const struct segment_command_64 *)command;
#else
            if (command->cmd == LC_SEGMENT) {
                const struct segment_command *segment = (const struct segment_command *)command;
#endif
                uintptr_t low = (uintptr_t)(segment->vmaddr + slide), high = low + (uintptr_t)segment->vmsize;
                if (pc >= low && pc < high)
                    return 1;
            }
            command = (const struct load_command *)((const char *)command + command->cmdsize);
        }
    }
    return 0;
}

static void YZCrashSignalHandler(int signal_number, siginfo_t *info, void *context) {
    YZCrashEcho = 1;
    YZCrashHeader("signal");
    YZCrashField("signal ", (unsigned)signal_number, 10);
    YZCrashField(" code ", (unsigned long long)(unsigned)(info ? info->si_code : 0), 10);
    YZCrashField(" address ", (unsigned long long)(uintptr_t)(info ? info->si_addr : NULL), 16);
    YZCrashWrite("\n");
    YZCrashRegisters(context);
    {
        uintptr_t pc = YZCrashFaultPC(context);
        if (!YZCrashInImage(pc)) {
            YZCrashField("fault in generated code, pc ", (unsigned long long)pc, 16);
            YZCrashWrite("\n");
        }
    }
    YZCrashNotesWrite();
    void *frames[48];
    int count = backtrace(frames, 48);
    if (YZCrashFD >= 0)
        backtrace_symbols_fd(frames, count, YZCrashFD);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    fsync(YZCrashFD);
    // the log capture reads the app's stderr on its own thread and writes the file: the last line before a stop (a zombie
    // names its class there) is on its way when this handler starts, so the stop waits a moment for it
    struct timespec pause = { 0, 300 * 1000 * 1000 };
    nanosleep(&pause, NULL);
    // the system still gets the crash: with the default action restored the same signal ends the process the way it would have
    for (size_t i = 0; i < sizeof YZCrashSignals / sizeof YZCrashSignals[0]; ++i)
        signal(YZCrashSignals[i], SIG_DFL);
    raise(signal_number);
}

static void YZCrashException(NSException *exception) {
    YZCrashEcho = 1;
    YZCrashHeader("exception");
    NSString *text = [NSString stringWithFormat:@"%@: %@\n%@\n", [exception name], [exception reason], [[exception callStackReturnAddresses] componentsJoinedByString:@" "]];
    YZCrashWrite([text UTF8String]);
    YZCrashNotesWrite();
    YZCrashWrite([[[exception callStackSymbols] componentsJoinedByString:@"\n"] UTF8String]);
    YZCrashWrite("\n");
    fsync(YZCrashFD);
    if (YZCrashPreviousHandler)
        YZCrashPreviousHandler(exception);
}

// "uuid ... slide 0x..." of the main binary
static void YZCrashDescribeBinary(void) {
    const struct mach_header *header = _dyld_get_image_header(0);
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    char uuid[40] = "unknown";
    if (header) {
#ifdef __LP64__
        const struct load_command *command = (const struct load_command *)((const char *)header + sizeof(struct mach_header_64));
#else
        const struct load_command *command = (const struct load_command *)((const char *)header + sizeof(struct mach_header));
#endif
        for (uint32_t i = 0; i < header->ncmds; ++i) {
            if (command->cmd == LC_UUID) {
                const struct uuid_command *found = (const struct uuid_command *)command;
                char *out = uuid;
                for (int b = 0; b < 16; ++b)
                    out += snprintf(out, 3, "%02X", found->uuid[b]);
                break;
            }
            command = (const struct load_command *)((const char *)command + command->cmdsize);
        }
    }
    snprintf(YZCrashIdentity, sizeof YZCrashIdentity, "uuid %s slide 0x%lx", uuid, (unsigned long)slide);
}

@implementation YZCrash

+ (NSString *)path {
    NSArray *caches = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
    NSString *directory = [caches count] ? [caches objectAtIndex:0] : NSTemporaryDirectory();
    return [directory stringByAppendingPathComponent:@"yozora-crash.log"];
}

+ (void)install {
    static BOOL installed;
    if (installed)
        return;
    installed = YES;
    const char *path = [[self path] fileSystemRepresentation];
    struct stat info;
    int flags = O_WRONLY | O_CREAT | O_APPEND;
    // a record that grew past the cap starts again, the newest crash is what matters
    if (stat(path, &info) == 0 && info.st_size > YZCrashFileCap)
        flags |= O_TRUNC;
    YZCrashStarted = time(NULL);
    YZCrashFD = open(path, flags, 0644);
    YZCrashDescribeBinary();
    if (YZCrashFD < 0) {
        NSLog(@"yozora: the crash record %s cannot be opened: %s", path, strerror(errno));
        return;
    }
    YZCrashPreviousHandler = NSGetUncaughtExceptionHandler();
    NSSetUncaughtExceptionHandler(YZCrashException);
    // a stack overflow cannot run a handler on the stack that overflowed
    stack_t alternate;
    alternate.ss_sp = YZCrashStack;
    alternate.ss_size = sizeof YZCrashStack;
    alternate.ss_flags = 0;
    sigaltstack(&alternate, NULL);
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = YZCrashSignalHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < sizeof YZCrashSignals / sizeof YZCrashSignals[0]; ++i)
        sigaction(YZCrashSignals[i], &action, &YZCrashPrevious[i]);
    YZCrashHeader("start");
}

+ (void)note:(NSString *)text {
    const char *utf8 = [text UTF8String];
    if (!utf8)
        return;
    unsigned at = YZCrashNoteTotal % YZCrashNoteCount;
    snprintf(YZCrashNotes[at], YZCrashNoteLength, "+%lds %s", (long)(time(NULL) - YZCrashStarted), utf8);
    ++YZCrashNoteTotal;
}

+ (void)noteLifecycle:(NSString *)event object:(id)object {
    [self note:[NSString stringWithFormat:@"%@ %@ %p", event, object ? NSStringFromClass([object class]) : @"nil", object]];
}

+ (BOOL)enableZombies {
    // the same switch as the NSZombieEnabled variable, which cannot be set from a launch from the home screen
    void (*enable)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "_CFEnableZombies");
    if (!enable) {
        NSLog(@"yozora: zombie objects cannot be turned on, this system has no _CFEnableZombies");
        [self note:@"zombies: not available"];
        return NO;
    }
    enable();
    [self note:@"zombies on"];
    NSLog(@"yozora: zombie objects on: a message to a freed object is logged with its class and the app stops there");
    return YES;
}

+ (NSString *)report {
    NSData *data = [NSData dataWithContentsOfFile:[self path]];
    if (![data length])
        return nil;
    NSString *text = [[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] autorelease];
    return text;
}

+ (void)clearReport {
    if (YZCrashFD >= 0)
        ftruncate(YZCrashFD, 0);
    else
        [[NSFileManager defaultManager] removeItemAtPath:[self path] error:NULL];
}

@end
