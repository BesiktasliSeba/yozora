#import "YZTab.h"

// previews are small pictures in the caches directory, the system may empty it and the tray then shows a
// placeholder
static NSString *YZThumbnailDirectory(void) {
    NSArray *roots = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
    return [roots count] ? [[roots objectAtIndex:0] stringByAppendingPathComponent:@"YozoraThumbnails"] : nil;
}

static NSString *YZThumbnailPath(NSString *identifier) {
    NSString *directory = YZThumbnailDirectory();
    return directory && [identifier length] ? [directory stringByAppendingPathComponent:[identifier stringByAppendingPathExtension:@"jpg"]] : nil;
}

// the files are written and removed one after another off the main thread, the flash of an old phone is slow
static dispatch_queue_t YZThumbnailQueue(void) {
    static dispatch_queue_t queue;
    if (!queue)
        queue = dispatch_queue_create("yozora.thumbnails", DISPATCH_QUEUE_SERIAL);
    return queue;
}

static NSString *YZNewIdentifier(void) {
    CFUUIDRef uuid = CFUUIDCreate(kCFAllocatorDefault);
    NSString *text = (NSString *)CFUUIDCreateString(kCFAllocatorDefault, uuid);
    CFRelease(uuid);
    return [text autorelease];
}

@implementation YZTab
@synthesize translation = _translation, url = _url;
@synthesize title = _title;
@synthesize isPrivate = _isPrivate;
@synthesize desktop = _desktop;
@synthesize scrollY = _scrollY;

- (id)init {
    self = [super init];
    if (!self)
        return nil;
    _identifier = [YZNewIdentifier() copy];
    return self;
}

- (NSString *)identifier { return _identifier; }

- (void)setIdentifier:(NSString *)identifier {
    if (![identifier length] || [identifier isEqualToString:_identifier])
        return;
    [_identifier release];
    _identifier = [identifier copy];
}

- (BOOL)isBlank {
    return ![_url length];
}

- (UIImage *)thumbnail {
    if (!_thumbnail && _thumbnailOnDisk && !_isPrivate) {
        NSString *path = YZThumbnailPath(_identifier);
        UIImage *stored = path ? [UIImage imageWithContentsOfFile:path] : nil;
        if (stored)
            _thumbnail = [stored retain];
        else
            _thumbnailOnDisk = NO;
    }
    return _thumbnail;
}

- (void)setThumbnail:(UIImage *)thumbnail {
    if (thumbnail == _thumbnail)
        return;
    [thumbnail retain];
    [_thumbnail release];
    _thumbnail = thumbnail;
    if (!thumbnail || _isPrivate) {
        _thumbnailOnDisk = NO;
        return;
    }
    _thumbnailOnDisk = YES;
    NSString *path = YZThumbnailPath(_identifier);
    NSData *data = path ? UIImageJPEGRepresentation(thumbnail, 0.82f) : nil;
    if (!data)
        return;
    // the block keeps the path and the bytes until the file is written
    dispatch_async(YZThumbnailQueue(), ^{
        NSFileManager *files = [NSFileManager defaultManager];
        NSString *directory = [path stringByDeletingLastPathComponent];
        NSDictionary *mode = [NSDictionary dictionaryWithObject:[NSNumber numberWithUnsignedLong:0700] forKey:NSFilePosixPermissions];
        NSError *failure = nil;
        if (![files createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:mode error:&failure]
            || ![data writeToFile:path options:NSDataWritingAtomic error:&failure])
            NSLog(@"yozora: tab preview not stored: %@", failure);
    });
}

- (void)dropThumbnailFromMemory {
    [_thumbnail release];
    _thumbnail = nil;
}

- (void)noteStoredThumbnail {
    NSString *path = YZThumbnailPath(_identifier);
    if (!_isPrivate && path && [[NSFileManager defaultManager] fileExistsAtPath:path])
        _thumbnailOnDisk = YES;
}

- (void)removeStoredThumbnail {
    _thumbnailOnDisk = NO;
    NSString *path = YZThumbnailPath(_identifier);
    if (!path)
        return;
    dispatch_async(YZThumbnailQueue(), ^{
        // the file may not exist, a private tab never had one
        [[NSFileManager defaultManager] removeItemAtPath:path error:NULL];
    });
}

+ (void)removeStoredThumbnailsExcept:(NSArray *)identifiers {
    NSString *directory = YZThumbnailDirectory();
    NSSet *keep = [NSSet setWithArray:identifiers];
    if (!directory)
        return;
    dispatch_async(YZThumbnailQueue(), ^{
        NSFileManager *files = [NSFileManager defaultManager];
        for (NSString *name in [files contentsOfDirectoryAtPath:directory error:NULL]) {
            if ([[name pathExtension] isEqualToString:@"jpg"] && ![keep containsObject:[name stringByDeletingPathExtension]])
                [files removeItemAtPath:[directory stringByAppendingPathComponent:name] error:NULL];
        }
    });
}

- (void)keepSuspendedCore:(yozora_core *)core {
    [self discardSuspendedCore];
    _suspendedCore = core;
}

- (yozora_core *)takeSuspendedCore {
    yozora_core *core = _suspendedCore;
    _suspendedCore = NULL;
    return core;
}

- (void)discardSuspendedCore {
    yozora_core *core = [self takeSuspendedCore];
    if (core) {
        // the translation works on that core: it is told before the core goes
        [_translation invalidate];
        _translation.delegate = nil;
        [self setTranslation:nil];
    }
    yozora_core_destroy_async(core);
}

- (void)dealloc {
    [self discardSuspendedCore];
    [_translation invalidate];
    _translation.delegate = nil;
    [_translation release];
    [_identifier release];
    [_url release];
    [_title release];
    [_thumbnail release];
    [super dealloc];
}
@end
