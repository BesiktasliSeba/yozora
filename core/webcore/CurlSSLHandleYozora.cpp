#include "config.h"
#include "CurlSSLHandle.h"

#if USE(CURL)

namespace WebCore {

void CurlSSLHandle::platformInitialize()
{
    // the ios 5 trust store is stale, the embedder sets its own root file before the first request
}

}

#endif
