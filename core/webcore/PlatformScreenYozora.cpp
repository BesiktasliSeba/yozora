#include "config.h"
#include "PlatformScreen.h"

#include "DestinationColorSpace.h"
#include "FloatRect.h"

namespace WebCore {

static ScreenData primary_screen()
{
    Ref screen = PlatformScreen::singleton();
    auto* data = screen->screenData(screen->primaryScreenDisplayID());
    RELEASE_ASSERT_WITH_MESSAGE(data, "yozora: configure screen properties before creating a page");
    return *data;
}

int screenDepth(Widget*) { return primary_screen().screenDepth; }
int screenDepthPerComponent(Widget*) { return primary_screen().screenDepthPerComponent; }
bool screenIsMonochrome(Widget*) { return primary_screen().screenDepth == 1; }
bool screenHasInvertedColors() { return primary_screen().screenHasInvertedColors; }
FloatRect screenRect(Widget*) { return primary_screen().screenRect; }
FloatRect screenAvailableRect(Widget*) { return primary_screen().screenAvailableRect; }
bool screenSupportsExtendedColor(Widget*) { return primary_screen().screenSupportsExtendedColor; }
DestinationColorSpace screenColorSpace(Widget*) { return primary_screen().colorSpace; }

}
