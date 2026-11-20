#pragma once

#include <wtf/text/WTFString.h>

namespace WebCore {
class ResourceRequest;
}

namespace YozoraDownloads {

// core thread only: queues the request address as a download into the directory the interface set. false when
// the address is no download
bool start(const WebCore::ResourceRequest&, const String& suggestedName);

}
