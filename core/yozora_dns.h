#ifndef YOZORA_DNS_H
#define YOZORA_DNS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the longest address of a DNS server the settings keep, with the terminator */
#define YOZORA_DNS_CAPACITY 256

typedef enum {
    /* the text is a server, out holds its DNS over HTTPS address */
    YOZORA_DNS_OK,
    /* the text is empty: the resolver of the system answers, out is empty */
    YOZORA_DNS_EMPTY,
    /* the text is not a server this build can ask: out is empty */
    YOZORA_DNS_INVALID_ADDRESS,
    /* the text is an address that would send the names in the clear (http) */
    YOZORA_DNS_INSECURE,
    /* the text does not fit YOZORA_DNS_CAPACITY */
    YOZORA_DNS_TOO_LONG
} yozora_dns_status;

typedef struct {
    const char *name;
    /* what a person types for it, shown in the list of servers */
    const char *address;
    /* the DNS over HTTPS address the pages are resolved through */
    const char *url;
} yozora_dns_preset;

/* the servers offered in the list, a person may type another one */
size_t yozora_dns_preset_count(void);
const yozora_dns_preset *yozora_dns_preset_at(size_t index);

/* what a person typed to the address curl asks. libcurl of this build has no c-ares, so a server is asked over HTTPS (DoH) and not on port 53:
   an https address is kept as it is (host, optional port and path, no user name, no query, no fragment), a bare name or address becomes
   https://NAME/dns-query, and the plain addresses of the known public resolvers become the name of that resolver, which the certificate carries.
   spaces around the text are ignored. out is empty unless the status is OK, and nothing is ever cut */
yozora_dns_status yozora_dns_normalize(const char *input, char *out, size_t capacity);

/* the name to show for a stored address: a preset's name, else the host. empty for an empty address. never longer than capacity - 1 */
void yozora_dns_label(const char *url, char *out, size_t capacity);

/* the server for this process, read by every request of every thread. url is an address from yozora_dns_normalize, or empty for the system.
   an address that does not fit is ignored and the system resolver stays */
void yozora_dns_set(const char *url);
/* copies the server in use into out, returns its length (0 for the system, out is empty then) */
size_t yozora_dns_get(char *out, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
