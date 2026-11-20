#include "config.h"
#include "AccessibilityObjectYozora.h"
#include "AccessibilityObject.h"
#include <limits>
#include <wtf/MainThread.h>

AccessibilityObjectWrapper::AccessibilityObjectWrapper(WebCore::AXCoreObject& object)
    : m_object(object)
    , m_id(object.objectID())
{
    RELEASE_ASSERT(isMainThread());
}

AccessibilityObjectWrapper::~AccessibilityObjectWrapper() = default;

bool AccessibilityObjectWrapper::isDetached() const
{
    RELEASE_ASSERT(isMainThread());
    return !m_object;
}

String AccessibilityObjectWrapper::label() const
{
    RELEASE_ASSERT(isMainThread());
    RefPtr object = m_object.get();
    if (!object)
        return { };
    auto label = object->title();
    if (label.isEmpty())
        label = object->description();
    return label.left(1024);
}

bool AccessibilityObjectWrapper::press()
{
    RELEASE_ASSERT(isMainThread());
    // pressing may run script which removes the element and its wrapper
    Ref protectedThis { *this };
    RefPtr object = m_object.get();
    if (!object || !object->isEnabled() || !object->supportsPressAction())
        return false;
    return object->press();
}

void AccessibilityObjectWrapper::didChange()
{
    RELEASE_ASSERT(isMainThread());
    RELEASE_ASSERT(m_revision != std::numeric_limits<uint64_t>::max());
    ++m_revision;
}

void AccessibilityObjectWrapper::detach()
{
    RELEASE_ASSERT(isMainThread());
    m_object = nullptr;
    didChange();
}

namespace WebCore {

void AccessibilityObject::detachPlatformWrapper(AccessibilityDetachmentType)
{
    if (RefPtr retainedWrapper = wrapper())
        retainedWrapper->detach();
}

bool AccessibilityObject::accessibilityIgnoreAttachment() const
{
    // yozora exposes embedded documents through their DOM accessibility trees
    return false;
}

AccessibilityObjectInclusion AccessibilityObject::accessibilityPlatformIncludesObject() const
{
    // DOM and ARIA rules determine inclusion before native UI filtering
    return AccessibilityObjectInclusion::DefaultBehavior;
}

}
