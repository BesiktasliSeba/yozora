#ifndef YOZORA_FILTER_H
#define YOZORA_FILTER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the rules of the ad blocker: a subset of the syntax of Adblock Plus lists, read from text, matched against the address of a request, and element hiding rules that
   come out as a style sheet for a page.

   network rules:
       ||example.com^        the host and its subdomains (|| starts at a label of the host)
       |https://a/path       the address starts with the text, a trailing | ends it
       /ads/ban*er^          text with * for any run and ^ for a separator (not a letter, digit, _ - . %) or the end
       @@rule                an exception: a request that one matches is let through, whatever blocks it
       options after $       third-party, ~third-party, script, image, stylesheet, font, media, xmlhttprequest, subdocument, ping, websocket, other,
                             any of them with ~ in front, domain=a.com|~b.com. important and match-case are accepted and ignored
   element hiding rules:
       ##selector            hides the elements on every page
       a.com,b.com##selector the same on those sites and their subdomains, ~a.com leaves a site out
       a.com#@#selector      lets a selector that was hidden stay on that site
   a line that starts with ! or [ is a comment, a rule of another kind (regular expressions, scriptlets, $redirect, $csp, #?#) is counted as unsupported and not used.
   the sets are bounded: YOZORA_FILTER_MAX_RULES network rules and YOZORA_FILTER_MAX_HIDING hiding rules, a line of at most YOZORA_FILTER_LINE_MAX bytes */

#define YOZORA_FILTER_MAX_RULES 4000
#define YOZORA_FILTER_MAX_HIDING 1000
#define YOZORA_FILTER_LINE_MAX 1024
/* the longest address that is matched, the rest of a longer one is not looked at */
#define YOZORA_FILTER_URL_MAX 2048

typedef enum {
    YOZORA_FILTER_OTHER = 0,
    YOZORA_FILTER_DOCUMENT,
    YOZORA_FILTER_SUBDOCUMENT,
    YOZORA_FILTER_SCRIPT,
    YOZORA_FILTER_IMAGE,
    YOZORA_FILTER_STYLESHEET,
    YOZORA_FILTER_FONT,
    YOZORA_FILTER_MEDIA,
    YOZORA_FILTER_XHR,
    YOZORA_FILTER_PING,
    YOZORA_FILTER_WEBSOCKET
} yozora_filter_type;

typedef enum {
    YOZORA_FILTER_ADDED = 0,
    /* a blank line or a comment */
    YOZORA_FILTER_SKIPPED,
    YOZORA_FILTER_UNSUPPORTED,
    YOZORA_FILTER_INVALID,
    /* the set is at its limit */
    YOZORA_FILTER_FULL,
    YOZORA_FILTER_NOMEM
} yozora_filter_status;

typedef struct {
    size_t added;
    size_t skipped;
    size_t unsupported;
    size_t invalid;
    size_t dropped;
} yozora_filter_report;

typedef struct {
    const char *url;
    size_t url_length;
    /* the host of the request and the host of the page that makes it, canonical ASCII without a port */
    const char *host;
    size_t host_length;
    const char *page_host;
    size_t page_host_length;
    yozora_filter_type type;
    /* 1 when the request goes to another site than the page, 0 when it stays on it */
    int third_party;
} yozora_filter_request;

typedef struct yozora_filter yozora_filter;

yozora_filter *yozora_filter_create(void);
void yozora_filter_destroy(yozora_filter *filter);

/* the rules that ship with the browser: ad and tracker hosts (yozora_blocklist), a few address patterns and
   ad slot markers */
int yozora_filter_add_builtin(yozora_filter *filter);

/* one line, with or without its line break */
yozora_filter_status yozora_filter_add_line(yozora_filter *filter, const char *line, size_t length);
/* a text of lines. the report says what became of them */
void yozora_filter_add_text(yozora_filter *filter, const char *text, size_t length, yozora_filter_report *report);
/* sorts the host rules for lookup: call it once after the last rule is added */
void yozora_filter_finish(yozora_filter *filter);

size_t yozora_filter_rule_count(const yozora_filter *filter);
size_t yozora_filter_hiding_count(const yozora_filter *filter);

/* 1 when the request is blocked */
int yozora_filter_blocks(const yozora_filter *filter, const yozora_filter_request *request);

/* the style sheet that hides elements for a page of that host, into out of capacity bytes with the
   terminator. returns the length, 0 when nothing is hidden. a sheet that does not fit is cut after the last
   whole rule */
size_t yozora_filter_hiding_css(const yozora_filter *filter, const char *host, size_t host_length, char *out, size_t capacity);

/* 1 when the host is the domain or a subdomain of it, ignoring case. both are canonical host names */
int yozora_filter_host_in_domain(const char *host, size_t host_length, const char *domain, size_t domain_length);

#ifdef __cplusplus
}
#endif
#endif
