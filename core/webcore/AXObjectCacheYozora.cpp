#include "config.h"
#include "AXObjectCache.h"
#include "AXNotifications.h"
#include "AccessibilityObject.h"
#include "Chrome.h"
#include "ChromeClient.h"
#include "DocumentPage.h"
#include "LocalFrame.h"
#include "NodeDocument.h"
#include "Page.h"
#include "RenderView.h"

namespace WebCore {

void AXObjectCache::attachWrapper(AccessibilityObject& object)
{
    auto wrapper = adoptRef(*new AccessibilityObjectWrapper(object));
    object.setWrapper(wrapper.ptr());
}

void AXObjectCache::detachWrapper(AXCoreObject* object, AccessibilityDetachmentType type)
{
    if (RefPtr protectedObject = object)
        protectedObject->detachWrapper(type);
}

void AXObjectCache::postPlatformNotification(AccessibilityObject& object, AXNotification notification)
{
    Ref protectedObject { object };
    if (auto* wrapper = object.wrapper())
        wrapper->didChange();
    if (RefPtr page = object.page())
        page->chrome().client().postAccessibilityNotification(object, notification);
}

void AXObjectCache::nodeTextChangePlatformNotification(AccessibilityObject* object, AXTextChange change, unsigned offset, const String& text)
{
    RefPtr protectedObject = object;
    if (!protectedObject)
        return;
    if (auto* wrapper = protectedObject->wrapper())
        wrapper->didChange();
    if (RefPtr page = protectedObject->page())
        page->chrome().client().postAccessibilityNodeTextChangeNotification(protectedObject.get(), change, offset, text);
}

void AXObjectCache::frameLoadingEventPlatformNotification(RenderView* view, AXLoadingEvent event)
{
    if (!view)
        return;
    RefPtr object = getOrCreate(*view);
    if (!object)
        return;
    if (auto* wrapper = object->wrapper())
        wrapper->didChange();
    if (RefPtr page = object->page())
        page->chrome().client().postAccessibilityFrameLoadingEventNotification(object.get(), event);
}

void AXObjectCache::handleScrolledToAnchor(const Node& node)
{
    if (RefPtr object = AccessibilityObject::firstAccessibleObjectFromNode(&node))
        postPlatformNotification(*object, AXNotification::ScrolledToAnchor);
}

void AXObjectCache::platformHandleFocusedUIElementChanged(AccessibilityObject* oldFocus, AccessibilityObject* newFocus)
{
    RefPtr protectedOldFocus = oldFocus;
    RefPtr protectedNewFocus = newFocus;
    WeakPtr cache { *this };
    if (protectedOldFocus)
        postPlatformNotification(*protectedOldFocus, AXNotification::FocusedUIElementChanged);
    // a native focus callback may close the page and destroy this cache
    if (cache && protectedNewFocus && protectedNewFocus != protectedOldFocus)
        postPlatformNotification(*protectedNewFocus, AXNotification::FocusedUIElementChanged);
}

void AXObjectCache::platformPerformDeferredCacheUpdate()
{
    if (RefPtr root = rootWebArea())
        postPlatformNotification(*root, AXNotification::LayoutComplete);
}

}
