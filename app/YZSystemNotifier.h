#import <Foundation/Foundation.h>

/* what the system shows for downloads outside the page: a notification, the number on the icon and the status
   bar spinner. what the oldest and newest systems lack is looked up at run time */
@interface YZSystemNotifier : NSObject
/* ios 8 and later show notifications and the icon number only after permission, asked once per run, older
   systems need no question */
+ (void)askPermission;
/* a notification now, with the sound of the system. a text that is empty is not shown */
+ (void)post:(NSString *)text;
/* the number on the icon of the app, 0 clears it */
+ (void)setBadge:(NSInteger)count;
/* the spinner in the status bar, turned by the system for as long as it is on */
+ (void)setActivity:(BOOL)active;
@end
