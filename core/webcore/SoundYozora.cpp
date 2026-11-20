#include "config.h"
#include <pal/system/Sound.h>

#include <wtf/Assertions.h>

namespace PAL {

void systemBeep()
{
    // headless editing has no audio device, keep rejected actions observable
    WTFLogAlways("yozora: editor requested an alert sound");
}

}
