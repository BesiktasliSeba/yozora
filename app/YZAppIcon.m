#import "YZAppIcon.h"

#import <UIKit/UIKit.h>
#import <objc/message.h>

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "yozora_icon_environment.h"

// the app is an executable, so the environment is a symbol it links against, the sdk headers do not declare it
extern char **environ;

static NSString *const YZIconErrorDomain = @"YZAppIcon";
// the name of the chosen icon sits in the plist beside the files it chose, so the two cannot disagree
static NSString *const YZIconNameKey = @"YozoraIconName";
// the icon entries of the packaged plist, saved before the first rewrite so the icon of the app can come back
static NSString *const YZIconOriginalKey = @"YozoraOriginalIcons";
// the folder of the bundle that keeps the icon files of the app itself, for the installs that overwrite them
static NSString *const YZOriginalIconsFolder = @"yozora-original-icons";
// the choice apart from the bundle: the name of the alternate icon, or an empty string for the icon of the app
static NSString *const YZIconChoiceKey = @"yozora.icon";

static BOOL YZNativeIconsSupported(void) {
    UIApplication *application = [UIApplication sharedApplication];
    SEL supports = NSSelectorFromString(@"supportsAlternateIcons");
    SEL set = NSSelectorFromString(@"setAlternateIconName:completionHandler:");
    if (![application respondsToSelector:supports] || ![application respondsToSelector:set])
        return NO;
    return ((BOOL (*)(id, SEL))(void *)objc_msgSend)(application, supports);
}

static NSString *YZBundlePlistPath(void) {
    return [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@"Info.plist"];
}

// the plist is replaced by a new file, which needs the folder. access() does not see what the sandbox denies: a
// bundle installed by the system lets access() say yes while the write fails with permErr (-54) on ios 6, so the
// folder is tried with a real file once per run
static BOOL YZBundleWritable(void) {
    static BOOL writable;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSString *bundle = [[NSBundle mainBundle] bundlePath];
        if (access([bundle fileSystemRepresentation], W_OK) != 0 || access([YZBundlePlistPath() fileSystemRepresentation], W_OK) != 0)
            return;
        NSString *probe = [bundle stringByAppendingPathComponent:@".yozora-write-probe"];
        int fd = open([probe fileSystemRepresentation], O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd < 0)
            return;
        close(fd);
        writable = unlink([probe fileSystemRepresentation]) == 0;
    });
    return writable;
}

static const char *YZKillallPath(void) {
    if (access("/usr/bin/killall", X_OK) == 0)
        return "/usr/bin/killall";
    if (access("/bin/killall", X_OK) == 0)
        return "/bin/killall";
    return NULL;
}

BOOL YZAppIconCanRewrite(void) {
    return YZBundleWritable() && access("/usr/bin/uicache", X_OK) == 0 && YZKillallPath() != NULL;
}

static dispatch_queue_t YZIconQueue(void) {
    static dispatch_queue_t queue;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ queue = dispatch_queue_create("yozora.icon", DISPATCH_QUEUE_SERIAL); });
    return queue;
}

YZAppIconMode YZAppIconCurrentMode(void) {
    if ([YZAlternateIconNames() count] == 0)
        return YZAppIconUnavailable;
    NSDictionary *plist = [NSDictionary dictionaryWithContentsOfFile:YZBundlePlistPath()];
    if ([plist objectForKey:YZIconOriginalKey] && YZAppIconCanRewrite())
        return YZAppIconRewrite;
    if (YZNativeIconsSupported())
        return YZAppIconNative;
    return YZAppIconCanRewrite() ? YZAppIconRewrite : YZAppIconUnavailable;
}

static NSError *YZIconFailure(NSInteger code, NSString *reason);

