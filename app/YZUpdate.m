#import "YZUpdate.h"
#import "YZStreamProxy.h"
#import "YZStrings.h"
#import <CommonCrypto/CommonDigest.h>
#include <string.h>
#include <unistd.h>
#include "yozora_release.h"

NSString *const YZUpdaterChangedNotification = @"YZUpdaterChanged";
NSString *const YZReleasePageAddress = @"https://github.com/sqmrak/yozora/releases/latest";

static NSString *const YZReleaseApiAddress = @"https://api.github.com/repos/sqmrak/yozora/releases/latest";
static NSString *const YZReleaseFeedAddress = @"https://github.com/sqmrak/yozora/releases.atom";
static NSString *const YZReleaseCacheKey = @"yozora.update.release";
static const unsigned long long YZUpdateLimit = 400ull * 1024 * 1024;

@implementation YZRelease
@synthesize tag, notes, ipaAddress, digestAddress, size;
- (void)dealloc {
    [tag release];
    [notes release];
    [ipaAddress release];
    [digestAddress release];
    [super dealloc];
}
@end

static void YZFetch(NSString *address, NSUInteger limit, void (^done)(NSData *data, NSInteger status)) {
    unsigned stream = 0, generation = 0;
    NSURL *url = [[YZStreamProxy shared] proxiedURLForAddress:address stream:&stream generation:&generation];
    if (!url) {
        done(nil, 0);
        return;
    }
    void (^finish)(NSData *, NSInteger) = [done copy];
    NSURLRequest *request = [NSURLRequest requestWithURL:url cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:25];
    [NSURLConnection sendAsynchronousRequest:request queue:[NSOperationQueue mainQueue] completionHandler:^(NSURLResponse *response, NSData *data, NSError *error) {
        [[YZStreamProxy shared] releaseStream:stream generation:generation];
        NSInteger status = [response isKindOfClass:[NSHTTPURLResponse class]] ? [(NSHTTPURLResponse *)response statusCode] : 0;
        if (error || [data length] > limit)
            data = nil;
        finish(data, status);
        [finish release];
    }];
}

static YZRelease *YZReleaseFromRaw(const yozora_release *raw) {
    YZRelease *release = [[[YZRelease alloc] init] autorelease];
    release.tag = [NSString stringWithUTF8String:raw->tag];
    release.notes = [NSString stringWithUTF8String:raw->notes];
    release.ipaAddress = [NSString stringWithUTF8String:raw->ipa_url];
    release.digestAddress = [NSString stringWithUTF8String:raw->sha_url];
    release.size = raw->ipa_size;
    return release;
}

static void YZKeepRelease(YZRelease *release) {
    NSDictionary *stored = [NSDictionary dictionaryWithObjectsAndKeys:release.tag, @"tag", release.notes, @"notes", release.ipaAddress, @"ipa",
                            release.digestAddress, @"digest", [NSNumber numberWithUnsignedLongLong:release.size], @"size", nil];
    [[NSUserDefaults standardUserDefaults] setObject:stored forKey:YZReleaseCacheKey];
}

static BOOL YZChecking;

@implementation YZUpdate

+ (NSString *)installedVersion {
    NSString *version = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    return [version length] ? version : @"?";
}

+ (YZRelease *)cachedRelease {
    NSDictionary *stored = [[NSUserDefaults standardUserDefaults] dictionaryForKey:YZReleaseCacheKey];
    NSString *tag = [stored objectForKey:@"tag"];
    if (![tag isKindOfClass:[NSString class]] || ![tag length])
        return nil;
    YZRelease *release = [[[YZRelease alloc] init] autorelease];
    release.tag = tag;
    release.notes = [[stored objectForKey:@"notes"] isKindOfClass:[NSString class]] ? [stored objectForKey:@"notes"] : @"";
    release.ipaAddress = [[stored objectForKey:@"ipa"] isKindOfClass:[NSString class]] ? [stored objectForKey:@"ipa"] : @"";
    release.digestAddress = [[stored objectForKey:@"digest"] isKindOfClass:[NSString class]] ? [stored objectForKey:@"digest"] : @"";
    release.size = [[stored objectForKey:@"size"] unsignedLongLongValue];
    return release;
}

+ (BOOL)isNewer:(YZRelease *)release {
    int order = 0;
    return release && yozora_version_compare([[self installedVersion] UTF8String], [release.tag UTF8String], &order) && order < 0;
}

