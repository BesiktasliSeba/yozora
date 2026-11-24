#include "config.h"
#include "SystemFontDatabase.h"

#include "WebKitFontFamilyNames.h"

namespace WebCore {

auto SystemFontDatabase::platformSystemFontShorthandInfo(FontShorthand shorthand) -> SystemFontShorthandInfo
{
    float size = 16;
    switch (shorthand) {
    case FontShorthand::SmallCaption:
    case FontShorthand::WebkitMiniControl:
        size = 11;
        break;
    case FontShorthand::WebkitSmallControl:
        size = 13;
        break;
    default:
        break;
    }
    // the catalog resolves this generic family without borrowing device font APIs
    return { WebKitFontFamilyNames::sansSerifFamily, size, normalWeightValue() };
}

void SystemFontDatabase::platformInvalidate()
{
    // invalidate() already clears shorthand entries, the font catalog is immutable
}

}