// the step and errno go in the error with the path, owners and modes: a refused write does not say which of them refused it
static NSError *YZIconWriteFailure(NSString *path, const char *step, int code) {
    struct stat info;
    NSString *folder = [path stringByDeletingLastPathComponent];
    NSString *state = stat([path fileSystemRepresentation], &info) == 0
        ? [NSString stringWithFormat:@"file uid %u mode %o", (unsigned)info.st_uid, (unsigned)info.st_mode & 0777]
        : @"file absent";
    NSString *folderState = stat([folder fileSystemRepresentation], &info) == 0
        ? [NSString stringWithFormat:@"folder uid %u mode %o", (unsigned)info.st_uid, (unsigned)info.st_mode & 0777]
        : @"folder absent";
    NSString *text = [NSString stringWithFormat:@"%@ was not written, %s failed: %s (%d) (%@, %@, process uid %u)", [path lastPathComponent], step, strerror(code), code,
        state, folderState, (unsigned)getuid()];
    NSLog(@"yozora: icon: %@", text);
    return YZIconFailure(code, text);
}

// replaces a file with a new one made beside it. NSDataWritingAtomic does the same but also gives the new file the owner of the old
// one, and a plist installed by root in a folder of mobile fails that fchown on ios 6 with Cocoa error 513 (NSFileWriteNoPermissionError)
// although the folder accepts the write. here the new file belongs to the process, which is all the rename needs
static NSError *YZReplaceFile(NSData *data, NSString *path) {
    struct stat old;
    mode_t mode = stat([path fileSystemRepresentation], &old) == 0 ? (old.st_mode & 0777) : 0644;
    NSString *temporary = [path stringByAppendingString:@".yozora-new"];
    const char *name = [temporary fileSystemRepresentation];
    unlink(name);
    int fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return YZIconWriteFailure(path, "open", errno);
    const uint8_t *bytes = [data bytes];
    size_t done = 0, total = [data length];
    while (done < total) {
        ssize_t n = write(fd, bytes + done, total - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            int code = n < 0 ? errno : EIO;
            close(fd);
            unlink(name);
            return YZIconWriteFailure(path, "write", code);
        }
        done += (size_t)n;
    }
    if (fchmod(fd, mode) != 0 || fsync(fd) != 0) {
        int code = errno;
        close(fd);
        unlink(name);
        return YZIconWriteFailure(path, "fchmod or fsync", code);
    }
    if (close(fd) != 0) {
        int code = errno;
        unlink(name);
        return YZIconWriteFailure(path, "close", code);
    }
    if (rename(name, [path fileSystemRepresentation]) != 0) {
        int code = errno;
        unlink(name);
        return YZIconWriteFailure(path, "rename", code);
    }
    return nil;
}

static NSError *YZIconFailure(NSInteger code, NSString *reason) {
    return [NSError errorWithDomain:YZIconErrorDomain code:code userInfo:[NSDictionary dictionaryWithObject:reason forKey:NSLocalizedDescriptionKey]];
}

