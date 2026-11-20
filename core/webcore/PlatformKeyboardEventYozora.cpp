#include "config.h"
#include "PlatformKeyboardEvent.h"

#if PLATFORM(YOZORA)

namespace WebCore {

// the embedder sends RawKeyDown and Char itself, a plain KeyDown is split the way the Windows port does
void PlatformKeyboardEvent::disambiguateKeyDownEvent(Type type, bool)
{
    ASSERT(m_type == Type::KeyDown);
    ASSERT(type == Type::RawKeyDown || type == Type::Char);
    m_type = type;
    if (type == Type::RawKeyDown) {
        m_text = String();
        m_unmodifiedText = String();
    } else {
        m_keyIdentifier = String();
        m_windowsVirtualKeyCode = 0;
    }
}

}

#endif
