#import "YZSystemNotifier.h"
#import <UIKit/UIKit.h>
#import <objc/message.h>

@implementation YZSystemNotifier

+ (void)askPermission {
    static BOOL asked;
    if (asked)
        return;
    asked = YES;
    UIApplication *application = [UIApplication sharedApplication];
    Class settingsClass = NSClassFromString(@"UIUserNotificationSettings");
    SEL factory = NSSelectorFromString(@"settingsForTypes:categories:");
    SEL registration = NSSelectorFromString(@"registerUserNotificationSettings:");
    // the armv7 sdk has neither the class nor the selector
    if (!settingsClass || ![settingsClass respondsToSelector:factory] || ![application respondsToSelector:registration])
        return;
    // badge, sound and alert
    id settings = ((id (*)(id, SEL, NSUInteger, id))(void *)objc_msgSend)(settingsClass, factory, (1 << 0) | (1 << 1) | (1 << 2), nil);
    [application performSelector:registration withObject:settings];
}

+ (void)post:(NSString *)text {
    if (![text length])
        return;
    UIApplication *application = [UIApplication sharedApplication];
    if (![application respondsToSelector:@selector(presentLocalNotificationNow:)])
        return;
    UILocalNotification *note = [[[UILocalNotification alloc] init] autorelease];
    note.alertBody = text;
    note.soundName = UILocalNotificationDefaultSoundName;
    [application presentLocalNotificationNow:note];
}

+ (void)setBadge:(NSInteger)count {
    UIApplication *application = [UIApplication sharedApplication];
    if (application.applicationIconBadgeNumber != count)
        application.applicationIconBadgeNumber = count;
}

+ (void)setActivity:(BOOL)active {
    UIApplication *application = [UIApplication sharedApplication];
    if (application.networkActivityIndicatorVisible != active)
        application.networkActivityIndicatorVisible = active;
}

@end