// runs a program with an argument vector and waits for it, nil when it exited with status 0
static NSError *YZRunProgram(const char *path, char *const argv[], BOOL mobileHome) {
    pid_t pid = 0;
    char **environment = environ;
    if (mobileHome) {
        size_t count = 0;
        while (count < 4096 && environ[count])
            ++count;
        if (count == 4096)
            return YZIconFailure(8, @"the environment is too large for uicache");
        environment = calloc(count + 3, sizeof(*environment));
        if (!environment)
            return YZIconFailure(ENOMEM, @"the uicache environment could not be allocated");
        if (!yozora_icon_cache_environment(environ, environment, count + 3)) {
            free(environment);
            return YZIconFailure(8, @"the uicache environment could not be prepared");
        }
    }
    int started = posix_spawn(&pid, path, NULL, NULL, argv, environment);
    if (mobileHome)
        free(environment);
    if (started != 0)
        return YZIconFailure(started, [NSString stringWithFormat:@"%s did not start (%d)", path, started]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return YZIconFailure(errno, [NSString stringWithFormat:@"%s was not waited for (%d)", path, errno]);
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return nil;
    return YZIconFailure(status, [NSString stringWithFormat:@"%s ended with status %d", path, status]);
}

// the home screen reads icons from its own cache of the app, which only uicache fills again, and SpringBoard shows
// the new icon after it starts again
static NSError *YZRefreshHomeScreen(void) {
    char *cache[] = {"uicache", NULL};
    char *restart[] = {"killall", "SpringBoard", NULL};
    NSError *error = YZRunProgram("/usr/bin/uicache", cache, YES);
    if (error)
        return error;
    const char *killall = YZKillallPath();
    return killall ? YZRunProgram(killall, restart, NO) : YZIconFailure(4, @"SpringBoard cannot be restarted");
}

// the icon entries a plist keeps: the iphone and ipad dictionaries and the flat lists of the oldest systems
static NSArray *YZIconEntryKeys(void) {
    return [NSArray arrayWithObjects:@"CFBundleIcons", @"CFBundleIcons~ipad", @"CFBundleIconFiles", @"CFBundleIconFiles~ipad", nil];
}

static id YZDeepCopy(id plist) {
    NSData *data = [NSPropertyListSerialization dataWithPropertyList:plist format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
    return data ? [NSPropertyListSerialization propertyListWithData:data options:NSPropertyListMutableContainers format:NULL error:NULL] : nil;
}

static NSMutableDictionary *YZReadBundlePlist(NSError **error) {
    NSData *data = [NSData dataWithContentsOfFile:YZBundlePlistPath()];
    NSMutableDictionary *plist = data ? [NSPropertyListSerialization propertyListWithData:data options:NSPropertyListMutableContainers format:NULL error:error] : nil;
    return [plist isKindOfClass:[NSMutableDictionary class]] ? plist : nil;
}

static NSError *YZWriteBundlePlist(NSDictionary *plist) {
    NSError *error = nil;
    NSData *out = [NSPropertyListSerialization dataWithPropertyList:plist format:NSPropertyListXMLFormat_v1_0 options:0 error:&error];
    if (!out)
        return error ? error : YZIconFailure(2, @"the Info.plist could not be made");
    return YZReplaceFile(out, YZBundlePlistPath());
}

// the plist with the primary icon of every entry swapped for the files of the named alternate icon, or put back as
// the package had it for nil
static NSError *YZRewriteIconPlist(NSString *name) {
    NSError *error = nil;
    NSMutableDictionary *plist = YZReadBundlePlist(&error);
    if (!plist)
        return error ? error : YZIconFailure(1, @"the Info.plist could not be read");
    if (![plist objectForKey:YZIconOriginalKey]) {
        NSMutableDictionary *saved = [NSMutableDictionary dictionary];
        for (NSString *key in YZIconEntryKeys()) {
            id entry = [plist objectForKey:key];
            id copy = entry ? YZDeepCopy(entry) : nil;
            if (copy)
                [saved setObject:copy forKey:key];
        }
        // ios 5 reads the pre-rendered flag of the flat file list from the top of the plist, so it is kept with the rest
        if ([plist objectForKey:@"UIPrerenderedIcon"])
            [saved setObject:[plist objectForKey:@"UIPrerenderedIcon"] forKey:@"UIPrerenderedIcon"];
        [plist setObject:saved forKey:YZIconOriginalKey];
    }
    NSMutableDictionary *original = [plist objectForKey:YZIconOriginalKey];
    if (!name && [original objectForKey:@"UIPrerenderedIcon"])
        [plist setObject:[original objectForKey:@"UIPrerenderedIcon"] forKey:@"UIPrerenderedIcon"];
    for (NSString *key in YZIconEntryKeys()) {
        id saved = [original objectForKey:key];
        if (!saved)
            continue;
        if (!name) {
            // a copy, because the alternate icons of the dictionary entries must stay in the plist whole
            id copy = YZDeepCopy(saved);
            if (copy)
                [plist setObject:copy forKey:key];
            continue;
        }
        BOOL flat = [saved isKindOfClass:[NSArray class]];
        NSDictionary *source = flat ? [original objectForKey:[key hasSuffix:@"~ipad"] ? @"CFBundleIcons~ipad" : @"CFBundleIcons"] : saved;
        NSDictionary *alternates = [source isKindOfClass:[NSDictionary class]] ? [source objectForKey:@"CFBundleAlternateIcons"] : nil;
        NSDictionary *icon = [alternates objectForKey:name];
        NSArray *files = [icon objectForKey:@"CFBundleIconFiles"];
        // an entry without files for this alternate keeps its primary icon
        if (![files count])
            continue;
        if (flat) {
            [plist setObject:files forKey:key];
            id flatFlag = [icon objectForKey:@"UIPrerenderedIcon"];
            if (flatFlag)
                [plist setObject:flatFlag forKey:@"UIPrerenderedIcon"];
            continue;
        }
        NSMutableDictionary *primary = [[plist objectForKey:key] objectForKey:@"CFBundlePrimaryIcon"];
        [primary setObject:files forKey:@"CFBundleIconFiles"];
        id prerendered = [icon objectForKey:@"UIPrerenderedIcon"];
        if (prerendered)
            [primary setObject:prerendered forKey:@"UIPrerenderedIcon"];
    }
    if (name)
        [plist setObject:name forKey:YZIconNameKey];
    else
        [plist removeObjectForKey:YZIconNameKey];
    return YZWriteBundlePlist(plist);
}

NSString *YZAlternateIconName(void) {
    if (YZAppIconCurrentMode() == YZAppIconNative) {
        SEL current = NSSelectorFromString(@"alternateIconName");
        UIApplication *application = [UIApplication sharedApplication];
        if (![application respondsToSelector:current])
            return nil;
        return ((NSString *(*)(id, SEL))(void *)objc_msgSend)(application, current);
    }
    // the file, not the dictionary the bundle loaded at launch, which does not change until the app starts again
    NSDictionary *plist = [NSDictionary dictionaryWithContentsOfFile:YZBundlePlistPath()];
    NSString *name = [plist objectForKey:YZIconNameKey];
    return [name isKindOfClass:[NSString class]] ? name : nil;
}

static void YZKeepIconChoice(NSString *name) {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:name ? name : @"" forKey:YZIconChoiceKey];
    // SpringBoard starts again right after and takes the app with it, so the value is written out at once
    [defaults synchronize];
}

// the size of a png from its header, without decoding it
static BOOL YZPNGSize(NSString *path, uint32_t *width, uint32_t *height) {
    NSFileHandle *handle = [NSFileHandle fileHandleForReadingAtPath:path];
    NSData *head = [handle readDataOfLength:24];
    [handle closeFile];
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if ([head length] < 24 || memcmp([head bytes], signature, 8) != 0)
        return NO;
    const uint8_t *b = [head bytes];
    *width = (uint32_t)b[16] << 24 | (uint32_t)b[17] << 16 | (uint32_t)b[18] << 8 | b[19];
    *height = (uint32_t)b[20] << 24 | (uint32_t)b[21] << 16 | (uint32_t)b[22] << 8 | b[23];
    return YES;
}

// an install from a user ipa on ios 7 and older: uicache rebuilds the entries of system apps but leaves the icon
// names of such an app in the installation cache, so the icon files themselves are written over the primary names
static BOOL YZUsesCachedUserIcons(void) {
    NSString *bundle = [[[NSBundle mainBundle] bundlePath] stringByResolvingSymlinksInPath];
    return ([bundle hasPrefix:@"/private/var/mobile/Applications/"] || [bundle hasPrefix:@"/var/mobile/Applications/"]) &&
        [[NSFileManager defaultManager] fileExistsAtPath:@"/var/mobile/Library/Caches/com.apple.mobile.installation.plist"];
}

static NSError *YZWriteUserIconFiles(NSString *name) {
    NSString *bundle = [[NSBundle mainBundle] bundlePath];
    NSString *originals = [bundle stringByAppendingPathComponent:YZOriginalIconsFolder];
    NSError *error = nil;
    NSArray *files = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:originals error:&error];
    if (![files count])
        return error ? error : YZIconFailure(9, @"the original app icons are missing");
    NSDictionary *plist = [NSDictionary dictionaryWithContentsOfFile:YZBundlePlistPath()];
    NSDictionary *saved = [plist objectForKey:YZIconOriginalKey];
    NSDictionary *source = saved ? saved : plist;
    NSMutableArray *alternates = [NSMutableArray array];
    if (name) {
        for (NSString *key in [NSArray arrayWithObjects:@"CFBundleIcons", @"CFBundleIcons~ipad", nil]) {
            NSDictionary *icons = [source objectForKey:key];
            NSDictionary *icon = [[icons objectForKey:@"CFBundleAlternateIcons"] objectForKey:name];
            NSArray *paths = [icon objectForKey:@"CFBundleIconFiles"];
            if ([paths isKindOfClass:[NSArray class]])
                [alternates addObjectsFromArray:paths];
        }
        if (![alternates count])
            return YZIconFailure(9, @"the selected icon has no files");
    }
    for (NSString *file in files) {
        if (![file hasPrefix:@"Icon"] || ![file hasSuffix:@".png"] || ![[file lastPathComponent] isEqualToString:file])
            return YZIconFailure(9, @"the original icon list is invalid");
        NSString *original = [originals stringByAppendingPathComponent:file];
        NSData *data = nil;
        if (!name) {
            data = [NSData dataWithContentsOfFile:original];
        } else {
            uint32_t width = 0, height = 0;
            if (!YZPNGSize(original, &width, &height) || !width || width != height || width > 1024)
                return YZIconFailure(9, @"an original app icon is invalid");
            // the alternate file of the size nearest to the one it replaces, already with its corners
            NSString *best = nil;
            uint32_t bestDistance = UINT32_MAX;
            for (id candidate in alternates) {
                if (![candidate isKindOfClass:[NSString class]] || ![[candidate lastPathComponent] isEqualToString:candidate])
                    continue;
                NSString *path = [bundle stringByAppendingPathComponent:candidate];
                uint32_t w = 0, h = 0;
                if (!YZPNGSize(path, &w, &h) || !w || w != h)
                    continue;
                uint32_t distance = w > width ? w - width : width - w;
                if (distance < bestDistance) {
                    best = path;
                    bestDistance = distance;
                }
            }
            UIImage *image = best ? [UIImage imageWithContentsOfFile:best] : nil;
            if (!image)
                return YZIconFailure(9, @"the selected icon could not be read");
            UIGraphicsBeginImageContextWithOptions(CGSizeMake(width, height), NO, 1);
            [image drawInRect:CGRectMake(0, 0, width, height)];
            UIImage *resized = UIGraphicsGetImageFromCurrentImageContext();
            data = resized ? UIImagePNGRepresentation(resized) : nil;
            UIGraphicsEndImageContext();
        }
        if (!data)
            return YZIconFailure(9, @"the app icon could not be prepared");
        NSError *written = YZReplaceFile(data, [bundle stringByAppendingPathComponent:file]);
        if (written)
            return written;
    }
    return nil;
}

