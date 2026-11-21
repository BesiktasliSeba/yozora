#ifndef YOZORA_ICON_ENVIRONMENT_H
#define YOZORA_ICON_ENVIRONMENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the environment uicache runs with: the one of the app, with the home of the app's sandbox replaced by the one of
   mobile. the uicache of ios 7 and later reads HOME to find the installation cache, and in the sandbox of the app
   it finds none. source is a null terminated list, out gets pointers into it and into string literals, and a null
   at the end. returns 0, with out undefined, when a list does not fit capacity or an argument is missing */
int yozora_icon_cache_environment(char *const *source, char **out, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
