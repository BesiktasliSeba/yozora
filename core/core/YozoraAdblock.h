#pragma once

#include "CachedResource.h"
#include "yozora_filter.h"
#include <wtf/URL.h>

namespace WebCore {
class Document;
class LocalFrame;
class Page;
}

// the core's ad blocker: shipped rules (core/yozora_filter.c, core/yozora_blocklist.c) and the person's
// (yozora_adblock_set_rules), paused sites are in YozoraSites. it runs on the core thread, the rule set and
// counters may be replaced and read from the interface thread
namespace YozoraAdblock {

yozora_filter_type typeOf(WebCore::CachedResource::Type, bool mainFrame);

// whether a page request is stopped. firstParty is the address of the top document the request belongs to. a
// stopped request is counted for the process and for the frame's page
bool blocks(WebCore::Page*, yozora_filter_type, const WTF::URL& request, const WTF::URL& firstParty);
bool blocksWebSocket(WebCore::Document&, const WTF::URL& request);

// a main frame document was committed: the style sheet that hides ad slots for its site replaces the page's
// previous one
void documentCommitted(WebCore::Page&, const WTF::URL& document);

void setEnabled(bool);
bool enabled();
unsigned long long blockedTotal();

}
