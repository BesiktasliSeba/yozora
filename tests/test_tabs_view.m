#import "YZHomeView.h"
#import "YZStore.h"
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import "YZTabsView.h"
#import "YZTab.h"
#import "YZErrorPage.h"
#include <assert.h>
#include <string.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>

static NSArray *cards(YZTabsView *tray) {
    NSMutableArray *result = [NSMutableArray array];
    for (UIView *container in tray.subviews) {
        if (![container isKindOfClass:[UIScrollView class]])
            continue;
        for (UIView *view in container.subviews) {
            NSUInteger buttons = 0;
            for (UIView *child in view.subviews)
                buttons += [child isKindOfClass:[UIButton class]];
            if (buttons == 2)
                [result addObject:view];
        }
    }
    return result;
}

static UIView *child_of_class(UIView *view, Class type) {
    for (UIView *child in view.subviews) {
        if ([child isKindOfClass:type])
            return child;
    }
    return nil;
}

@interface YZErrorPageTestTarget : NSObject {
@public
    BOOL retried;
}
- (void)retry;
@end
@implementation YZErrorPageTestTarget
- (void)retry { retried = YES; }
@end

static void test_error_page(void) {
    YZErrorPageTestTarget *target = [[YZErrorPageTestTarget alloc] init];
    YZErrorPage *page = [[YZErrorPage alloc] initWithTarget:target action:@selector(retry)];
    page.frame = CGRectMake(0, 0, 320, 300);
    [page showError:YOZORA_PAGE_ERROR_INTERRUPTED address:@"https://example.test/path" reason:@"connection reset"];
    [page layoutIfNeeded];
    assert(page.contentSize.height > page.bounds.size.height);
    UIButton *retry = (UIButton *)child_of_class(page, [UIButton class]);
    assert(retry && retry.frame.size.height == 44);
    [retry sendActionsForControlEvents:UIControlEventTouchUpInside];
    assert(target->retried);
    [page applyTheme];
    page.frame = CGRectMake(0, 0, 480, 300);
    [page layoutIfNeeded];
    assert(isfinite(page.contentSize.height));
    [page release];
    [target release];
}

@interface YZTrayTestDelegate : NSObject <YZTabsViewDelegate> {
@public
    NSMutableArray *tabs;
    YZTab *selected;
    YZTab *closed;
    BOOL finished;
}
@end
@implementation YZTrayTestDelegate
- (void)tabsView:(YZTabsView *)view didSelectIndex:(NSUInteger)index {
    (void)view;
    [selected release];
    selected = [[tabs objectAtIndex:index] retain];
}
- (void)tabsView:(YZTabsView *)view didCloseIndex:(NSUInteger)index {
    [closed release];
    closed = [[tabs objectAtIndex:index] retain];
    [tabs removeObjectAtIndex:index];
    [view showTabs:tabs selected:0];
}
- (void)tabsViewDidRequestNewTab:(YZTabsView *)view {
    [tabs addObject:[[[YZTab alloc] init] autorelease]];
    [view showTabs:tabs selected:[tabs count] - 1];
}
- (void)tabsViewDidFinish:(YZTabsView *)view { (void)view; finished = YES; }
- (void)dealloc {
    [tabs release];
    [selected release];
    [closed release];
    [super dealloc];
}
@end

@interface YZHomeTestDelegate : NSObject <YZHomeViewDelegate> {
@public
    NSString *selectedURL;
}
@end
@implementation YZHomeTestDelegate
- (void)homeView:(YZHomeView *)view didSelectURL:(NSString *)url {
    (void)view;
    [selectedURL release];
    selectedURL = [url copy];
}
- (void)dealloc {
    [selectedURL release];
    [super dealloc];
}
@end

static NSArray *home_tiles(YZHomeView *home) {
    NSMutableArray *tiles = [NSMutableArray array];
    for (UIView *view in home.subviews) {
        if ([view isKindOfClass:[UIButton class]])
            [tiles addObject:view];
    }
    return tiles;
}

