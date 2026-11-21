#import "YZSignIn.h"
#import "YZStrings.h"
#include <string.h>
#include "yozora_address.h"
#include "yozora_core.h"

// the buffer the cookie header is read into, a longer one is reported as too large and never cut
#define YZSignInCookieCap 16384

@implementation YZSignIn
@synthesize finishing = _finishing;

+ (YZSignIn *)signInForLink:(NSURL *)link status:(yozora_signin_status *)status {
    YZSignIn *signIn = [[[YZSignIn alloc] init] autorelease];
    yozora_signin_status result = yozora_signin_parse([[link absoluteString] UTF8String], &signIn->_request);
    if (status)
        *status = result;
    return result == YOZORA_SIGNIN_OK ? signIn : nil;
}

+ (NSString *)reasonForStatus:(yozora_signin_status)status {
    switch (status) {
    case YOZORA_SIGNIN_BAD_URL: return YZT(@"Запрос входа: неверный адрес сайта");
    case YOZORA_SIGNIN_BAD_CALLBACK: return YZT(@"Запрос входа: неверный адрес возврата");
    case YOZORA_SIGNIN_NO_END: return YZT(@"Запрос входа: не сказано, чем он заканчивается");
    case YOZORA_SIGNIN_BAD_END: return YZT(@"Запрос входа: неверное условие окончания");
    default: return YZT(@"Запрос входа не понят");
    }
}

- (NSString *)startURL { return [NSString stringWithUTF8String:_request.url]; }

- (NSString *)callbackScheme {
    const char *colon = strchr(_request.callback, ':');
    return [[[NSString alloc] initWithBytes:_request.callback length:(NSUInteger)(colon - _request.callback) encoding:NSUTF8StringEncoding] autorelease];
}

- (NSString *)host {
    char host[256];
    return yozora_address_host(_request.url, host, sizeof host) ? [NSString stringWithUTF8String:host] : [self startURL];
}

- (BOOL)watchesCookie { return _request.cookie[0] != '\0'; }

- (BOOL)finishedAtAddress:(NSString *)address cookies:(NSString *)cookies {
    return yozora_signin_finished(&_request, [address UTF8String], [cookies UTF8String]) != 0;
}

- (BOOL)shouldPollAt:(CFTimeInterval)now {
    if (_polling || now - _polledAt < 1.0)
        return NO;
    _polledAt = now;
    _polling = YES;
    return YES;
}

- (void)readCookiesForAddress:(NSString *)address completion:(void (^)(NSString *cookies, BOOL tooLarge))completion {
    NSString *url = [[address copy] autorelease];
    void (^done)(NSString *, BOOL) = [[completion copy] autorelease];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        // the strings made here live until the main queue block has copied them
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        char header[YZSignInCookieCap];
        int length = yozora_cookie_header([url UTF8String], header, sizeof header);
        NSString *cookies = length > 0 ? [NSString stringWithUTF8String:header] : @"";
        BOOL tooLarge = length < 0;
        dispatch_async(dispatch_get_main_queue(), ^{
            _polling = NO;
            done(cookies, tooLarge);
        });
        [pool drain];
    });
}

- (NSURL *)resultURLForOutcome:(yozora_signin_outcome)outcome address:(NSString *)address cookies:(NSString *)cookies {
    char out[YOZORA_SIGNIN_RESULT_CAP];
    const char *page = [address UTF8String];
    size_t length = yozora_signin_result(&_request, outcome, page, [cookies UTF8String], out, sizeof out);
    // cookies that do not fit the answer are not cut: the caller is told they were too many
    if (!length && outcome == YOZORA_SIGNIN_DONE)
        length = yozora_signin_result(&_request, YOZORA_SIGNIN_TOO_LARGE, page, NULL, out, sizeof out);
    return length ? [NSURL URLWithString:[NSString stringWithUTF8String:out]] : nil;
}
@end
