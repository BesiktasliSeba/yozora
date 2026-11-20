#import "YZStreamProxy.h"
#include "yozora_stream_proxy.h"

// the three calls of the proxy for a local stream, over the object of core/yozora_hls.h that the stream is
static int YZLocalRead(void *context, const char *name, uint64_t offset, uint8_t *out, size_t capacity, size_t *got, uint64_t *total, const char **contentType) {
    return (int)yozora_hls_read((yozora_hls *)context, name, offset, out, capacity, got, total, contentType);
}
static void YZLocalRetain(void *context) { yozora_hls_retain((yozora_hls *)context); }
static void YZLocalRelease(void *context) { yozora_hls_release((yozora_hls *)context); }

@implementation YZStreamProxy {
    yozora_proxy *_proxy;
    unsigned _generation;
}

+ (YZStreamProxy *)shared {
    static YZStreamProxy *shared = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ shared = [[YZStreamProxy alloc] init]; });
    return shared;
}

- (BOOL)ensureRunning {
    if (_proxy && yozora_proxy_alive(_proxy))
        return YES;
    if (_proxy) {
        NSLog(@"yozora: the stream proxy lost its socket, starting another");
        yozora_proxy_stop(_proxy);
        _proxy = NULL;
    }
    NSString *roots = [[NSBundle mainBundle] pathForResource:@"ca-bundle" ofType:@"crt"];
    if (!roots) {
        NSLog(@"yozora: the root certificates are missing from the bundle, the stream proxy cannot start");
        return NO;
    }
    yozora_proxy_config config;
    config.ca_bundle = [roots fileSystemRepresentation];
    config.user_agent = yozora_user_agent();
    yozora_proxy_status status = yozora_proxy_start(&config, &_proxy);
    if (status != YOZORA_PROXY_OK) {
        NSLog(@"yozora: the stream proxy did not start (%d)", (int)status);
        _proxy = NULL;
        return NO;
    }
    ++_generation;
    return YES;
}

- (NSURL *)proxiedURLForRequest:(const yozora_media_request *)request stream:(unsigned *)stream generation:(unsigned *)generation {
    @synchronized(self) {
        if (![self ensureRunning])
            return nil;
        char local[8192];
        unsigned registered = 0;
        yozora_proxy_status status = yozora_proxy_register(_proxy, request->url, request->cookie, request->referer, local, sizeof local, &registered);
        if (status != YOZORA_PROXY_OK) {
            NSLog(@"yozora: the stream proxy refused a stream (%d)", (int)status);
            return nil;
        }
        if (stream)
            *stream = registered;
        if (generation)
            *generation = _generation;
        return [NSURL URLWithString:[NSString stringWithUTF8String:local]];
    }
}

- (NSURL *)localURLForMediaID:(unsigned)mediaID core:(yozora_core *)core stream:(unsigned *)stream generation:(unsigned *)generation {
    // the core hands over a reference: the proxy keeps it for the stream, and it is let go here when the proxy takes none
    yozora_hls *hls = yozora_core_mse_stream(core, mediaID);
    if (!hls)
        return nil;
    @synchronized(self) {
        if (![self ensureRunning]) {
            yozora_hls_release(hls);
            return nil;
        }
        char local[256];
        unsigned registered = 0;
        yozora_proxy_status status = yozora_proxy_register_local(_proxy, YZLocalRead, YZLocalRetain, YZLocalRelease, hls, local, sizeof local, &registered);
        if (status != YOZORA_PROXY_OK) {
            NSLog(@"yozora: the stream proxy refused a local stream (%d)", (int)status);
            yozora_hls_release(hls);
            return nil;
        }
        if (stream)
            *stream = registered;
        if (generation)
            *generation = _generation;
        return [NSURL URLWithString:[[NSString stringWithUTF8String:local] stringByAppendingString:@YOZORA_HLS_NAME_PLAYLIST]];
    }
}

- (void)releaseStream:(unsigned)stream generation:(unsigned)generation {
    @synchronized(self) {
        if (_proxy && generation == _generation)
            yozora_proxy_release(_proxy, stream);
    }
}

@end