static NSError *YZRewriteSelectedIcon(NSString *name) {
    if (!YZUsesCachedUserIcons())
        return YZRewriteIconPlist(name);
    NSString *previous = [[YZAlternateIconName() copy] autorelease];
    NSData *previousPlist = [NSData dataWithContentsOfFile:YZBundlePlistPath()];
    if (!previousPlist)
        return YZIconFailure(1, @"the Info.plist could not be read");
    NSError *error = YZWriteUserIconFiles(name);
    if (!error) {
        // the plist keeps the packaged icon names here, only the name of the choice is added
        error = YZRewriteIconPlist(nil);
        if (!error && name) {
            NSMutableDictionary *plist = YZReadBundlePlist(&error);
            if (!plist)
                error = error ? error : YZIconFailure(1, @"the Info.plist could not be read");
            else {
                [plist setObject:name forKey:YZIconNameKey];
                error = YZWriteBundlePlist(plist);
            }
        }
    }
    if (error) {
        NSError *rollback = YZWriteUserIconFiles(previous);
        if (rollback)
            NSLog(@"yozora: the previous app icon files could not be restored: %@", rollback);
        NSError *plistError = YZReplaceFile(previousPlist, YZBundlePlistPath());
        if (plistError)
            NSLog(@"yozora: the previous app icon plist could not be restored: %@", plistError);
    }
    return error;
}

