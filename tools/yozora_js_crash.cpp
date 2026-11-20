#include "yozora_crash.h"

#if defined(__APPLE__)
// ios does not produce crash reports for command-line probes launched over ssh
__attribute__((constructor)) static void yozora_js_install_crash_handler()
{
    YozoraCrash::install();
}
#endif