+ (BOOL)canInstallInApp {
    return access("/Library/MobileSubstrate/DynamicLibraries/AppSyncUnified-installd.dylib", R_OK) == 0
        || access("/var/jb/Library/MobileSubstrate/DynamicLibraries/AppSyncUnified-installd.dylib", R_OK) == 0
        || access("/var/jb/usr/lib/TweakInject/AppSyncUnified-installd.dylib", R_OK) == 0;
}

+ (BOOL)checking { return YZChecking; }

+ (NSTimeInterval)lastCheck { return [[NSUserDefaults standardUserDefaults] doubleForKey:@"yozora.update.checked"]; }

+ (void)check:(void (^)(YZRelease *, BOOL))completion {
    void (^done)(YZRelease *, BOOL) = [completion copy];
    YZChecking = YES;
    [[NSNotificationCenter defaultCenter] postNotificationName:YZUpdaterChangedNotification object:nil];
    void (^finish)(YZRelease *) = ^(YZRelease *release) {
        YZChecking = NO;
        if (release) {
            YZKeepRelease(release);
            [[NSUserDefaults standardUserDefaults] setDouble:[NSDate timeIntervalSinceReferenceDate] forKey:@"yozora.update.checked"];
        }
        else
            NSLog(@"yozora: the latest release could not be read");
        if (done)
            done(release ? release : [YZUpdate cachedRelease], release == nil);
        [done release];
        [[NSNotificationCenter defaultCenter] postNotificationName:YZUpdaterChangedNotification object:nil];
    };
    YZFetch(YZReleaseApiAddress, 512 * 1024, ^(NSData *data, NSInteger status) {
        yozora_release raw;
        if (status == 200 && data && yozora_release_parse_json([data bytes], [data length], &raw) == YOZORA_RELEASE_OK) {
            finish(YZReleaseFromRaw(&raw));
            return;
        }
        NSLog(@"yozora: the release api answered %d, reading the releases feed", (int)status);
        YZFetch(YZReleaseFeedAddress, 1024 * 1024, ^(NSData *feed, NSInteger feedStatus) {
            yozora_release fromFeed;
            if (feedStatus == 200 && feed && yozora_release_parse_atom([feed bytes], [feed length], &fromFeed) == YOZORA_RELEASE_OK)
                finish(YZReleaseFromRaw(&fromFeed));
            else {
                NSLog(@"yozora: the releases feed answered %d", (int)feedStatus);
                finish(nil);
            }
        });
    });
}

@end

@interface YZUpdater () <NSURLConnectionDataDelegate> {
    YZUpdaterState _state;
    float _fraction;
    unsigned long long _downloaded;
    unsigned long long _total;
    NSString *_failure;
    YZRelease *_release;
    NSURLConnection *_connection;
    NSFileHandle *_file;
    CC_SHA256_CTX _hash;
    uint8_t _expected[32];
    unsigned _stream;
    unsigned _generation;
    NSTimeInterval _lastNote;
    UIBackgroundTaskIdentifier _background;
    id _opener;
}
@end

static NSString *YZUpdateDirectory(void) {
    return [NSTemporaryDirectory() stringByAppendingPathComponent:@"yozora-update"];
}

static NSString *YZUpdateIpaPath(void) {
    return [YZUpdateDirectory() stringByAppendingPathComponent:@"Yozora.ipa"];
}

@implementation YZUpdater

@synthesize state = _state, fraction = _fraction, downloaded = _downloaded, total = _total, failure = _failure;

+ (YZUpdater *)shared {
    static YZUpdater *shared;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ shared = [[YZUpdater alloc] init]; });
    return shared;
}

- (id)init {
    self = [super init];
    if (self)
        _background = UIBackgroundTaskInvalid;
    return self;
}

- (BOOL)hasFile {
    return (_state == YZUpdaterFailed || _state == YZUpdaterInstalling) && _fraction >= 1 && [[NSFileManager defaultManager] fileExistsAtPath:YZUpdateIpaPath()];
}

- (void)changed {
    [[NSNotificationCenter defaultCenter] postNotificationName:YZUpdaterChangedNotification object:self];
}

- (void)beginBackground {
    [UIApplication sharedApplication].idleTimerDisabled = YES;
    if (_background == UIBackgroundTaskInvalid)
        _background = [[UIApplication sharedApplication] beginBackgroundTaskWithExpirationHandler:^{ [self endBackground]; }];
}

