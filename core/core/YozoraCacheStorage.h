#pragma once

#include <wtf/Ref.h>

namespace WebCore {
class CacheStorageProvider;
}

namespace YozoraCacheStorage {

// the provider a page configuration takes for window.caches and the caches of its workers: the profile's, or while
// the private session runs one of its own. both keep their caches in memory for the life of the app
Ref<WebCore::CacheStorageProvider> provider();
// core thread only: deletes the caches of every site
void clear();
// core thread only: deletes the caches of the private session
void clearPrivate();

}