void YZAppIconRestore(void) {
    if (YZAppIconCurrentMode() != YZAppIconRewrite)
        return;
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSString *shown = YZAlternateIconName();
    id kept = [defaults objectForKey:YZIconChoiceKey];
    NSString *wanted = shown;
    if (![kept isKindOfClass:[NSString class]]) {
        if (shown)
            YZKeepIconChoice(shown);
    } else {
        wanted = [(NSString *)kept length] ? kept : nil;
        if (wanted && ![YZAlternateIconNames() containsObject:wanted]) {
            // an icon this version no longer has
            [defaults removeObjectForKey:YZIconChoiceKey];
            [defaults synchronize];
            wanted = shown;
        }
    }
    if (wanted == shown || [wanted isEqualToString:shown])
        return;
    NSString *chosen = [[wanted copy] autorelease];
    dispatch_async(YZIconQueue(), ^{
        @autoreleasepool {
            NSError *error = YZRewriteSelectedIcon(chosen);
            if (error) {
                NSLog(@"yozora: the chosen icon was not put back: %@", error);
                return;
            }
            error = YZRefreshHomeScreen();
            if (error)
                NSLog(@"yozora: the home screen did not read the icon again: %@", error);
        }
    });
}

static void YZFinishIcon(void (^done)(NSError *), NSError *error) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (done)
            done(error);
        [done release];
    });
}