- (void)endBackground {
    [UIApplication sharedApplication].idleTimerDisabled = NO;
    if (_background != UIBackgroundTaskInvalid)
        [[UIApplication sharedApplication] endBackgroundTask:_background];
    _background = UIBackgroundTaskInvalid;
}

- (void)releaseStream {
    if (_stream)
        [[YZStreamProxy shared] releaseStream:_stream generation:_generation];
    _stream = 0;
}

- (void)closeDownload {
    [_connection cancel];
    [_connection release];
    _connection = nil;
    [_file closeFile];
    [_file release];
    _file = nil;
    [self releaseStream];
}

- (void)failWith:(NSString *)text {
    NSLog(@"yozora: update failed: %@", text);
    [self closeDownload];
    [self endBackground];
    [_failure release];
    _failure = [text copy];
    _state = YZUpdaterFailed;
    [self changed];
}

- (void)cancel {
    [self closeDownload];
    [self endBackground];
    [[NSFileManager defaultManager] removeItemAtPath:YZUpdateDirectory() error:NULL];
    _state = YZUpdaterIdle;
    _fraction = 0;
    _downloaded = _total = 0;
    [self changed];
}

- (void)startWithRelease:(YZRelease *)release {
    if (_state == YZUpdaterWorking || _state == YZUpdaterInstalling || !release)
        return;
    [_release release];
    _release = [release retain];
    _fraction = 0;
    _downloaded = 0;
    _total = release.size;
    [_failure release];
    _failure = nil;
    _state = YZUpdaterWorking;
    [self beginBackground];
    [self changed];
    if (![release.ipaAddress length] || ![release.digestAddress length]) {
        [self failWith:YZT(@"У релиза нет файла или контрольной суммы")];
        return;
    }
    [[NSFileManager defaultManager] removeItemAtPath:YZUpdateDirectory() error:NULL];
    YZFetch(release.digestAddress, 1024, ^(NSData *data, NSInteger status) {
        if (_state != YZUpdaterWorking || _release != release)
            return;
        if (status != 200 || !data || !yozora_release_parse_sha256([data bytes], [data length], _expected)) {
            [self failWith:YZT(@"Не удалось получить контрольную сумму релиза")];
            return;
        }
        [self download];
    });
}

- (void)download {
    NSFileManager *files = [NSFileManager defaultManager];
    NSError *error = nil;
    if (![files createDirectoryAtPath:YZUpdateDirectory() withIntermediateDirectories:YES attributes:nil error:&error] ||
        ![files createFileAtPath:YZUpdateIpaPath() contents:nil attributes:nil]) {
        [self failWith:YZT(@"Не удалось создать файл обновления")];
        return;
    }
    _file = [[NSFileHandle fileHandleForWritingAtPath:YZUpdateIpaPath()] retain];
    NSURL *url = [[YZStreamProxy shared] proxiedURLForAddress:_release.ipaAddress stream:&_stream generation:&_generation];
    if (!_file || !url) {
        [self failWith:YZT(@"Не удалось начать загрузку")];
        return;
    }
    CC_SHA256_Init(&_hash);
    _connection = [[NSURLConnection alloc] initWithRequest:[NSURLRequest requestWithURL:url cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:60] delegate:self];
}

- (void)connection:(NSURLConnection *)connection didReceiveResponse:(NSURLResponse *)response {
    NSInteger status = [response isKindOfClass:[NSHTTPURLResponse class]] ? [(NSHTTPURLResponse *)response statusCode] : 0;
    if (status != 200) {
        [self failWith:[NSString stringWithFormat:YZT(@"Сервер ответил %d"), (int)status]];
        return;
    }
    long long expected = [response expectedContentLength];
    if (expected > 0)
        _total = (unsigned long long)expected;
    if (_total > YZUpdateLimit)
        [self failWith:YZT(@"Файл обновления слишком большой")];
}

- (void)connection:(NSURLConnection *)connection didReceiveData:(NSData *)data {
    if (connection != _connection)
        return;
    if (_downloaded + [data length] > YZUpdateLimit) {
        [self failWith:YZT(@"Файл обновления слишком большой")];
        return;
    }
    @try {
        [_file writeData:data];
    } @catch (NSException *exception) {
        [self failWith:YZT(@"Не хватает места на телефоне")];
        return;
    }
    CC_SHA256_Update(&_hash, [data bytes], (CC_LONG)[data length]);
    _downloaded += [data length];
    if (_total)
        _fraction = MIN(1.0f, (float)((double)_downloaded / (double)_total));
    NSTimeInterval now = [NSDate timeIntervalSinceReferenceDate];
    if (now - _lastNote >= 0.1) {
        _lastNote = now;
        [self changed];
    }
}

