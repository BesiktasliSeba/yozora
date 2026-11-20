#ifndef YOZORA_ADDRESS_H
#define YOZORA_ADDRESS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YOZORA_SEARCH_GOOGLE,
    YOZORA_SEARCH_DUCKDUCKGO,
    YOZORA_SEARCH_YANDEX
} yozora_search_engine;

typedef enum {
    YOZORA_ADDRESS_URL,
    YOZORA_ADDRESS_SEARCH,
    YOZORA_ADDRESS_INVALID
} yozora_address_kind;

/* turns address bar text into a page or search address of the given core, out is empty on INVALID. an unknown
   core searches with Google. a result that does not fit capacity is INVALID, never cut */
yozora_address_kind yozora_address_resolve(const char *input, yozora_search_engine core, char *out, size_t capacity);
/* writes the host of an http or https address without a leading www.; returns 0 when there is none */
int yozora_address_host(const char *url, char *out, size_t capacity);

typedef enum {
    /* the page keeps the address */
    YOZORA_EXTERNAL_NONE,
    /* a scheme another app registers (tg:, mailto:, tel:, itms-apps:), no page can show it */
    YOZORA_EXTERNAL_SCHEME,
    /* an http or https address of another site, which an installed app may claim as a universal link; the
       page loads it when no app does */
    YOZORA_EXTERNAL_UNIVERSAL
} yozora_external_kind;

/* decides whether a navigation of the page at page_url (empty or NULL when there is none) should be offered to
   another app first. schemes a page must never hand out (javascript, data, file, blob, about, view-source,
   ws, wss, ftp, filesystem, itms-services), malformed schemes, control bytes and credentials in the address
   are NONE. an address on the host of the page, ignoring a leading www., stays in the page */
yozora_external_kind yozora_address_external(const char *url, const char *page_url);

#ifdef __cplusplus
}
#endif
#endif
