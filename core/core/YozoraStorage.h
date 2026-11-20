#pragma once

#include "StorageNamespaceProvider.h"
#include <wtf/Ref.h>
#include <wtf/RefPtr.h>

namespace YozoraStorage {

// one provider for each page: the session storage lives in it, local storage is shared by every page of the process
Ref<WebCore::StorageNamespaceProvider> createProvider();
// writes local storage to its file now when something changed since the last write
void flush();
// forgets all local storage, the file included
void clear();
// forgets the local storage of the private session
void clearPrivate();
// the file local storage is kept in, nothing is persisted without one
void setFile(const char* path);

}
