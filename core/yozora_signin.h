#ifndef YOZORA_SIGNIN_H
#define YOZORA_SIGNIN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* another app asks Yozora to sign a person in on a site: yozora-auth://start?url=...&callback=...&until=...
   &cookie=...&state=... (every value percent-encoded). Yozora opens url in a private tab and, once the
   end condition holds, opens callback with the answer appended */

#define YOZORA_SIGNIN_URL_CAP 2048
#define YOZORA_SIGNIN_CALLBACK_CAP 256
#define YOZORA_SIGNIN_COOKIE_NAME_CAP 128
#define YOZORA_SIGNIN_STATE_CAP 256
/* the longest answer the caller should allow for, a larger one is refused by yozora_signin_result */
#define YOZORA_SIGNIN_RESULT_CAP 32768

typedef enum {
    YOZORA_SIGNIN_OK,
    /* the address is not a yozora-auth one, another handler may take it */
    YOZORA_SIGNIN_NOT_SIGNIN,
    /* wrong path, a repeated or oversized field, a bad escape or a control byte */
    YOZORA_SIGNIN_MALFORMED,
    /* url is missing, not http or https, has credentials, or already satisfies until */
    YOZORA_SIGNIN_BAD_URL,
    /* callback is missing, has no scheme, a fragment, or a scheme that a page or Yozora itself owns */
    YOZORA_SIGNIN_BAD_CALLBACK,
    /* neither until nor cookie, nothing would end the sign in */
    YOZORA_SIGNIN_NO_END,
    /* until is not an http or https address, or cookie is not a cookie name */
    YOZORA_SIGNIN_BAD_END
} yozora_signin_status;

typedef struct {
    char url[YOZORA_SIGNIN_URL_CAP];
    char callback[YOZORA_SIGNIN_CALLBACK_CAP];
    /* the sign in ends when the page address starts with this, empty for no such condition */
    char until[YOZORA_SIGNIN_URL_CAP];
    /* or when the cookies for the page include a cookie of this name with a value, empty for none */
    char cookie[YOZORA_SIGNIN_COOKIE_NAME_CAP];
    char state[YOZORA_SIGNIN_STATE_CAP];
} yozora_signin_request;

typedef enum {
    YOZORA_SIGNIN_DONE,
    /* the person closed the tab before the end condition */
    YOZORA_SIGNIN_CANCELLED,
    /* the end condition held but the cookies did not fit the answer */
    YOZORA_SIGNIN_TOO_LARGE
} yozora_signin_outcome;

/* out is zeroed first and holds a request only when the status is OK */
yozora_signin_status yozora_signin_parse(const char *link, yozora_signin_request *out);

/* 1 when the page address, or the cookie header the page would send (name=value; name=value), ends the sign in */
int yozora_signin_finished(const yozora_signin_request *request, const char *page_url, const char *cookie_header);

/* the address to open for the caller: callback, then status=done|cancelled|too_large, url, cookies and state
   (each percent-encoded, empty ones left out). returns the length, 0 when it does not fit capacity, out is
   empty then. page_url and cookie_header may be NULL, CANCELLED and TOO_LARGE carry no cookies */
size_t yozora_signin_result(const yozora_signin_request *request, yozora_signin_outcome outcome, const char *page_url,
                            const char *cookie_header, char *out, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
