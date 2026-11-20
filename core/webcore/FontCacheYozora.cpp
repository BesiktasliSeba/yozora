#include "config.h"
#include "FontManagerYozora.h"

#include <cstdio>
#include <wtf/Lock.h>
#include <wtf/NeverDestroyed.h>

namespace WebCore {

static Lock font_lock;
static NeverDestroyed<sk_sp<SkFontMgr>> font_manager WTF_GUARDED_BY_LOCK(font_lock);

bool initializeYozoraFonts(const char* directory, char* error, size_t capacity)
{
    Locker locker { font_lock };
    if (font_manager.get()) {
        if (error && capacity)
            snprintf(error, capacity, "font catalog is already initialized");
        return false;
    }
    auto manager = createYozoraFontManager(directory, error, capacity);
    if (!manager)
        return false;
    // all WebCore font caches share one immutable catalog selected before page creation
    font_manager.get() = std::move(manager);
    return true;
}

sk_sp<SkFontMgr> yozoraFontManager()
{
    Locker locker { font_lock };
    return font_manager.get();
}

}
