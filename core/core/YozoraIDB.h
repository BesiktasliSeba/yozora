#pragma once

#include <wtf/Ref.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
class DatabaseProvider;
}

namespace YozoraIDB {

// the folder site databases live in, read when the first page asks for one. without it they stay in memory
// until the app ends
void setDirectory(const String&);
// the provider a page configuration takes: the profile's, or while the private session runs the one with databases in memory
Ref<WebCore::DatabaseProvider> provider();
// core thread only, open requests and connections may hold locks while a document is suspended
bool hasOpenDatabases();
// deletes the databases of every site
void clear();
// deletes the databases of the private session
void clearPrivate();


}