static void test_home(void) {
    YZHomeView *home = [[YZHomeView alloc] initWithFrame:CGRectMake(0, 0, 320, 460)];
    YZHomeTestDelegate *owner = [[YZHomeTestDelegate alloc] init];
    home.homeDelegate = owner;
    NSMutableArray *sites = [NSMutableArray array];
    for (unsigned i = 0; i < 12; ++i) {
        YZRecord *record = [[[YZRecord alloc] init] autorelease];
        record.url = [NSString stringWithFormat:@"https://site%u.example/", i];
        record.title = [NSString stringWithFormat:@"site %u", i];
        [sites addObject:record];
    }
    [home showSites:sites bookmarks:[NSArray array]];
    [home layoutIfNeeded];
    NSArray *original = [home_tiles(home) copy];
    assert([original count] == 8);
    for (unsigned i = 0; i < 200; ++i) {
        NSAutoreleasePool *iteration = [[NSAutoreleasePool alloc] init];
        home.frame = CGRectMake(0, 0, i % 2 ? 320 : 480, 460);
        [home setNeedsLayout];
        [home layoutIfNeeded];
        assert([home_tiles(home) isEqualToArray:original]);
        [iteration drain];
    }
    [home applyTheme];
    [home layoutIfNeeded];
    assert([home_tiles(home) isEqualToArray:original]);
    [sites exchangeObjectAtIndex:0 withObjectAtIndex:1];
    YZRecord *first = [sites objectAtIndex:0];
    first.title = @"changed home title";
    [home showSites:sites bookmarks:[NSArray arrayWithObject:[sites lastObject]]];
    [home layoutIfNeeded];
    NSArray *updated = home_tiles(home);
    assert([updated count] == 9);
    UIButton *button = [updated objectAtIndex:0];
    assert(button == [original objectAtIndex:0]);
    BOOL foundTitle = NO;
    for (UIView *child in button.subviews) {
        if ([child isKindOfClass:[UILabel class]] && [((UILabel *)child).text isEqualToString:first.title])
            foundTitle = YES;
    }
    assert(foundTitle);
    [button sendActionsForControlEvents:UIControlEventTouchUpInside];
    assert([owner->selectedURL isEqualToString:first.url]);
    [home showSites:sites bookmarks:sites];
    [home layoutIfNeeded];
    assert([home_tiles(home) count] == 16);
    [home showSites:[NSArray arrayWithObject:first] bookmarks:nil];
    [home layoutIfNeeded];
    assert([home_tiles(home) count] == 1);
    assert([home_tiles(home) objectAtIndex:0] == button);
    [home showSites:nil bookmarks:nil];
    [home layoutIfNeeded];
    assert([home_tiles(home) count] == 2);
    [home setContentOffset:CGPointMake(0, 20) animated:NO];
    [home layoutIfNeeded];
    assert([home_tiles(home) count] == 2);
    assert(isfinite(home.contentSize.height) && home.contentSize.height > 0);
    printf("home: bounded reuse, 200 resizes, theme, changed title, reordered target, removal and defaults passed\n");
    [original release];
    home.homeDelegate = nil;
    [owner release];
    [home release];
}

