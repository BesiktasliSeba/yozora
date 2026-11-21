#import <UIKit/UIKit.h>

/* the icon of the app on the home screen. ios 10.3 has a call for it, older systems get it from a bundle the app
   can write: the app rewrites the icon entries of its own Info.plist and runs uicache */

typedef enum {
    /* a system without the call and a bundle the app cannot write: the picker is shown dimmed with the reason */
    YZAppIconUnavailable = 0,
    /* the call of ios 10.3 */
    YZAppIconNative,
    /* the app rewrites its Info.plist, runs uicache and restarts SpringBoard, which takes the app with it */
    YZAppIconRewrite
} YZAppIconMode;

YZAppIconMode YZAppIconCurrentMode(void);
/* the names of the alternate icons the Info.plist lists, in the order the picker shows them */
NSArray *YZAlternateIconNames(void);
/* the name of the alternate icon the home screen shows now, nil for the icon of the app itself */
NSString *YZAlternateIconName(void);
/* name nil goes back to the icon of the app. the completion runs on the main thread with nil, or the error that
   stopped it (ios 10.3 refuses while the app is not in the foreground, the rewrite fails when the plist cannot be
   written or uicache does not start) */
void YZSetAlternateIcon(NSString *name, void (^completion)(NSError *error));
BOOL YZAppIconCanRewrite(void);
void YZRewriteAppIcon(NSString *name, void (^completion)(NSError *error));
/* a new install brings its own Info.plist and with it the icon of the app, so the choice is kept in the user
   defaults too and put back at launch */
void YZAppIconRestore(void);
