#ifndef YOZORA_ICONS_H
#define YOZORA_ICONS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YOZORA_ICON_URL_MAX 2048
#define YOZORA_ICON_LINKS_MAX 8
/* the part of a page that is read: icons are declared in the head, a page that has not said it by then does not */
#define YOZORA_ICON_SCAN_MAX (256 * 1024)

typedef struct {
    char url[YOZORA_ICON_URL_MAX];
    /* the largest side the page declares for it, 0 when it says nothing */
    unsigned size;
    /* an apple-touch-icon, which is a picture meant for a home screen and not a 16 pixel favicon */
    int touch;
} yozora_icon_link;

/* the icons that the head of an http or https page declares with <link rel="icon">, "shortcut icon",
   "apple-touch-icon" and "apple-touch-icon-precomposed", best first (the largest, a home screen picture when
   sizes are equal). addresses are resolved against base and an address appears once. vector icons (svg), data:
   addresses, a mask-icon and tags past the bound are left out. writes at most capacity entries and returns how
   many, 0 for an empty page or a bad argument */
size_t yozora_icon_links(const char *html, size_t length, const char *base, yozora_icon_link *out, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