- (void)connection:(NSURLConnection *)connection didFailWithError:(NSError *)error {
    if (connection != _connection)
        return;
    NSLog(@"yozora: the update download failed: %@", error);
    [self failWith:YZT(@"Загрузка прервалась")];
}

- (void)connectionDidFinishLoading:(NSURLConnection *)connection {
    if (connection != _connection)
        return;
    uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &_hash);
    BOOL sizeKnown = _release.size > 0;
    if (sizeKnown && _downloaded != _release.size) {
        [self failWith:YZT(@"Файл скачался не полностью")];
        return;
    }
    if (memcmp(digest, _expected, sizeof digest) != 0) {
        [[NSFileManager defaultManager] removeItemAtPath:YZUpdateDirectory() error:NULL];
        [self failWith:YZT(@"Контрольная сумма не совпала, файл не установлен")];
        return;
    }
    [self closeDownload];
    _fraction = 1;
    [self install];
}

- (NSString *)escaped:(NSString *)text {
    NSString *encoded = (NSString *)CFURLCreateStringByAddingPercentEscapes(NULL, (CFStringRef)text, NULL, CFSTR("!*'();:@&=+$,/?#[]"), kCFStringEncodingUTF8);
    return [encoded autorelease];
}

- (void)install {
    if (_state == YZUpdaterInstalling || !_release || ![[NSFileManager defaultManager] fileExistsAtPath:YZUpdateIpaPath()])
        return;
    [self releaseStream];
    NSString *manifestPath = [YZUpdateDirectory() stringByAppendingPathComponent:@"manifest.plist"];
    NSDictionary *files = [NSDictionary dictionaryWithObjectsAndKeys:YZUpdateIpaPath(), @"Yozora.ipa", manifestPath, @"manifest.plist", nil];
    NSURL *base = [[YZStreamProxy shared] localURLForFiles:files stream:&_stream generation:&_generation];
    if (!base) {
        [self failWith:YZT(@"Не удалось подготовить установку")];
        return;
    }
    NSString *package = [[base absoluteString] stringByAppendingString:@"Yozora.ipa"];
    NSString *version = [_release.tag hasPrefix:@"v"] ? [_release.tag substringFromIndex:1] : _release.tag;
    NSDictionary *asset = [NSDictionary dictionaryWithObjectsAndKeys:@"software-package", @"kind", package, @"url", nil];
    NSDictionary *metadata = [NSDictionary dictionaryWithObjectsAndKeys:[[NSBundle mainBundle] bundleIdentifier], @"bundle-identifier", version, @"bundle-version",
                              @"software", @"kind", @"Yozora", @"title", nil];
    NSDictionary *item = [NSDictionary dictionaryWithObjectsAndKeys:[NSArray arrayWithObject:asset], @"assets", metadata, @"metadata", nil];
    NSDictionary *manifest = [NSDictionary dictionaryWithObject:[NSArray arrayWithObject:item] forKey:@"items"];
    NSData *xml = [NSPropertyListSerialization dataWithPropertyList:manifest format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    if (![xml length] || ![xml writeToFile:manifestPath atomically:YES]) {
        [self failWith:YZT(@"Не удалось подготовить установку")];
        return;
    }
    NSString *link = [@"itms-services://?action=download-manifest&url=" stringByAppendingString:[self escaped:[[base absoluteString] stringByAppendingString:@"manifest.plist"]]];
    _state = YZUpdaterInstalling;
    [self changed];
    if (![[UIApplication sharedApplication] openURL:[NSURL URLWithString:link]])
        [self failWith:YZT(@"Система не приняла установку")];
}

- (void)openFileInView:(UIView *)view {
    if (![[NSFileManager defaultManager] fileExistsAtPath:YZUpdateIpaPath()] || !view)
        return;
    [_opener release];
    _opener = [[UIDocumentInteractionController interactionControllerWithURL:[NSURL fileURLWithPath:YZUpdateIpaPath()]] retain];
    CGRect bounds = view.bounds;
    if (![_opener presentOpenInMenuFromRect:CGRectMake(bounds.size.width / 2, bounds.size.height - 1, 1, 1) inView:view animated:YES])
        NSLog(@"yozora: no app takes the update file");
}

@end
