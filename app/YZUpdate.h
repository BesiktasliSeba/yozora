#import <UIKit/UIKit.h>

extern NSString *const YZUpdaterChangedNotification;
extern NSString *const YZReleasePageAddress;

@interface YZRelease : NSObject
@property(nonatomic, copy) NSString *tag;
@property(nonatomic, copy) NSString *notes;
@property(nonatomic, copy) NSString *ipaAddress;
@property(nonatomic, copy) NSString *digestAddress;
@property(nonatomic, assign) unsigned long long size;
@end

/* the latest release from GitHub, kept between runs for the screen offline */
@interface YZUpdate : NSObject
+ (NSString *)installedVersion;
+ (YZRelease *)cachedRelease;
+ (BOOL)isNewer:(YZRelease *)release;
/* AppSync takes an app signed by ldid, a phone without it is sent to the release page */
+ (BOOL)canInstallInApp;
+ (BOOL)checking;
+ (NSTimeInterval)lastCheck;
/* the api first, the releases feed when the api refuses (60 requests an hour), the completion runs on the main thread */
+ (void)check:(void (^)(YZRelease *release, BOOL failed))completion;
@end

typedef enum {
    YZUpdaterIdle = 0,
    YZUpdaterWorking,
    YZUpdaterInstalling,
    YZUpdaterFailed
} YZUpdaterState;

/* downloads the ipa through the stream proxy, checks its sha-256, installs it as App Installer does: a manifest and the file are
   served from the loopback address and itms-services opens the manifest. changes post YZUpdaterChangedNotification */
@interface YZUpdater : NSObject
+ (YZUpdater *)shared;
@property(nonatomic, readonly) YZUpdaterState state;
@property(nonatomic, readonly) float fraction;
@property(nonatomic, readonly) unsigned long long downloaded;
@property(nonatomic, readonly) unsigned long long total;
@property(nonatomic, readonly, copy) NSString *failure;
/* the checked file is on the phone, another app can take it after a failed install */
@property(nonatomic, readonly) BOOL hasFile;
- (void)startWithRelease:(YZRelease *)release;
- (void)install;
- (void)cancel;
- (void)openFileInView:(UIView *)view;
@end
