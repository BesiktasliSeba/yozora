#import <UIKit/UIKit.h>

/* shown while the browser is made, which takes a moment on an old phone: the launch screen violet (storyboard
   and launch images carry it too, so nothing flashes), the sticker that wobbles in and the name. it becomes a
   cover on the window when the browser is ready and fades away */
@interface YZSplashController : UIViewController {
    UIImageView *_hero;
    UIImageView *_name;
    BOOL _played;
    // where the view stood under the root view controller, the cover keeps it
    CGRect _shownFrame;
}
/* the color of the launch screen, set in the nib of app/LaunchScreen.storyboardc as 88, 30, 118 */
+ (UIColor *)launchColor;
/* the sticker and the name come in. called when the splash is on the window and again by the system when the
   screen appeared, the second call changes nothing */
- (void)play;
/* the splash lies over the window; it fades and leaves, the block runs when it is gone. with reduced motion it leaves at once */
- (void)fadeOutOverWindow:(UIWindow *)window completion:(void (^)(void))completion;
@end