void YZRewriteAppIcon(NSString *name, void (^completion)(NSError *)) {
    void (^done)(NSError *) = [completion copy];
    if (name && ![YZAlternateIconNames() containsObject:name]) {
        YZFinishIcon(done, YZIconFailure(7, @"the selected icon is absent from this app bundle"));
        return;
    }
    if (!YZAppIconCanRewrite()) {
        YZFinishIcon(done, YZIconFailure(4, @"the app bundle or the home screen cache cannot be updated"));
        return;
    }
    NSString *chosen = [[name copy] autorelease];
    NSString *previous = [[YZAlternateIconName() copy] autorelease];
    dispatch_async(YZIconQueue(), ^{
        @autoreleasepool {
            NSError *error = YZRewriteSelectedIcon(chosen);
            if (!error)
                YZKeepIconChoice(chosen);
            if (!error) {
                error = YZRefreshHomeScreen();
                if (error) {
                    NSError *rollback = YZRewriteSelectedIcon(previous);
                    if (rollback) {
                        NSLog(@"yozora: the previous icon could not be restored: %@", rollback);
                    } else {
                        YZKeepIconChoice(previous);
                        char *cache[] = {"uicache", NULL};
                        NSError *refresh = YZRunProgram("/usr/bin/uicache", cache, YES);
                        if (refresh)
                            NSLog(@"yozora: the restored icon cache was not refreshed: %@", refresh);
                    }
                }
            }
            YZFinishIcon(done, error);
        }
    });
}

void YZSetAlternateIcon(NSString *name, void (^completion)(NSError *)) {
    void (^done)(NSError *) = [completion copy];
    YZAppIconMode mode = YZAppIconCurrentMode();
    if (mode == YZAppIconRewrite) {
        [done release];
        YZRewriteAppIcon(name, completion);
        return;
    }
    if (mode != YZAppIconNative) {
        YZFinishIcon(done, YZIconFailure(4, @"this system cannot change the icon"));
        return;
    }
    UIApplication *application = [UIApplication sharedApplication];
    SEL set = NSSelectorFromString(@"setAlternateIconName:completionHandler:");
    ((void (*)(id, SEL, NSString *, void (^)(NSError *)))(void *)objc_msgSend)(application, set, name, ^(NSError *error) {
        if (!error)
            YZKeepIconChoice(name);
        YZFinishIcon(done, error);
    });
}

NSArray *YZAlternateIconNames(void) {
    NSDictionary *icons = [[[NSBundle mainBundle] infoDictionary] objectForKey:@"CFBundleIcons"];
    NSDictionary *alternates = [icons objectForKey:@"CFBundleAlternateIcons"];
    return [[alternates allKeys] sortedArrayUsingSelector:@selector(compare:)];
}
