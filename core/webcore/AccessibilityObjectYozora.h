#pragma once

#include <WebCore/AXID.h>
#include <wtf/RefCounted.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
class AXCoreObject;
}

class AccessibilityObjectWrapper final : public RefCounted<AccessibilityObjectWrapper> {
public:
    explicit AccessibilityObjectWrapper(WebCore::AXCoreObject&);
    ~AccessibilityObjectWrapper();

    WebCore::AXID objectID() const { return m_id; }
    uint64_t revision() const { return m_revision; }
    bool isDetached() const;
    String label() const;
    bool press();
    void didChange();
    void detach();

private:
    WeakPtr<WebCore::AXCoreObject> m_object;
    const WebCore::AXID m_id;
    uint64_t m_revision { 1 };
};
