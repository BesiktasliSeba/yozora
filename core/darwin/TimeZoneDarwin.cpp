#include "config.h"

#include <CoreFoundation/CFNotificationCenter.h>
#include <CoreFoundation/CFTimeZone.h>
#include <mutex>
#include <wtf/Assertions.h>
#include <wtf/TimeZone.h>

namespace WTF {

static void time_zone_changed(CFNotificationCenterRef, void*, CFStringRef, const void*, CFDictionaryRef)
{
    timeZoneDidChange();
}

void listenForTimeZoneChangeNotifications()
{
    /* the process owns this observer, date caches may outlive individual contexts */
    static std::once_flag once;
    std::call_once(once, [] {
        auto center = CFNotificationCenterGetLocalCenter();
        auto name = kCFTimeZoneSystemTimeZoneDidChangeNotification;
        RELEASE_ASSERT(center && name);
        CFNotificationCenterAddObserver(center, &once, time_zone_changed, name, nullptr,
            CFNotificationSuspensionBehaviorDeliverImmediately);
    });
}

}
