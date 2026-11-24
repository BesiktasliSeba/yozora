#import <UIKit/UIKit.h>

/* the card that picks the DNS server pages are resolved through. the modern design draws its own card: one field that is checked as it is typed,
   with the address the core will ask under it and no list of servers. the flat and the classic design ask with the card of the app (the alert
   of the system on ios 5 to 7), the way the other questions of those designs are asked, with the address in a field */
@interface YZDnsPopup : NSObject
/* shows the card in host. `current` is the DNS over HTTPS address in use (@"" for the system). done gets the address the person chose, @"" for the
   system, or is not called when the card is cancelled. the address is already normalized by the core */
+ (void)presentInView:(UIView *)host current:(NSString *)current done:(void (^)(NSString *url))done;
@end
