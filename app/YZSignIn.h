#import <Foundation/Foundation.h>
#include "yozora_signin.h"

/* a sign in another app asked for with a yozora-auth link (core/yozora_signin.h). the browser opens the
   address in a private tab, asks this object whether the sign in ended and sends the answer back to the app
   that asked */
@interface YZSignIn : NSObject {
    yozora_signin_request _request;
    BOOL _finishing;
    BOOL _polling;
    CFTimeInterval _polledAt;
}
/* nil when the link is not a valid request, status tells why */
+ (YZSignIn *)signInForLink:(NSURL *)link status:(yozora_signin_status *)status;
/* what to tell the person about a refused link */
+ (NSString *)reasonForStatus:(yozora_signin_status)status;
@property(nonatomic, readonly) NSString *startURL;
/* the scheme the answer goes to, for the question the person is asked */
@property(nonatomic, readonly) NSString *callbackScheme;
@property(nonatomic, readonly) NSString *host;
/* the answer is being made: the end was seen and nothing else may end this sign in */
@property(nonatomic, assign) BOOL finishing;
/* the sign in ends at a cookie, so the cookies of the page are read now and then */
- (BOOL)watchesCookie;
/* the end condition holds for the address or the cookie header (nil when it is not known) */
- (BOOL)finishedAtAddress:(NSString *)address cookies:(NSString *)cookies;
/* true at most once a second and never while a read is still going, so a slow core thread is not asked twice */
- (BOOL)shouldPollAt:(CFTimeInterval)now;
/* the cookies the page at address would send, read off the main thread (the core thread may be inside a
   script). the completion runs on the main thread, tooLarge when the header did not fit what an answer holds */
- (void)readCookiesForAddress:(NSString *)address completion:(void (^)(NSString *cookies, BOOL tooLarge))completion;
/* the link that goes back to the app, nil when it cannot be made */
- (NSURL *)resultURLForOutcome:(yozora_signin_outcome)outcome address:(NSString *)address cookies:(NSString *)cookies;
@end
