#import "YZSystemSheet.h"
#import <objc/message.h>

id YZSystemSheetMake(NSArray *titles, NSString *cancelTitle, id delegate) {
    Class sheetClass = NSClassFromString(@"UIActionSheet");
    if (!sheetClass)
        return nil;
    SEL initializer = NSSelectorFromString(@"initWithTitle:delegate:cancelButtonTitle:destructiveButtonTitle:otherButtonTitles:");
    id sheet = ((id (*)(id, SEL, NSString *, id, NSString *, NSString *, NSString *))objc_msgSend)([sheetClass alloc], initializer, nil, delegate, nil, nil, nil);
    SEL add = NSSelectorFromString(@"addButtonWithTitle:");
    for (NSString *title in titles)
        ((NSInteger (*)(id, SEL, NSString *))objc_msgSend)(sheet, add, title);
    NSInteger cancel = ((NSInteger (*)(id, SEL, NSString *))objc_msgSend)(sheet, add, cancelTitle);
    ((void (*)(id, SEL, NSInteger))objc_msgSend)(sheet, NSSelectorFromString(@"setCancelButtonIndex:"), cancel);
    return sheet;
}

void YZSystemSheetShow(id sheet, UIView *host, CGRect rect) {
    if (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad)
        ((void (*)(id, SEL, CGRect, UIView *, BOOL))objc_msgSend)(sheet, NSSelectorFromString(@"showFromRect:inView:animated:"), rect, host, YES);
    else
        ((void (*)(id, SEL, UIView *))objc_msgSend)(sheet, NSSelectorFromString(@"showInView:"), host);
}

void YZSystemSheetDismiss(id sheet) {
    ((void (*)(id, SEL, id))objc_msgSend)(sheet, NSSelectorFromString(@"setDelegate:"), nil);
    ((void (*)(id, SEL, NSInteger, BOOL))objc_msgSend)(sheet, NSSelectorFromString(@"dismissWithClickedButtonIndex:animated:"), 0, NO);
}
