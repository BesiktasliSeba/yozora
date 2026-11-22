#import <UIKit/UIKit.h>

/* the action sheet of the system, for the classic design (ios 5 to 7): a column of buttons that rises from the bottom of
   an iPhone and stands in a popover on an iPad. UIActionSheet is reached by name and selector, the sdk that builds the
   arm64 slice marks it deprecated and no later system runs this code. the delegate gets
   actionSheet:clickedButtonAtIndex:, the last button is the cancel button */

/* a sheet with one button for each title and a cancel button after them, nil when the system has no UIActionSheet. the caller
   owns it and sets its delegate to nil before letting go */
id YZSystemSheetMake(NSArray *titles, NSString *cancelTitle, id delegate);
/* shows it from the bottom of host, or from rect on an iPad */
void YZSystemSheetShow(id sheet, UIView *host, CGRect rect);
/* takes it away without an answer, the delegate is not called */
void YZSystemSheetDismiss(id sheet);