static int run_tests(int argc, char **argv) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    BOOL benchmark = (argc == 2 || argc == 3) && !strcmp(argv[1], "--benchmark");
    [UIView setAnimationsEnabled:NO];
    if (!benchmark) {
        test_home();
        test_error_page();
    }
    YZTrayTestDelegate *owner = [[YZTrayTestDelegate alloc] init];
    owner->tabs = [[NSMutableArray alloc] init];
    for (NSUInteger i = 0; i < 6; ++i) {
        YZTab *tab = [[[YZTab alloc] init] autorelease];
        tab.title = [NSString stringWithFormat:@"test tab %lu", (unsigned long)i];
        [owner->tabs addObject:tab];
    }
    Class trayClass = benchmark && argc == 3 ? NSClassFromString([NSString stringWithUTF8String:argv[2]]) : [YZTabsView class];
    assert(trayClass && [trayClass isSubclassOfClass:[UIView class]]);
    YZTabsView *tray = [[trayClass alloc] initWithFrame:CGRectMake(0, 0, 320, 460)];
    tray.delegate = owner;
    [tray showTabs:owner->tabs selected:1];
    assert([cards(tray) count] == 6);
    mach_timebase_info_data_t timebase;
    assert(mach_timebase_info(&timebase) == KERN_SUCCESS);
    uint64_t began = mach_absolute_time();
    unsigned replaced = 0;
    for (unsigned pass = 0; pass < 200; ++pass) {
        NSAutoreleasePool *iteration = [[NSAutoreleasePool alloc] init];
        UIView *previous = [[cards(tray) objectAtIndex:0] retain];
        [tray setNeedsLayout];
        [tray layoutIfNeeded];
        replaced += [cards(tray) objectAtIndex:0] != previous;
        [previous release];
        [iteration drain];
    }
    double ms = (mach_absolute_time() - began) * (double)timebase.numer / timebase.denom / 1000000.0;
    printf("tray layout: 200 passes %.3f ms, first card replaced %u times\n", ms, replaced);
    if (!benchmark) {
        assert(replaced == 0);
        NSArray *before = [cards(tray) copy];
        tray.frame = CGRectMake(0, 0, 480, 300);
        [tray setNeedsLayout];
        [tray layoutIfNeeded];
        NSArray *after = cards(tray);
        for (NSUInteger i = 0; i < [before count]; ++i)
            assert([before objectAtIndex:i] == [after objectAtIndex:i]);
        [tray applyTheme];
        [tray layoutIfNeeded];
        assert([cards(tray) objectAtIndex:0] == [before objectAtIndex:0]);
        YZTab *tab = [owner->tabs objectAtIndex:1];
        tab.title = @"changed title";
        UIGraphicsBeginImageContext(CGSizeMake(8, 8));
        [[UIColor redColor] setFill];
        UIRectFill(CGRectMake(0, 0, 8, 8));
        tab.thumbnail = UIGraphicsGetImageFromCurrentImageContext();
        UIGraphicsEndImageContext();
        [tray showTabs:owner->tabs selected:1];
        UIView *card = [before objectAtIndex:1];
        assert([((UILabel *)child_of_class(card, [UILabel class])).text isEqualToString:tab.title]);
        UIImageView *picture = (UIImageView *)child_of_class(card, [UIImageView class]);
        assert(picture.image == tab.thumbnail);
        assert(picture.contentMode == UIViewContentModeScaleAspectFit);
        assert(CGRectEqualToRect(picture.layer.contentsRect, CGRectMake(0, 0, 1, 1)));
        tab.thumbnail = nil;
        [tray refreshThumbnails];
        assert(picture.image == nil);
        [owner->tabs removeObjectAtIndex:0];
        [tray showTabs:owner->tabs selected:0];
        UIButton *open = (UIButton *)child_of_class(card, [UIButton class]);
        [open sendActionsForControlEvents:UIControlEventTouchUpInside];
        assert(owner->selected == tab);
        for (UIView *button in card.subviews) {
            if ([button isKindOfClass:[UIButton class]] && button != open)
                [(UIButton *)button sendActionsForControlEvents:UIControlEventTouchUpInside];
        }
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:2];
        while (!owner->closed && [deadline timeIntervalSinceNow] > 0)
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:deadline];
        assert(owner->closed == tab && [owner->tabs indexOfObjectIdenticalTo:tab] == NSNotFound);
        assert([cards(tray) count] == 4);
        [before release];
        puts("tray views: reuse, resize, theme, title, discarded thumbnail and tab identity passed");
    }
    tray.delegate = nil;
    [tray release];
    [owner release];
    [pool drain];
    return 0;
}

@interface YZUITestApplicationDelegate : NSObject <UIApplicationDelegate>
@end
@implementation YZUITestApplicationDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application;
    (void)options;
    // ios 6 initializes its font services during UIApplicationMain, before the test views exist
    [self performSelector:@selector(runTests) withObject:nil afterDelay:0];
    return YES;
}
- (void)runTests {
    NSString *path = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Caches/Yozora-UI-Tests.log"];
    FILE *log = fopen([path fileSystemRepresentation], "w");
    if (!log) { perror("ui test log"); exit(1); }
    int descriptor = fileno(log);
    if (dup2(descriptor, STDOUT_FILENO) < 0 || dup2(descriptor, STDERR_FILENO) < 0) {
        perror("ui test output");
        fclose(log);
        exit(1);
    }
    fclose(log);
    setvbuf(stdout, NULL, _IONBF, 0);
    NSString *fixturePath = [[NSBundle mainBundle] pathForResource:@"YZUITest" ofType:@"plist"];
    NSDictionary *fixture = fixturePath ? [NSDictionary dictionaryWithContentsOfFile:fixturePath] : nil;
    NSArray *arguments = [fixture objectForKey:@"arguments"];
    assert(!arguments || ([arguments isKindOfClass:[NSArray class]] && [arguments count] <= 2));
    char *argv[4] = { "yozora-ui-tests", NULL, NULL, NULL };
    for (NSUInteger i = 0; i < [arguments count]; ++i) {
        NSString *argument = [arguments objectAtIndex:i];
        assert([argument isKindOfClass:[NSString class]] && [argument length] <= 64);
        argv[i + 1] = (char *)[argument UTF8String];
    }
    int result = run_tests((int)[arguments count] + 1, argv);
    printf("ui tests completed %d\n", result);
    exit(result);
}
@end

int main(int argc, char **argv) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    int result = UIApplicationMain(argc, argv, nil, @"YZUITestApplicationDelegate");
    [pool drain];
    return result;
}
