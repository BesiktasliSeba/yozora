#include "config.h"
#include <pal/text/KillRing.h>

#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>

namespace PAL {

WTF_MAKE_TZONE_ALLOCATED_IMPL(KillRing);

static constexpr unsigned text_limit = 65536;

void KillRing::append(const String& text)
{
    if (text.isEmpty())
        return;
    if (text.length() > text_limit || (!m_newSequence && m_text.length() > text_limit - text.length())) {
        WTFLogAlways("yozora kill ring: text exceeds 65536 UTF-16 units");
        return;
    }
    m_text = m_newSequence ? text : makeString(m_text, text);
    m_newSequence = false;
}

void KillRing::prepend(const String& text)
{
    if (text.isEmpty())
        return;
    if (text.length() > text_limit || (!m_newSequence && m_text.length() > text_limit - text.length())) {
        WTFLogAlways("yozora kill ring: text exceeds 65536 UTF-16 units");
        return;
    }
    m_text = m_newSequence ? text : makeString(text, m_text);
    m_newSequence = false;
}

String KillRing::yank() { return m_text; }
void KillRing::startNewSequence() { m_newSequence = true; }
void KillRing::setToYankedState() { m_newSequence = true; }

}
